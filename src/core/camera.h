#pragma once
#include <glm/glm.hpp>

namespace fr {

/// Simple orbit camera around a target point.
class OrbitCamera {
public:
    glm::vec3 target{0.0f};
    float distance = 2.0f;
    float yaw = 0.0f;     ///< radians
    float pitch = 0.0f;   ///< radians
    float fovY = glm::radians(40.0f);
    float nearZ = 0.01f, farZ = 100.0f;

    glm::vec3 position() const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;
    void orbit(float dYaw, float dPitch);
    void pan(float dx, float dy);   ///< screen-space pan scaled by distance
    void zoom(float factor);
};

} // namespace fr
