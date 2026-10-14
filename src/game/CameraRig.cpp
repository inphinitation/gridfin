#include "game/CameraRig.hpp"

#include "game/Player.hpp"

namespace gridfin {

namespace {
constexpr float kThirdPersonPitchMin = radians(-70.0f);
constexpr float kThirdPersonPitchMax = radians(60.0f);
constexpr float kFirstPersonPitchLimit = radians(85.0f);
constexpr float kPivotHeight = 1.35f;
} // namespace

void CameraRig::reset(float yaw, float pitch)
{
    m_yaw = yaw;
    m_pitch = pitch;
    m_distance = m_targetDistance = m_lastThirdPersonDistance = kDefaultDistance;
}

void CameraRig::handleInput(const InputState& input)
{
    m_yaw = wrapAngle(m_yaw + input.look.x);
    m_pitch += input.look.y;

    if (input.toggleViewPressed) {
        if (wantsFirstPerson()) {
            m_targetDistance = m_lastThirdPersonDistance;
        } else {
            m_lastThirdPersonDistance = m_targetDistance;
            m_targetDistance = 0.0f;
        }
    }

    if (input.zoom != 0.0f) {
        float distance = m_targetDistance;
        if (wantsFirstPerson()) {
            // Zooming out of first person lands on the closest third person distance.
            if (input.zoom < 0.0f) distance = kMinDistance;
        } else {
            distance -= input.zoom * kZoomStep;
            if (distance < kMinDistance - 0.01f) distance = 0.0f;
        }
        m_targetDistance = distance == 0.0f ? 0.0f : std::clamp(distance, kMinDistance, kMaxDistance);
        if (!wantsFirstPerson()) m_lastThirdPersonDistance = m_targetDistance;
    }

    const bool fp = wantsFirstPerson();
    m_pitch = std::clamp(m_pitch, fp ? -kFirstPersonPitchLimit : kThirdPersonPitchMin,
                         fp ? kFirstPersonPitchLimit : kThirdPersonPitchMax);
}

void CameraRig::update(float dt, glm::vec3 focus, const World& world)
{
    m_distance = glm::mix(m_distance, m_targetDistance, smoothingFactor(10.0f, dt));
    if (std::abs(m_distance - m_targetDistance) < 0.01f) m_distance = m_targetDistance;

    // Blend the pivot from the shoulders (third person) up to the eyes (first person).
    const float t = std::clamp(m_distance / kMinDistance, 0.0f, 1.0f);
    const glm::vec3 pivot = focus + glm::vec3(0.0f, glm::mix(Player::kEyeHeight, kPivotHeight, t), 0.0f);

    m_camera.yaw = m_yaw;
    m_camera.pitch = m_pitch;
    const float cp = std::cos(m_pitch);
    const glm::vec3 forward{std::sin(m_yaw) * cp, std::sin(m_pitch), -std::cos(m_yaw) * cp};

    float distance = m_distance;
    if (distance > 0.0f) {
        // Pull the camera in front of anything between it and the player.
        const float hit = world.raycast(pivot, -forward, distance + 0.3f);
        distance = std::min(distance, std::max(hit - 0.3f, 0.0f));
    }
    m_camera.position = pivot - forward * distance;
    m_camera.nearZ = 0.1f;
    m_camera.farZ = 64.0f;
    m_camera.fovY = radians(glm::mix(66.0f, 58.0f, t));
}

} // namespace gridfin
