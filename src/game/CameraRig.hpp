#pragma once

#include "core/Math.hpp"
#include "game/InputState.hpp"
#include "game/World.hpp"
#include "saturn/Geometry.hpp"

namespace gridfin {

/// Orbit camera that blends seamlessly between third and first person.
///
/// Zooming all the way in (mouse wheel / d-pad) or pressing the view toggle
/// enters first person; zooming out again returns to the over-the-shoulder view.
class CameraRig {
public:
    static constexpr float kMinDistance = 1.6f; ///< closest third person distance
    static constexpr float kMaxDistance = 9.0f;
    static constexpr float kDefaultDistance = 4.5f;
    static constexpr float kZoomStep = 0.75f;

    void reset(float yaw, float pitch);

    /// Applies look/zoom/toggle input. Call once per rendered frame.
    void handleInput(const InputState& input);

    /// Places the camera behind `focus` (the player's feet), avoiding walls.
    void update(float dt, glm::vec3 focus, const World& world);

    [[nodiscard]] saturn::Camera camera() const noexcept { return m_camera; }
    [[nodiscard]] float yaw() const noexcept { return m_yaw; }
    [[nodiscard]] float pitch() const noexcept { return m_pitch; }
    [[nodiscard]] float targetDistance() const noexcept { return m_targetDistance; }
    /// True once the camera has (nearly) reached the player's eyes.
    [[nodiscard]] bool firstPerson() const noexcept { return m_distance < 0.5f; }
    /// True as soon as first person was requested; drives controls immediately.
    [[nodiscard]] bool wantsFirstPerson() const noexcept { return m_targetDistance == 0.0f; }

private:
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_distance = kDefaultDistance;
    float m_targetDistance = kDefaultDistance;
    float m_lastThirdPersonDistance = kDefaultDistance;
    saturn::Camera m_camera;
};

} // namespace gridfin
