#pragma once

#include "core/Math.hpp"
#include "game/Assets.hpp"
#include "game/InputState.hpp"
#include "game/World.hpp"
#include "saturn/Geometry.hpp"

namespace gridfin {

/// The player character, a small robot named Gridfin.
class Player {
public:
    static constexpr glm::vec3 kHalfExtents{0.3f, 0.85f, 0.3f};
    static constexpr float kEyeHeight = 1.55f;
    static constexpr float kWalkSpeed = 4.5f;
    static constexpr float kRunSpeed = 7.5f;
    static constexpr float kJumpSpeed = 8.5f;
    static constexpr float kGravity = 24.0f;
    static constexpr float kStepHeight = 0.35f;

    void spawn(glm::vec3 feet, float yaw);

    /// One fixed simulation step. `cameraYaw` orients the movement input.
    void step(float dt, const InputState& input, float cameraYaw, bool firstPerson, const World& world);

    /// Position at `alpha` between the previous and the current simulation step.
    [[nodiscard]] glm::vec3 interpolatedPosition(float alpha) const noexcept
    {
        return glm::mix(m_previousPosition, m_position, alpha);
    }

    [[nodiscard]] glm::vec3 position() const noexcept { return m_position; }
    [[nodiscard]] glm::vec3 velocity() const noexcept { return m_velocity; }
    [[nodiscard]] bool grounded() const noexcept { return m_grounded; }
    [[nodiscard]] float yaw() const noexcept { return m_yaw; }

    void draw(saturn::Geometry& geometry, const Assets& assets, const World& world, glm::vec3 feet,
              bool drawBody) const;

private:
    [[nodiscard]] Aabb bounds(glm::vec3 feet) const noexcept
    {
        // Built from the feet so that min.y equals feet.y exactly (no rounding against floors).
        return {feet - glm::vec3(kHalfExtents.x, 0.0f, kHalfExtents.z),
                feet + glm::vec3(kHalfExtents.x, kHalfExtents.y * 2.0f, kHalfExtents.z)};
    }
    void moveAxis(int axis, float delta, const World& world);

    glm::vec3 m_position{0.0f};
    glm::vec3 m_previousPosition{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_yaw = 0.0f;
    float m_walkCycle = 0.0f;
    float m_coyoteTime = 0.0f;
    float m_jumpBuffer = 0.0f;
    bool m_grounded = false;
};

} // namespace gridfin
