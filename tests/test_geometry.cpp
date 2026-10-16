#include "saturn/Geometry.hpp"

#include <doctest/doctest.h>

#include <vector>

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
    TextureCache textures(vram);
    const std::vector<std::uint8_t> pixels(32 * 64, 1);
    const auto id = textures.addLutTexture(32, 64, pixels, Lut{});
    REQUIRE(id);
    Geometry geometry;
    geometry.begin(view, {}, &textures);
    Material material;
    material.texture = *id;
    geometry.billboard({0.0f, -1.0f, -5.0f}, {1.0f, 2.0f}, material);
    geometry.end(vram);
    REQUIRE(vram.commandCount() == 1);
    CHECK((vram.read16(0) & 0xF) == static_cast<std::uint16_t>(CommandType::ScaledSprite));
}

TEST_CASE("textured quads fall back to a flat polygon until their texture is resident")
{
    const View view = testView();
    Vdp1Vram vram;
    TextureCache textures(vram);
    textures.dmaBytesPerFrame = 0; // nothing can be transferred this frame
    std::vector<std::uint8_t> pixels(16 * 16, 2);
    Lut lut{};
    lut[2] = rgb555(31, 0, 0);
    const auto id = textures.addLutTexture(16, 16, pixels, lut);
    REQUIRE(id);

    Material material;
    material.texture = *id;
    Geometry geometry;
    geometry.begin(view, {}, &textures);
    geometry.quad(facingQuad(4.0f), material);
    geometry.end(vram);
    REQUIRE(vram.commandCount() == 1);
    CHECK((vram.read16(0) & 0xF) == static_cast<std::uint16_t>(CommandType::Polygon));
    CHECK(vram.read16(6) == rgb555(31, 0, 0)); // the texture's average colour
    CHECK(geometry.stats().textureMisses == 1);

    textures.dmaBytesPerFrame = TextureCache::kDefaultDmaBytesPerFrame;
    geometry.begin(view, {}, &textures);
    geometry.quad(facingQuad(4.0f), material);
    geometry.end(vram);
    CHECK((vram.read16(0) & 0xF) == static_cast<std::uint16_t>(CommandType::DistortedSprite));
    CHECK(geometry.stats().textureMisses == 0);
}

TEST_CASE("the frame reports its VDP1 drawing time")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    geometry.quad(facingQuad(4.0f), {});
    geometry.end(vram);
    CHECK(geometry.stats().vdp1Cycles > 0);
    CHECK(geometry.stats().vdp1Load() < 1.0f);
    CHECK(geometry.stats().fieldsNeeded() == 1);
}

TEST_CASE("an overloaded command list needs more than one field")
{
    const View view = testView();
    Vdp1Vram vram;
    Geometry geometry;
    geometry.begin(view, {});
    Material transparent;
    transparent.colorCalc = ColorCalc::HalfTransparent;
    // Screen-filling half-transparent layers: the classic way to tank a Saturn's frame rate.
    for (int i = 0; i < 12; ++i) geometry.quad(facingQuad(1.0f + static_cast<float>(i) * 0.01f, 1.2f), transparent);
    geometry.end(vram);
    CHECK(geometry.stats().vdp1Load() > 1.0f);
    CHECK(geometry.stats().fieldsNeeded() >= 2);
}

TEST_CASE("billboards fade to mesh up close and are culled before they fill the screen")
{
    const View view = testView();
    Vdp1Vram vram;
    TextureCache textures(vram);
    const std::vector<std::uint8_t> pixels(32 * 64, 1);
    const auto id = textures.addLutTexture(32, 64, pixels, Lut{});
    REQUIRE(id);
    Material material;
    material.texture = *id;

    const auto drawAt = [&](float distance) {
        Geometry geometry;
        geometry.begin(view, {}, &textures);
        geometry.billboard({0.0f, -0.5f, -distance}, {0.5f, 1.0f}, material);
        geometry.end(vram);
        return vram.commandCount();
    };

    REQUIRE(drawAt(5.0f) == 1);
    CHECK((vram.read16(4) & pmod::kMesh) == 0);
    REQUIRE(drawAt(1.0f) == 1);
    CHECK((vram.read16(4) & pmod::kMesh) != 0);
    CHECK(drawAt(0.5f) == 0);
}

TEST_CASE("point lights brighten nearby corners in their own colour")
{
    const View view = testView();
    Vdp1Vram vram;
    Lighting lighting;
    lighting.ambient = 0.5f;
    lighting.diffuse = 0.0f;
    lighting.hazeSteps = 0;

    const auto cornerGouraud = [&](const Lighting& light) {
        Geometry geometry;
        geometry.begin(view, light);
        geometry.quad(facingQuad(4.0f), {});
        geometry.end(vram);
        REQUIRE(vram.commandCount() == 1);
        const std::uint32_t table = vram.read16(28) * 8u;
        return std::array<Rgb5, 2>{unpack(vram.read16(table)), unpack(vram.read16(table + 2))};
    };

    const auto dark = cornerGouraud(lighting);
    // An orange light in front of corner A (top-left, at x = -1, y = 1, z = -4).
    REQUIRE(lighting.addPointLight({{-1.0f, 1.0f, -3.5f}, {1.0f, 0.5f, 0.0f}, 2.0f}));
    const auto lit = cornerGouraud(lighting);

    CHECK(lit[0].r > dark[0].r);
    CHECK(lit[0].r > lit[0].g);
    CHECK(lit[0].b == dark[0].b);
    CHECK(lit[1] == dark[1]); // corner B is beyond the light's radius
}

TEST_CASE("only a few point lights are allowed")
{
    Lighting lighting;
    for (std::size_t i = 0; i < Lighting::kMaxPointLights; ++i) CHECK(lighting.addPointLight({}));
    CHECK_FALSE(lighting.addPointLight({}));
}
