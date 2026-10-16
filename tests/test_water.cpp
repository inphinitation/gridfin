#include "saturn/Geometry.hpp"
#include "saturn/Window.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace gridfin;
using namespace gridfin::saturn;

namespace {

View viewFrom(glm::vec3 position, float pitchDegrees)
{
    Camera camera;
    camera.position = position;
    camera.pitch = radians(pitchDegrees);
    camera.fovY = radians(60.0f);
    return View::fromCamera(camera);
}

/// A 4x4 floor patch centred on (0, 0, -z).
std::array<glm::vec3, 4> floorPatch(float z)
{
    return {glm::vec3(-2.0f, 0.0f, -z - 2.0f), glm::vec3(2.0f, 0.0f, -z - 2.0f), glm::vec3(2.0f, 0.0f, -z + 2.0f),
            glm::vec3(-2.0f, 0.0f, -z + 2.0f)};
}

/// A wall facing the camera, 2 x 2 units, standing on the floor at distance z.
std::array<glm::vec3, 4> wall(float z)
{
    return {glm::vec3(-1.0f, 2.0f, -z), glm::vec3(1.0f, 2.0f, -z), glm::vec3(1.0f, 0.0f, -z),
            glm::vec3(-1.0f, 0.0f, -z)};
}

} // namespace

TEST_CASE("a line window covers the projected outline row by row")
{
    const View view = viewFrom({0.0f, 2.0f, 0.0f}, -20.0f);
    std::array<LineSpan, kScreenHeight> window{};
    const ScreenRect bounds = computeLineWindow(view, floorPatch(6.0f), window);
    REQUIRE_FALSE(bounds.empty());

    // The patch is centred horizontally, below the horizon, and nearer rows are wider.
    const auto& near = window[static_cast<std::size_t>(bounds.maxY + kScreenHeight / 2)];
    const auto& far = window[static_cast<std::size_t>(bounds.minY + kScreenHeight / 2)];
    CHECK(near.end - near.start > far.end - far.start);
    CHECK(near.start + near.end == doctest::Approx(kScreenWidth).epsilon(0.02));
    CHECK(bounds.minY > static_cast<int>(view.horizonRow()) - kScreenHeight / 2);
    CHECK(window[0].start == window[0].end); // the top line is outside
}

TEST_CASE("line windows are clipped at the near plane")
{
    const View view = viewFrom({0.0f, 2.0f, 0.0f}, -20.0f);
    std::array<LineSpan, kScreenHeight> window{};
    // Entirely behind the camera: nothing.
    CHECK(computeLineWindow(view, floorPatch(-6.0f), window).empty());
    // Straddling the camera: still visible, reaching the bottom of the screen.
    const ScreenRect bounds = computeLineWindow(view, floorPatch(1.0f), window);
    REQUIRE_FALSE(bounds.empty());
    CHECK(bounds.maxY == kScreenHeight / 2 - 1);
}

TEST_CASE("reflections are mirrored, darkened, clipped to the window and drawn first")
{
    const View view = viewFrom({0.0f, 2.0f, 4.0f}, -15.0f);
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    geometry.beginReflection(0.0f, ScreenRect{-160, -112, 159, 111});
    geometry.quad(wall(4.0f), {});
    geometry.endReflection();
    geometry.quad(wall(4.0f), {});
    geometry.end(vram);

    REQUIRE(vram.commandCount() == 2);
    // The reflection is first in the command table.
    const std::uint16_t reflectedPmod = vram.read16(4);
    CHECK((reflectedPmod & pmod::kUserClip) != 0);
    CHECK((reflectedPmod & 3u) == static_cast<std::uint16_t>(ColorCalc::HalfLuminance));
    const std::uint16_t realPmod = vram.read16(32 + 4);
    CHECK((realPmod & pmod::kUserClip) == 0);

    // Mirrored: the reflection's top corner (A) is below the real wall's bottom on screen.
    const auto y = [&](std::uint32_t command, int corner) {
        return static_cast<std::int16_t>(vram.read16(command * 32 + 14 + static_cast<std::uint32_t>(corner) * 4));
    };
    CHECK(y(0, 0) > y(1, 3));
}

TEST_CASE("reflected polygons outside the window cost nothing")
{
    const View view = viewFrom({0.0f, 2.0f, 4.0f}, -15.0f);
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    geometry.beginReflection(0.0f, ScreenRect{-160, -112, -150, -100}); // a corner far from the wall
    geometry.quad(wall(4.0f), {});
    geometry.endReflection();
    geometry.end(vram);
    CHECK(vram.commandCount() == 0);
    CHECK(geometry.stats().culledOffscreen == 1);
}

TEST_CASE("specular materials catch a highlight")
{
    // Looking straight down at a floor lit from above: the halfway vector matches the normal.
    Camera camera;
    camera.position = {0.0f, 5.0f, 0.0f};
    camera.pitch = radians(-89.0f);
    const View view = View::fromCamera(camera);
    Lighting lighting;
    lighting.sunDirection = {0.0f, 1.0f, 0.0f};
    lighting.hazeSteps = 0;

    const auto gouraudRed = [&](float specular) {
        Vdp1Vram vram;
        Geometry geometry;
        geometry.begin(view, lighting);
        Material material;
        material.specular = specular;
        material.doubleSided = true;
        geometry.quad(floorPatch(0.0f), material);
        geometry.end(vram);
        REQUIRE(vram.commandCount() == 1);
        return unpack(vram.read16(vram.read16(28) * 8u)).r;
    };
    CHECK(gouraudRed(1.0f) > gouraudRed(0.0f));
}
