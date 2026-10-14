#include "game/Game.hpp"

#include "core/Log.hpp"

#include <cmath>

namespace gridfin {

using namespace saturn;

namespace {
constexpr ColorWord kZenith = rgbHex(0x1C4AA0);
constexpr ColorWord kSky = rgbHex(0x4C8ED8);
constexpr ColorWord kHorizon = rgbHex(0xB8D8F0);
constexpr glm::vec3 kSpawn{0.0f, 0.0f, 6.0f};
} // namespace

bool Game::init(Machine& machine)
{
    if (!m_assets.load(machine.vdp1, machine.vdp2)) return false;
    m_world.buildTestLevel();
    m_player.spawn(kSpawn, 0.0f);
    m_cameraRig.reset(0.0f, radians(-12.0f));
    m_cameraRig.update(0.0f, kSpawn, m_world);
    return true;
}

void Game::update(float dt, const InputState& input)
{
    m_time += dt;
    m_cameraRig.handleInput(input);

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

void Game::render(Machine& machine) const
{
    machine.view = View::fromCamera(m_cameraRig.camera());

    Geometry& geometry = machine.geometry;
    geometry.begin(machine.view, m_lighting);
    m_world.draw(geometry, m_assets);
    m_player.draw(geometry, m_assets, m_world, m_player.interpolatedPosition(m_accumulator / kFixedStep),
                  !m_cameraRig.firstPerson());
    geometry.end(machine.vdp1);

    computeSky(machine.view, machine.vdp2Registers);
}

} // namespace gridfin
