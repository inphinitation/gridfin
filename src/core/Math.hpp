#pragma once

#define GLM_FORCE_XYZW_ONLY
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace gridfin {

inline constexpr float kPi = glm::pi<float>();
inline constexpr float kTau = 2.0f * kPi;

[[nodiscard]] constexpr float radians(float degrees) noexcept
{
    return degrees * (kPi / 180.0f);
}

/// Frame-rate independent exponential smoothing factor for `sharpness` per second.
[[nodiscard]] inline float smoothingFactor(float sharpness, float dt) noexcept
{
    return 1.0f - std::exp(-sharpness * dt);
}

/// Wraps an angle to (-pi, pi].
[[nodiscard]] inline float wrapAngle(float angle) noexcept
{
    angle = std::fmod(angle + kPi, kTau);
    if (angle < 0.0f) angle += kTau;
    return angle - kPi;
}

/// Moves `angle` towards `target` along the shortest arc.
[[nodiscard]] inline float approachAngle(float angle, float target, float t) noexcept
{
    return angle + wrapAngle(target - angle) * t;
}

/// Axis aligned bounding box.
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] glm::vec3 center() const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 size() const noexcept { return max - min; }

    [[nodiscard]] bool overlaps(const Aabb& other) const noexcept
    {
        return min.x < other.max.x && max.x > other.min.x && min.y < other.max.y && max.y > other.min.y
               && min.z < other.max.z && max.z > other.min.z;
    }

    [[nodiscard]] static Aabb fromCenter(glm::vec3 center, glm::vec3 halfExtents) noexcept
    {
        return {center - halfExtents, center + halfExtents};
    }
};

/// Ray vs AABB slab test. Returns the entry distance along `dir` or a negative value on a miss.
[[nodiscard]] inline float raycast(const Aabb& box, glm::vec3 origin, glm::vec3 dir, float maxDistance) noexcept
{
    float tMin = 0.0f;
    float tMax = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(dir[axis]) < 1e-8f) {
            if (origin[axis] < box.min[axis] || origin[axis] > box.max[axis]) return -1.0f;
            continue;
        }
        const float inv = 1.0f / dir[axis];
        float t0 = (box.min[axis] - origin[axis]) * inv;
        float t1 = (box.max[axis] - origin[axis]) * inv;
        if (t0 > t1) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) return -1.0f;
    }
    return tMin;
}

} // namespace gridfin
