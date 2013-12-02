#include <geode/math/constants.h>
#include <geode/image/image.h>
#include <geode/image/JpgFile.h>
#include <geode/geometry/platonic.h>
#include <geode/random/Random.h>
#include <geode/openmesh/TriMesh.h>
#include <geode/utility/DebugPrint.h>
#include <geode/math/lerp.h>
#include <geode/utility/interrupts.h>
#include <geode/array/IndirectArray.h>

#include <fstream>

#include "moon.h"

namespace omc {
using namespace geode;

typedef unsigned char PixelType;

static Vec2 normalized_lat_lon(const Vec3 normal) {
  const real latitude = 0.5 + atan2(normal.z, normal.xy().magnitude()) / (pi);
  const real longitude = 0.5 + atan2(normal.y, normal.x) / (2*pi);
  assert(latitude >= 0 && latitude <= 1);
  assert(longitude >= 0 && longitude <= 1);
  return Vec2(latitude, longitude);
}

// Generates barycentric coordinates for uniform samples on a triangle from uniform samples on unit square
// Based on http://math.stackexchange.com/questions/18686/uniform-random-point-in-triangle
static Vec3 barycentric_map(Vec2 uv) {
  const real r1 = sqrt(uv[0]);
  const real r2 = uv[1];
  return Vec3(1 - r1, r1 * (1- r2), r1 * r2);
}

// roll components of barycentric_coord so it will be closest to first vertex
static Vec3 rotate_into_trident(Vec3 barycentric_coord) {
  return barycentric_coord.roll(barycentric_coord.argmax());
}

// Map points in unit square to points in third of triangle closest to first vertex
static Vec3 trident_sample(const Vec2 uv) {
  return rotate_into_trident(barycentric_map(uv));
}

// Should have same behavior as geode::wrap, but I was running in to some kind of issue (possibly with overloads)
static inline int test_wrap(const int i, const int lower, const int upper) {
  int r = (i-lower) % (upper-lower);
  return (r >= 0) ? r+lower : r+upper;
}

class PolarTexture {
  Array<unsigned char, 2> data;
 public:
  Vec2i dims;
  // These values come from documentation provided with the sorce topo data
  const real h_base = 1737400;
  const real h_min = h_base - 18250; // Min value in image will map to this height
  const real h_max = h_base + 21546; // Max value in image will map to this height

  PolarTexture(const Array<Vector<unsigned char,3>, 2>& image)
  {
    data.m = image.m;
    data.n = image.n;
    data.flat.preallocate(image.flat.size());
    for(const auto& p : image.flat) {
      data.flat.append(p[0]);
    }
    dims = data.sizes();
    dims.y /= 3; // WWWWWHHHHHHYYYY!!!!!?!?!?!?!?!????
  }
  real sample(Vec3 x) const {
    const Vec2 lat_lon = normalized_lat_lon(x);
    Vec2i icoords = Vec2i(floor(lat_lon*Vec2(dims) + Vec2::ones()*0.5));
    icoords.x = test_wrap(icoords.x, 0, dims.x - 1);
    icoords.y = test_wrap(icoords.y, 0, dims.y - 1);

    if(!data.valid(icoords))
      return 0.5;
    return lerp(data[icoords] / (255.), h_min, h_max) / (h_min);
  }
};

static std::vector<char> file_bytes(std::string filename) {
  std::vector<char> bytes;

  std::ifstream file(filename.c_str());
  if(file.is_open()) {
    file.seekg(0, std::ios::end);
    bytes.resize(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(&bytes[0], bytes.size());
    file.close();
  }
  return bytes;
}

// From: http://stackoverflow.com/questions/2182002/convert-big-endian-to-little-endian-in-c-without-using-provided-func
static uint32_t swap_uint32( uint32_t val )
{
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF ); 
    return (val << 16) | ((val >> 16) & 0xFFFF);
}

Array<const float, 2> load_raw(const string& filename, const int width, const int height) {
  const auto raw_bytes = file_bytes(filename);
  static_assert(sizeof(float) == 4, "Need 32 bit floats to load file");
  assert(raw_bytes.size() % 4 == 0);
  const int nvals = raw_bytes.size() / 4;
  GEODE_PROBE(nvals, width, nvals % width);
  assert(nvals == width*height);
  union RawFloat {
    uint32_t as_int;
    float as_float;
  };
  static_assert(sizeof(RawFloat) == 4, "Padding bytes in union. This isn't going to work");

  auto linear_array = RawArray<RawFloat>(nvals, (RawFloat*)(raw_bytes.data()));
  for(auto& x : linear_array) {
    x.as_int = swap_uint32(x.as_int);
  }

  auto result = Array<float, 2>();
  result.m = height;
  result.n = width;
  result.flat = Array<float>(linear_array.size(), false);
  for(int i : range(linear_array.size())) {
    result.flat[i] = linear_array[i].as_float;
  }
  return result;
}

void make_the_moon(const string& src_heightmap_filename, const string& dst_mesh) {
  const int refinements = 7;
  // const real r_min = 1728490.000000;
  //const real r_max = 1745339.000000;

  // const int height = 2880;
  // const int width = 5760;

  std::cout << "Loading heightmap" << std::endl;
  //load_raw(src_heightmap_filename, width, height); // 
  const auto heightmap = PolarTexture(JpgFile<unsigned char>::read(src_heightmap_filename));
  const Vec2i dims = heightmap.dims;
  std::cout << "Heightmap resolution is " << dims << std::endl;

  const real r = 1.0;

  std::cout << "Generating initial mesh" << std::endl;
  const auto mesh_X = sphere_mesh(refinements, Vec3(0,0,0), r);
  const auto& mesh = *(mesh_X.x);
  const auto& X = mesh_X.y;

  Array<Vec3> new_x;
  new_x.preallocate(mesh_X.y.size());
  const int n_verts = mesh_X.y.size();
  std::cout << "Processing " << n_verts << " verticies." << std::endl;
  int percent = 0;
  int vert_count = 0;

  const auto incident_elements = mesh.incident_elements();
  const int num_samples = 50;
  Ref<Random> rng = new_<Random>(123408);
  for(const int vid : range(X.size())) {
    real total_h = 0, total_w = 0;

    for(int adjacent_triangle_id : incident_elements[vid]) {
      Vec3i verts = mesh.elements[adjacent_triangle_id];
      const int first_i = verts.find(vid);
      assert(first_i >= 0);
      verts.roll(first_i); // Make current vertex closest be 0
      const auto tri = Triangle<Vec3>(X.subset(verts));
      for(int i = 0; i < num_samples; ++i) {
        // Samples are randomly placed on third of triangle closest to vertex 0 (which should be vid)
        // It would be faster just to sample in some kind of grid, but it isn't obvious to me how to generate one
        const Vec2 uv = Vec2(rng->uniform(),rng->uniform());
        const Vec3 bary = trident_sample(uv);
        const Vec3 sample_pt = tri.point_from_barycentric_coordinates(bary);
        total_h += heightmap.sample(sample_pt);
        total_w += 1.;
      }
    }
    const real h = total_h / total_w;
    new_x.append(X[vid]*h);
    const int new_percent = int(100 * vert_count / n_verts);
    if(new_percent > percent) {
      std::cout << "  at " << new_percent << '%' << std::endl;
      percent = new_percent;
      check_interrupts();
    }
    ++vert_count;
  }
  std::cout << "Writing output" << std::endl;
  auto out_mesh = new_<TriMesh>(tuple(mesh_X.x, new_x));
  out_mesh->write(dst_mesh);
}

} // namespace omc
