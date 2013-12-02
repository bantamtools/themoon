//#####################################################################
// Module themoon
//#####################################################################
#include <geode/python/module.h>
#include <geode/python/wrap.h>
#include <themoon/moon.h>
using namespace geode;
using namespace omc;

GEODE_PYTHON_MODULE(other_themoon) {
  GEODE_FUNCTION(make_the_moon)
}
