#include "game/CameraRig.hpp"
#include "game/Game.hpp"
#include "game/Player.hpp"
#include "game/World.hpp"

#include <doctest/doctest.h>

using namespace gridfin;

namespace {

void simulate(Player& player, const World& world, float seconds, InputState input = {})
{
    for (float t = 0.0f; t < seconds; t += Game::kFixedStep) {
        player.step(Game::kFixedStep, input, 0.0f, false, world);
        input.jumpPressed = false;
    }
}

} // namespace

TEST_CASE("ray vs box")
{
    const Aabb box{{-1, -1, -1}, {1, 1, 1}};
    CHECK(raycast(box, {0, 0, 5}, {0, 0, -1}, 100.0f) == doctest::Approx(4.0f));
    CHECK(raycast(box, {0, 5, 5}, {0, 0, -1}, 100.0f) < 0.0f);
}

TEST_CASE("the player falls onto the ground and onto blocks")
{
    World world;
    world.buildTestLevel();
    Player player;

    player.spawn({0.0f, 3.0f, 8.0f}, 0.0f);
    simulate(player, world, 2.0f);
    CHECK(player.grounded());
    CHECK(player.position().y == doctest::Approx(0.0f));

    // Drop onto the grassy plateau (top at 1.8).
    player.spawn({13.0f, 4.0f, -4.0f}, 0.0f);
    simulate(player, world, 2.0f);
    CHECK(player.grounded());
    CHECK(player.position().y == doctest::Approx(1.8f));
}

TEST_CASE("the player climbs stairs but not walls")
{
    World world;
    world.buildTestLevel();
    Player player;

    // Walk east (+X) up the stairs: camera yaw 90 degrees makes forward point to +X.
    player.spawn({2.5f, 0.0f, -2.5f}, 0.0f);
    InputState input;
    input.move = {0.0f, 1.0f};
    for (int i = 0; i < 150; ++i) player.step(Game::kFixedStep, input, kPi * 0.5f, false, world);
    CHECK(player.position().y > 1.7f);

    // The monolith is 6 units tall: walking into it stops the player.
    player.spawn({0.0f, 0.0f, -9.0f}, 0.0f);
    for (int i = 0; i < 120; ++i) player.step(Game::kFixedStep, input, 0.0f, false, world);
    CHECK(player.position().z == doctest::Approx(-12.0f + Player::kHalfExtents.z).epsilon(0.01));
    CHECK(player.position().y == doctest::Approx(0.0f));
}

TEST_CASE("jumping leaves the ground and comes back")
{
    World world;
    world.buildTestLevel();
    Player player;
    player.spawn({0.0f, 0.0f, 8.0f}, 0.0f);
    simulate(player, world, 0.2f);

    InputState jump;
    jump.jumpPressed = true;
    jump.jumpHeld = true;
    player.step(Game::kFixedStep, jump, 0.0f, false, world);
    CHECK_FALSE(player.grounded());
    simulate(player, world, 0.2f, jump);
    CHECK(player.position().y > 1.0f);
    simulate(player, world, 2.0f);
    CHECK(player.grounded());
}

TEST_CASE("zooming in switches to first person and back")
{
    World world;
    CameraRig rig;
    rig.reset(0.0f, 0.0f);
    CHECK_FALSE(rig.wantsFirstPerson());

    InputState zoomIn;
    zoomIn.zoom = 20.0f;
    rig.handleInput(zoomIn);
    CHECK(rig.wantsFirstPerson());
    for (int i = 0; i < 120; ++i) rig.update(Game::kFixedStep, {0, 0, 0}, world);
    CHECK(rig.firstPerson());

    InputState toggle;
    toggle.toggleViewPressed = true;
    rig.handleInput(toggle);
    CHECK_FALSE(rig.wantsFirstPerson());
    CHECK(rig.targetDistance() >= CameraRig::kMinDistance);
}

TEST_CASE("a full game frame fits in VDP1 and VDP2 memory")
{
    saturn::Machine machine;
    Game game;
    REQUIRE(game.init(machine));
    game.update(Game::kFixedStep, {});
    game.render(machine);
    CHECK(machine.vdp1.commandCount() > 50);
    CHECK(machine.geometry.stats().overflow == 0);
}

TEST_CASE("the game fades in from black and tints the backgrounds at dusk")
{
    saturn::Machine machine;
    Game game;
    REQUIRE(game.init(machine));

    game.render(machine);
    const auto& registers = machine.vdp2Registers;
    CHECK(registers.colorOffsetB.r == -255); // the first frame is black
    CHECK(registers.colorOffsetEnable == saturn::layer::kAll);

    for (int i = 0; i < 60; ++i) game.update(Game::kFixedStep, {});
    game.render(machine);
    CHECK(registers.colorOffsetA == saturn::ColorOffset{});
    CHECK(registers.colorOffsetB == saturn::ColorOffset{});

    InputState toggle;
    toggle.toggleDuskPressed = true;
    game.update(Game::kFixedStep, toggle);
    for (int i = 0; i < 180; ++i) game.update(Game::kFixedStep, {});
    CHECK(game.duskAmount() == doctest::Approx(1.0f));
    game.render(machine);
    CHECK(registers.colorOffsetA.b < 0);
    CHECK(registers.colorOffsetB == saturn::ColorOffset{}); // sprites are lit by their own lights instead
}

TEST_CASE("the pond is visible from beside it and turns on the water layer")
{
    saturn::Machine machine;
    Game game;
    REQUIRE(game.init(machine));
    game.spawn(-4.5f, 5.0f, 0.0f);
    game.render(machine);
    CHECK(machine.vdp2Registers.waterEnabled);
    int insideLines = 0;
    for (const auto& span : machine.vdp2Registers.window) insideLines += span.end > span.start ? 1 : 0;
    CHECK(insideLines > 20);

    // Facing away from it, the window is empty and the layer off.
    game.spawn(-4.5f, 8.0f, kPi);
    game.render(machine);
    CHECK_FALSE(machine.vdp2Registers.waterEnabled);
}
