#include <doctest/doctest.h>
#include "Gui/SpatialNavigation.hpp"
#include <array>

using namespace Funke::SpatialNavigation;

TEST_CASE("Spatial focus follows geometry rather than registry order") {
    const std::array<Point, 4> points{{ {0,-100}, {100,0}, {0,100}, {-100,0} }};
    CHECK(Pick({0,0}, {1,0}, points, false) == 1);
    CHECK(Pick({0,0}, {-1,0}, points, false) == 3);
    CHECK(Pick({0,0}, {0,1}, points, false) == 2);
    CHECK(Pick({0,0}, {0,-1}, points, false) == 0);
}

TEST_CASE("Spatial focus balances alignment and distance with stable ties") {
    const std::array<Point, 4> points{{ {10,100}, {100,0}, {100,0}, {0,0} }};
    CHECK(Pick({0,0}, {1,0}, points, false) == 1);
    CHECK(Pick({0,0}, {0,0}, points, true) == None);
    CHECK(Pick({0,0}, {-1,0}, points, false) == None);
    CHECK(Pick({0,0}, {1,0}, {}, true) == None);
}

TEST_CASE("Spatial focus wraps to opposite edge of nearest row only when enabled") {
    const std::array<Point, 3> points{{ {0,10}, {-50,0}, {-100,0} }};
    CHECK(Pick({100,0}, {1,0}, points, false) == None);
    CHECK(Pick({100,0}, {1,0}, points, true) == 2);
    const std::array<Point, 2> invalid{{ {NAN,0}, {INFINITY,0} }};
    CHECK(Pick({0,0}, {1,0}, invalid, true) == None);
    CHECK(Pick({NAN,0}, {1,0}, points, true) == None);
}
