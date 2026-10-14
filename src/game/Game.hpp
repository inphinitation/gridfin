#pragma once

#include "game/Assets.hpp"
#include "game/CameraRig.hpp"
#include "game/InputState.hpp"
#include "game/Player.hpp"
#include "game/World.hpp"
#include "saturn/Machine.hpp"

namespace gridfin {

/// Platform independent game logic: simulation and building each frame for the Saturn renderer.
class Game {
public:
    static constexpr float kFixedStep = 1.0f / 60.0f;

    [[nodiscard]] bool init(saturn::Machine& machine);

    /// Advances the game by one rendered frame of `dt` seconds.
    void update(float dt, const InputState& input);

    /// Produces this frame's VDP1 command list and VDP2 register state.
    void render(saturn::Machine& machine) const;

    [[nodiscard]] const Player& player() const noexcept { return m_player; }
    [[nodiscard]] const CameraRig& cameraRig() const noexcept { return m_cameraRig; }

private:
    void computeSky(const saturn::View& view, saturn::Vdp2Registers& registers) const;

    Assets m_assets;
    World m_world;
    Player m_player;
    CameraRig m_cameraRig;
    saturn::Lighting m_lighting;
    float m_accumulator = 0.0f;
    float m_time = 0.0f;
    bool m_jumpLatch = false;
};

} // namespace gridfin
