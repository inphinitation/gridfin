#include "game/Player.hpp"

#include <array>
#include <cmath>

namespace gridfin {

using saturn::ColorCalc;
using saturn::Material;

namespace {

/// Rotates a model space point (forward = -Z) by `yaw` and moves it to `origin`.
[[nodiscard]] glm::vec3 toWorld(glm::vec3 local, float yaw, glm::vec3 origin) noexcept
{
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    return origin + glm::vec3(local.x * c - local.z * s, local.y, local.x * s + local.z * c);
}

void drawPart(saturn::Geometry& geometry, const Aabb& local, float yaw, glm::vec3 origin, saturn::TextureId side,
              saturn::TextureId front, float depthBias)
{
    Material material;
    material.depthBias = depthBias;
    material.specular = 0.8f; // polished metal
    const auto faces = boxFaces(local);
    for (std::size_t f = 0; f < faces.size(); ++f) {
        const BoxFace& face = faces[f];
        material.texture = f == kFaceNegZ ? front : side;
        geometry.quad({toWorld(face.a, yaw, origin), toWorld(face.a + face.u, yaw, origin),
                       toWorld(face.a + face.u + face.v, yaw, origin), toWorld(face.a + face.v, yaw, origin)},
                      material);
    }
}

} // namespace

void Player::spawn(glm::vec3 feet, float yaw)
{
    m_position = m_previousPosition = feet;
    m_velocity = glm::vec3(0.0f);
    m_yaw = yaw;
    m_grounded = false;
}

void Player::moveAxis(int axis, float delta, const World& world)
{
    if (delta == 0.0f) return;
    m_position[axis] += delta;

    const auto collides = [&](glm::vec3 feet) {
        const Aabb box = bounds(feet);
        for (const Aabb& collider : world.colliders())
            if (box.overlaps(collider)) return true;
        return false;
    };

    for (const Aabb& collider : world.colliders()) {
        if (!bounds(m_position).overlaps(collider)) continue;

        if (axis == 1) {
            if (delta < 0.0f) {
                m_position.y = collider.max.y;
                m_grounded = true;
            } else {
                m_position.y = collider.min.y - kHalfExtents.y * 2.0f;
            }
            m_velocity.y = 0.0f;
            continue;
        }

        // Floor contact from rounding is not a wall; small ledges such as stairs are walked up.
        const float rise = collider.max.y - m_position.y;
        if (rise <= 1e-3f) continue;
        if (m_grounded && rise <= kStepHeight) {
            const glm::vec3 raised{m_position.x, collider.max.y + 1e-3f, m_position.z};
            if (!collides(raised)) {
                m_position = raised;
                continue;
            }
        }

        constexpr float kSkin = 1e-4f;
        m_position[axis] = delta > 0.0f ? collider.min[axis] - kHalfExtents[axis] - kSkin
                                        : collider.max[axis] + kHalfExtents[axis] + kSkin;
        m_velocity[axis] = 0.0f;
    }

    if (axis == 1 && m_position.y < World::kGroundHeight) {
        m_position.y = World::kGroundHeight;
        m_velocity.y = 0.0f;
        m_grounded = true;
    }
}

void Player::step(float dt, const InputState& input, float cameraYaw, bool firstPerson, const World& world)
{
    m_previousPosition = m_position;

    const glm::vec3 forward{std::sin(cameraYaw), 0.0f, -std::cos(cameraYaw)};
    const glm::vec3 right{std::cos(cameraYaw), 0.0f, std::sin(cameraYaw)};
    glm::vec3 wish = forward * input.move.y + right * input.move.x;
    const float wishLength = glm::length(wish);
    if (wishLength > 1.0f) wish /= wishLength;

    const float speed = input.sprint ? kRunSpeed : kWalkSpeed;
    const float sharpness = m_grounded ? 14.0f : 3.5f;
    const float blend = smoothingFactor(sharpness, dt);
    m_velocity.x = glm::mix(m_velocity.x, wish.x * speed, blend);
    m_velocity.z = glm::mix(m_velocity.z, wish.z * speed, blend);

    if (firstPerson) {
        m_yaw = cameraYaw;
    } else if (wishLength > 0.1f) {
        m_yaw = wrapAngle(approachAngle(m_yaw, std::atan2(wish.x, -wish.z), smoothingFactor(12.0f, dt)));
    }

    // Jumping with a little input buffering and coyote time.
    m_coyoteTime = m_grounded ? 0.1f : m_coyoteTime - dt;
    m_jumpBuffer = input.jumpPressed ? 0.12f : m_jumpBuffer - dt;
    if (m_jumpBuffer > 0.0f && m_coyoteTime > 0.0f) {
        m_velocity.y = kJumpSpeed;
        m_jumpBuffer = 0.0f;
        m_coyoteTime = 0.0f;
        m_grounded = false;
    }
    float gravity = kGravity;
    if (m_velocity.y > 0.0f && !input.jumpHeld) gravity *= 2.2f; // short hops
    m_velocity.y = std::max(m_velocity.y - gravity * dt, -30.0f);

    moveAxis(0, m_velocity.x * dt, world);
    moveAxis(2, m_velocity.z * dt, world);
    m_grounded = false;
    moveAxis(1, m_velocity.y * dt, world);

    const float horizontalSpeed = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
    m_walkCycle = m_grounded ? std::fmod(m_walkCycle + horizontalSpeed * dt * 2.2f, kTau) : m_walkCycle * 0.9f;
}

void Player::draw(saturn::Geometry& geometry, const Assets& assets, const World& world, glm::vec3 feet,
                  bool drawBody) const
{
    // Blob shadow: VDP1 "shadow" colour calculation halves what is underneath.
    const float floor = world.floorHeight(feet.x, feet.z, feet.y + 0.05f);
    const float size = 0.42f * std::clamp(1.0f - (feet.y - floor) / 6.0f, 0.4f, 1.0f);
    const float y = floor + 0.02f;
    Material shadow;
    shadow.texture = assets.shadow;
    shadow.colorCalc = ColorCalc::Shadow;
    shadow.lit = false;
    shadow.depthBias = -0.6f;
    geometry.quad({glm::vec3(feet.x - size, y, feet.z - size), glm::vec3(feet.x + size, y, feet.z - size),
                   glm::vec3(feet.x + size, y, feet.z + size), glm::vec3(feet.x - size, y, feet.z + size)},
                  shadow);

    if (!drawBody) return;

    constexpr float kBias = -0.7f;
    const float swing = std::sin(m_walkCycle) * 0.14f;
    const auto box = [](glm::vec3 min, glm::vec3 max) { return Aabb{min, max}; };

    // Legs and arms swing opposite to each other.
    drawPart(geometry, box({-0.23f, 0.0f, -0.1f + swing}, {-0.05f, 0.5f, 0.1f + swing}), m_yaw, feet, assets.robotBody,
             assets.robotBody, kBias);
    drawPart(geometry, box({0.05f, 0.0f, -0.1f - swing}, {0.23f, 0.5f, 0.1f - swing}), m_yaw, feet, assets.robotBody,
             assets.robotBody, kBias);
    drawPart(geometry, box({-0.28f, 0.5f, -0.2f}, {0.28f, 1.1f, 0.2f}), m_yaw, feet, assets.robotBody, assets.robotBody,
             kBias);
    drawPart(geometry, box({-0.43f, 0.62f, -0.07f - swing}, {-0.29f, 1.07f, 0.07f - swing}), m_yaw, feet,
             assets.robotBody, assets.robotBody, kBias);
    drawPart(geometry, box({0.29f, 0.62f, -0.07f + swing}, {0.43f, 1.07f, 0.07f + swing}), m_yaw, feet,
             assets.robotBody, assets.robotBody, kBias);
    drawPart(geometry, box({-0.25f, 1.13f, -0.23f}, {0.25f, 1.55f, 0.22f}), m_yaw, feet, assets.robotBody,
             assets.robotFace, kBias);

    // The dorsal fin: a double sided quad with two corners collapsed into a triangle.
    Material fin;
    fin.texture = assets.fin;
    fin.doubleSided = true;
    fin.depthBias = kBias;
    const glm::vec3 tip = toWorld({0.0f, 1.95f, 0.28f}, m_yaw, feet);
    geometry.quad({tip, tip, toWorld({0.0f, 1.55f, 0.22f}, m_yaw, feet), toWorld({0.0f, 1.55f, -0.12f}, m_yaw, feet)},
                  fin);
}

} // namespace gridfin
