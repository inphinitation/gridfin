#include "saturn/TextureCache.hpp"
#include "saturn/Vdp1Timing.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace gridfin::saturn;

namespace {

/// A 32x32 4bpp texture: 512 bytes of pixels plus a 32 byte lookup table.
TextureId addTexture(TextureCache& cache)
{
    const std::vector<std::uint8_t> pixels(32 * 32, 1);
    const auto id = cache.addLutTexture(32, 32, pixels, Lut{});
    REQUIRE(id);
    return *id;
}

constexpr std::uint32_t kTextureBytes = 512 + 32;

Command quad(std::uint16_t size, int x0, int y0, int x1, int y1)
{
    Command command;
    command.ctrl = static_cast<std::uint16_t>(CommandType::DistortedSprite);
    command.size = size;
    command.xy = {static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y0), static_cast<std::int16_t>(x1),
                  static_cast<std::int16_t>(y0), static_cast<std::int16_t>(x1), static_cast<std::int16_t>(y1),
                  static_cast<std::int16_t>(x0), static_cast<std::int16_t>(y1)};
    return command;
}

} // namespace

TEST_CASE("textures live in work RAM until they are used")
{
    Vdp1Vram vram;
    TextureCache cache(vram);
    const TextureId id = addTexture(cache);
    CHECK(cache.stats().workRamBytes == kTextureBytes);
    CHECK_FALSE(cache.isResident(id));

    cache.beginFrame();
    const auto texture = cache.acquire(id);
    REQUIRE(texture);
    CHECK(cache.isResident(id));
    CHECK(texture->address % 8 == 0);
    CHECK(texture->colr * 8u + 32u == texture->address); // the LUT sits in front of the pixels
    CHECK(cache.stats().dmaBytes == kTextureBytes);

    // Already resident: no further transfer.
    cache.beginFrame();
    REQUIRE(cache.acquire(id));
    CHECK(cache.stats().dmaBytes == 0);
}

TEST_CASE("the per-frame DMA budget limits uploads")
{
    Vdp1Vram vram;
    TextureCache cache(vram);
    cache.dmaBytesPerFrame = kTextureBytes * 2;
    std::vector<TextureId> ids;
    for (int i = 0; i < 3; ++i) ids.push_back(addTexture(cache));

    cache.beginFrame();
    CHECK(cache.acquire(ids[0]));
    CHECK(cache.acquire(ids[1]));
    CHECK_FALSE(cache.acquire(ids[2]));
    CHECK(cache.stats().misses == 1);

    cache.beginFrame();
    CHECK(cache.acquire(ids[2]));
}

TEST_CASE("least recently used textures are evicted, but never ones the frame needs")
{
    Vdp1Vram vram;
    TextureCache cache(vram, 2 * kTextureBytes); // room for exactly two textures
    const TextureId a = addTexture(cache);
    const TextureId b = addTexture(cache);
    const TextureId c = addTexture(cache);

    cache.beginFrame();
    REQUIRE(cache.acquire(a));
    cache.beginFrame();
    REQUIRE(cache.acquire(b));
    cache.beginFrame();
    REQUIRE(cache.acquire(b));
    REQUIRE(cache.acquire(c)); // evicts a, the least recently used
    CHECK(cache.stats().evictions == 1);
    CHECK_FALSE(cache.isResident(a));
    CHECK(cache.isResident(b));

    // b and c are both in use this frame, so a cannot come back yet.
    CHECK_FALSE(cache.acquire(a));
}

TEST_CASE("work RAM is a hard limit")
{
    Vdp1Vram vram;
    TextureCache cache(vram);
    const std::vector<ColorWord> big(504 * 255, rgb555(1, 2, 3));
    int added = 0;
    while (cache.addRgbTexture(504, 255, big)) ++added;
    CHECK(added == static_cast<int>(TextureCache::kWorkRamBudget / (504 * 255 * 2)));
}

TEST_CASE("VDP1 time grows with texture height, area and read-modify-write colour calculation")
{
    const std::uint16_t small = (4u << 8) | 32u; // 32x32
    const std::uint16_t tall = (4u << 8) | 255u; // 32x255
    const std::uint32_t base = timing::commandCycles(quad(small, 0, 0, 10, 10));

    // A tiny quad still walks every texture row.
    CHECK(timing::commandCycles(quad(tall, 0, 0, 10, 10)) > base);
    CHECK(timing::commandCycles(quad(small, 0, 0, 100, 100)) > base);

    Command transparent = quad(small, 0, 0, 100, 100);
    transparent.pmod = static_cast<std::uint16_t>(ColorCalc::HalfTransparent);
    CHECK(timing::commandCycles(transparent) > timing::commandCycles(quad(small, 0, 0, 100, 100)));

    // A screen-filling quad costs a noticeable part of a field.
    const std::uint32_t fullscreen = timing::commandCycles(quad(small, -160, -112, 159, 111));
    CHECK(fullscreen > timing::kCyclesPerField / 10);
}
