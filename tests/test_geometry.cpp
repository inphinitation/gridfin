#include "saturn/Geometry.hpp"

#include <doctest/doctest.h>

using namespace gridfin;
using namespace gridfin::saturn;

namespace {

View testView()
{
    Camera camera;
    camera.position = {0.0f, 0.0f, 0.0f};
    camera.fovY = radians(90.0f);
    return View::fromCamera(camera);
}

/// A unit quad facing the camera (which looks down -Z) at distance `z`.
std::array<glm::vec3, 4> facingQuad(float z, float size = 1.0f)
{
    return {glm::vec3(-size, size, -z), glm::vec3(size, size, -z), glm::vec3(size, -size, -z),
            glm::vec3(-size, -size, -z)};
}

std::int16_t commandX(const Vdp1Vram& vram, std::uint32_t index, int corner)
{
    return static_cast<std::int16_t>(vram.read16(index * 32 + 12 + static_cast<std::uint32_t>(corner) * 4));
}

} // namespace

TEST_CASE("the view projects onto the 320x224 screen with 4:3 pixels")
{
    const View view = testView();
    CHECK(view.focalY == doctest::Approx(112.0f));
    CHECK(view.focalX > view.focalY);
    const glm::vec2 centre = view.project(view.toView({0.0f, 0.0f, -5.0f}));
    CHECK(centre.x == doctest::Approx(0.0f));
    CHECK(centre.y == doctest::Approx(0.0f));
    CHECK(view.horizonRow() == doctest::Approx(112.0f));
}

TEST_CASE("front faces are drawn, back faces culled")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});

    Material material;
    geometry.quad(facingQuad(4.0f), material);
    auto back = facingQuad(4.0f);
    std::swap(back[1], back[3]);
    geometry.quad(back, material);
    material.doubleSided = true;
    geometry.quad(back, material);
    geometry.end(vram);

    CHECK(geometry.stats().drawn == 2);
    CHECK(geometry.stats().culledBackface == 1);
}

TEST_CASE("polygons crossing the near plane are dropped, not clipped")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    auto quad = facingQuad(4.0f);
    quad[0].z = 1.0f; // behind the camera
    geometry.quad(quad, {});
    geometry.end(vram);
    CHECK(geometry.stats().culledNear == 1);
    CHECK(vram.commandCount() == 0);
}

TEST_CASE("the ordering table draws far polygons first")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    geometry.quad(facingQuad(2.0f, 0.5f), {});  // near, small on screen
    geometry.quad(facingQuad(20.0f, 4.0f), {}); // far
    geometry.end(vram);

    REQUIRE(vram.commandCount() == 2);
    // The far quad spans 4/20 of the depth, so its corner x is smaller than the near quad's 0.5/2.
    CHECK(std::abs(commandX(vram, 0, 0)) < std::abs(commandX(vram, 1, 0)));
}

TEST_CASE("vertices snap to whole pixels")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    geometry.quad(facingQuad(3.0f, 1.0f), {});
    geometry.end(vram);
    REQUIRE(vram.commandCount() == 1);
    const float expected = -1.0f * view.focalX / 3.0f;
    CHECK(commandX(vram, 0, 0) == static_cast<std::int16_t>(std::lround(expected)));
}

TEST_CASE("billboards become scaled sprites")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    Material material;
    material.texture = Texture{Vdp1Vram::kDataAddress, 32, 64, ColorMode::Lut16, 0};
    geometry.billboard({0.0f, -1.0f, -5.0f}, {1.0f, 2.0f}, material);
    geometry.end(vram);
    REQUIRE(vram.commandCount() == 1);
    CHECK((vram.read16(0) & 0xF) == static_cast<std::uint16_t>(CommandType::ScaledSprite));
}
