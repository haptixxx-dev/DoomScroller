#pragma once

#include <glm/glm.hpp>

namespace ds {

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 uv;
    glm::vec3 normal;
};

} // namespace ds
