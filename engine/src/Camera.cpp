#include "engine/Camera.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ds {

glm::vec3 Camera::front() const {
    float y = glm::radians(yaw);
    float p = glm::radians(pitch);
    return glm::normalize(glm::vec3{
        std::cos(p) * std::cos(y),
        std::sin(p),
        std::cos(p) * std::sin(y),
    });
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(front(), glm::vec3{0.f, 1.f, 0.f}));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, position + front(), glm::vec3{0.f, 1.f, 0.f});
}

glm::mat4 Camera::projMatrix(float aspect) const {
    // GLM_FORCE_DEPTH_ZERO_TO_ONE set via CMake; Slang auto-flips Y for SPIRV.
    return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
}

void Camera::rotate(float dx, float dy) {
    yaw += dx * lookSensitivity;
    pitch -= dy * lookSensitivity;
    pitch = std::clamp(pitch, -89.f, 89.f);
}

void Camera::moveLocal(glm::vec3 dir, float dt) {
    position += front() * dir.z * moveSpeed * dt;
    position += right() * dir.x * moveSpeed * dt;
    position += glm::vec3{0.f, 1.f, 0.f} * dir.y * moveSpeed * dt;
}

} // namespace ds
