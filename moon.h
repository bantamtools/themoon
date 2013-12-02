#pragma once
#include <geode/image/image.h>
#include <geode/geometry/platonic.h>
#include <geode/random/Random.h>

namespace omc {

void make_the_moon(const std::string& src_heightmap_filename, const std::string& dst_mesh);

} // namespace omc