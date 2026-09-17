#include "core/camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace fr {

glm::vec3 OrbitCamera::position() const {
    glm::vec3 dir(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
    return target + dir * distance;
}
glm::mat4 OrbitCamera::view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }
glm::mat4 OrbitCamera::projection(float aspect) const { return glm::perspective(fovY, std::max(aspect, 1e-3f), nearZ, farZ); }
void OrbitCamera::orbit(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, glm::radians(-89.0f), glm::radians(89.0f));
}
void OrbitCamera::pan(float dx, float dy) {
    glm::mat4 v = view();
    glm::vec3 right(v[0][0], v[1][0], v[2][0]);
    glm::vec3 up(v[0][1], v[1][1], v[2][1]);
    target += (-right * dx + up * dy) * distance;
}
void OrbitCamera::zoom(float factor) { distance = std::clamp(distance * factor, 0.05f, 50.0f); }

} // namespace fr
