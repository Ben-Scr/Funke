#pragma once
#include <cstdint>

namespace Funke {
    enum  class ShapeType : int { Square, Circle, Polygon };
    enum class BodyType : int { Static, Kinematic, Dynamic };
}