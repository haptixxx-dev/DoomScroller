#pragma once

#include <glm/glm.hpp>

namespace ds {

class Camera {
  public:
    glm::vec3 position    = {0.f, 0.f, 3.f};
    float yaw             = -90.f; // degrees; -90 = looking along -Z
    float pitch           = 0.f;
    float fovY            = 70.f;
    float nearZ           = 0.01f;
    float farZ            = 1000.f;
    float moveSpeed       = 5.f; // units/sec
    float lookSensitivity = 0.1f;

    glm::vec3 front() const;
    glm::vec3 right() const;
    glm::mat4 viewMatrix() const;
    glm::mat4 projMatrix(float aspect) const;

    void rotate(float deltaX, float deltaY);
    void moveLocal(glm::vec3 dir, float dt);
};

} // namespace ds
