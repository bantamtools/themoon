themoon
=======

planet terrain mesh generation. because the moon.


Installation:
 1.) Install and configure the otherlab/other repository
 2.) Clone themoon into a folder in $OTHER (i.e. inside the otherlab/other directory)
   cd $OTHER
   git clone git@github.com:omco/themoon.git
 3.) Build with scons (This might need to be repeated a few times due to a dependency error in the build scripts)
   cd $OTHER/themoon
   scons -u -j7 type=debug
 4.) Run make_the_moon
   ./make_the_moon

Many values are hardcoded into moon.cpp:

PolarTexture::h_base, h_min, and h_max map heightmap gray levels to radius

change refinements in moon.cpp:make_the_moon(...) to adjust number of subdivisions
  Resolution grows quickly as number of faces in result will be 20 * (4 << refinements) 
  refinements = 7 or 8 is probably a reasonable value
  refinements = 10 will probably be too big to load on most machines

