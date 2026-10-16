#include "game/Game.hpp"

#include "core/Log.hpp"
#include "saturn/Window.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gridfin {

using namespace saturn;

namespace {
constexpr ColorWord kZenith = rgbHex(0x1C4AA0);
constexpr ColorWord kSky = rgbHex(0x4C8ED8);
constexpr ColorWord kHorizon = rgbHex(0xB8D8F0);
constexpr glm::vec3 kSpawn{0.0f, 0.0f, 6.0f};
constexpr float kFadeSeconds = 0.8f;
constexpr float kDuskSeconds = 2.0f;
/// Dusk tint for the VDP2 backgrounds, in 8-bit colour offset steps.
constexpr ColorOffset kDuskOffset{-72, -84, -28};
constexpr glm::vec3 kFireColor{1.0f, 0.55f, 0.2f};
} // namespace

bool Game::init(Machine& machine)
{
    if (!m_assets.load(machine.textures, machine.vdp2)) return false;
    m_world.buildTestLevel();
    m_player.spawn(kSpawn, 0.0f);
    m_cameraRig.reset(0.0f, radians(-12.0f));
    m_cameraRig.update(0.0f, kSpawn, m_world);
    return true;
}

void Game::spawn(float x, float z, float yaw)
{
    const glm::vec3 feet{x, m_world.floorHeight(x, z, 100.0f), z};
    m_player.spawn(feet, yaw);
    m_cameraRig.reset(yaw, radians(-12.0f));
    m_cameraRig.update(0.0f, feet, m_world);
}

void Game::update(float dt, const InputState& input)
{
    m_time += dt;
    m_cameraRig.handleInput(input);

    // Fade in from black at start-up, and ease between day and dusk.
    m_fade = std::max(0.0f, m_fade - dt / kFadeSeconds);
    if (input.toggleDuskPressed) m_dusk = !m_dusk;
    const float duskStep = dt / kDuskSeconds;
    m_duskAmount = std::clamp(m_duskAmount + (m_dusk ? duskStep : -duskStep), 0.0f, 1.0f);

    // Fixed-step simulation; buffered presses survive frames that run no step.
    m_jumpLatch = m_jumpLatch || input.jumpPressed;
    m_accumulator = std::min(m_accumulator + dt, 0.25f);
    while (m_accumulator >= kFixedStep) {
        InputState stepInput = input;
        stepInput.jumpPressed = m_jumpLatch;
        m_jumpLatch = false;
        m_player.step(kFixedStep, stepInput, m_cameraRig.yaw(), m_cameraRig.wantsFirstPerson(), m_world);
        m_accumulator -= kFixedStep;
    }

    m_cameraRig.update(dt, m_player.interpolatedPosition(m_accumulator / kFixedStep), m_world);
}

void Game::computeSky(const View& view, Vdp2Registers& registers) const
{
    // The back screen gradient is a per-scanline colour table, recomputed every frame for the camera pitch.
    const float centre = static_cast<float>(kScreenHeight) * 0.5f;
    for (int line = 0; line < kScreenHeight; ++line) {
        const float elevation = view.pitch + std::atan((centre - static_cast<float>(line) - 0.5f) / view.focalY);
        ColorWord color = kHorizon;
        if (elevation > 0.0f) {
            const float t = std::sqrt(std::min(elevation / radians(60.0f), 1.0f));
            color = t < 0.5f ? blend(kHorizon, kSky, static_cast<int>(t * 2.0f * 31.0f))
                             : blend(kSky, kZenith, static_cast<int>((t - 0.5f) * 2.0f * 31.0f));
        }
        registers.lineColors[static_cast<std::size_t>(line)] = color;
    }

    registers.groundEnabled = true;
    registers.ground = m_assets.ground;
    registers.groundHeight = World::kGroundHeight;
    registers.groundTexelsPerUnit = 16.0f;
    registers.fadeStart = 18.0f;
    registers.fadeEnd = 110.0f;
    registers.hazeColor = kHorizon;

    registers.cloudsEnabled = true;
    registers.clouds = m_assets.clouds;
    // Two copies of the 512 pixel bitmap span a full turn.
    registers.cloudScrollX = view.yaw / kTau * 1024.0f + m_time * 1.5f;
}

Lighting Game::frameLighting() const
{
    Lighting lighting = m_lighting;
    const float dusk = m_duskAmount;
    lighting.ambient = glm::mix(m_lighting.ambient, 0.3f, dusk);
    lighting.diffuse = glm::mix(m_lighting.diffuse, 0.12f, dusk);
    // By day the distance haze brightens towards the sky; at dusk it darkens into the night.
    lighting.hazeSteps = static_cast<int>(std::lround(glm::mix(static_cast<float>(m_lighting.hazeSteps), -7.0f, dusk)));

    // Fires flicker; only the nearest few get a light, as lighting costs CPU time per vertex.
    std::vector<const Brazier*> braziers;
    for (const Brazier& brazier : m_world.braziers()) braziers.push_back(&brazier);
    const glm::vec3 focus = m_player.position();
    std::sort(braziers.begin(), braziers.end(), [&](const Brazier* a, const Brazier* b) {
        return glm::distance(a->base, focus) < glm::distance(b->base, focus);
    });
    for (std::size_t i = 0; i < braziers.size() && i < Lighting::kMaxPointLights; ++i) {
        const float seed = static_cast<float>(i) * 1.7f;
        const float flicker = 0.85f + 0.1f * std::sin(m_time * 13.0f + seed) + 0.05f * std::sin(m_time * 31.0f + seed);
        const float strength = glm::mix(0.55f, 1.0f, dusk) * flicker;
        lighting.addPointLight({braziers[i]->lightPosition(), kFireColor * strength, glm::mix(5.0f, 7.0f, dusk)});
    }
    return lighting;
}

void Game::setColorOffset(Vdp2Registers& registers) const
{
    // Offset A tints the backgrounds for dusk; offset B is the sprite layer. Both carry the fade.
    const int fade = static_cast<int>(std::lround(-255.0f * m_fade));
    const auto scaled = [&](int value) {
        return static_cast<int>(std::lround(static_cast<float>(value) * m_duskAmount));
    };
    registers.colorOffsetA = {std::max(-256, scaled(kDuskOffset.r) + fade),
                              std::max(-256, scaled(kDuskOffset.g) + fade),
                              std::max(-256, scaled(kDuskOffset.b) + fade)};
    registers.colorOffsetB = {fade, fade, fade};
    registers.colorOffsetEnable = layer::kAll;
    registers.colorOffsetSelect = layer::kSprite;
}

void Game::computeWater(const View& view, Vdp2Registers& registers) const
{
    // Ripples: each scanline of the reflection shows the mirrored line, shifted sideways a
    // little. Lines further below the horizon (closer water) ripple more.
    const float horizon = view.horizonRow();
    for (int line = 0; line < kScreenHeight; ++line) {
        const float y = static_cast<float>(line);
        const float depth = std::clamp((y - horizon) / 40.0f, 0.0f, 1.0f);
        const float wave = std::sin(y * 0.9f + m_time * 4.0f);
        LineScroll& scroll = registers.reflection[static_cast<std::size_t>(line)];
        scroll.row = static_cast<std::int16_t>(std::lround(2.0f * horizon - y - 1.0f + wave * depth));
        scroll.offsetX = static_cast<std::int16_t>(std::lround(wave * (0.5f + 2.5f * depth)));
    }
    registers.waterColor = rgb(40, 96, 112);
    registers.waterRatio = 9;
}

void Game::render(Machine& machine) const
{
    machine.view = View::fromCamera(m_cameraRig.camera());
    Vdp2Registers& registers = machine.vdp2Registers;
    const glm::vec3 feet = m_player.interpolatedPosition(m_accumulator / kFixedStep);

    Geometry& geometry = machine.geometry;
    geometry.begin(machine.view, frameLighting(), &machine.textures);
    // Palette cycling: the fire's and the water's lookup tables are rewritten in VRAM (32 bytes of DMA each).
    machine.textures.setLut(m_assets.fire, Assets::fireLut(m_time));
    machine.textures.setLut(m_assets.water, Assets::waterLut(m_time));

    // The pond: the CPU works out its on-screen footprint as a VDP2 line window. Inside it the
    // ground is replaced by the reflection layer, and the objects around the pond are drawn
    // mirrored (clipped to the window) before the mesh water surface goes on top.
    const Pond& pond = m_world.pond();
    const auto outline = pond.outline();
    const ScreenRect window = computeLineWindow(machine.view, outline, registers.window);
    registers.waterEnabled = !window.empty();
    if (registers.waterEnabled) {
        computeWater(machine.view, registers);
        const Aabb reflected = pond.reflectedArea(6.0f);
        geometry.beginReflection(pond.surfaceY, window);
        m_world.draw(geometry, m_assets, &reflected);
        // You see yourself in the water even in first person.
        if (reflected.overlaps(Aabb::fromCenter(feet, glm::vec3(0.5f)))) {
            m_player.draw(geometry, m_assets, m_world, feet, true);
        }
        geometry.endReflection();
    }

    m_world.draw(geometry, m_assets);
    m_player.draw(geometry, m_assets, m_world, feet, !m_cameraRig.firstPerson());
    geometry.end(machine.vdp1);

    computeSky(machine.view, registers);
    setColorOffset(registers);
}

} // namespace gridfin
