#include "saturn/Vdp1.hpp"
#include "saturn/Vdp2.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace gridfin::saturn;

TEST_CASE("texture sizes follow VDP1 rules")
{
    CHECK(isValidTextureSize(8, 1));
    CHECK(isValidTextureSize(504, 255));
    CHECK_FALSE(isValidTextureSize(12, 8));
    CHECK_FALSE(isValidTextureSize(512, 8));
    CHECK_FALSE(isValidTextureSize(8, 256));
}

TEST_CASE("4bpp textures pack the left pixel into the high nibble")
{
    Vdp1Vram vram;
    const auto lut = vram.uploadLut(Lut{});
    REQUIRE(lut);
    std::vector<std::uint8_t> pixels(8 * 2, 0);
    pixels[0] = 0x1;
    pixels[1] = 0x2;
    pixels[15] = 0xF;
    const auto texture = vram.uploadLutTexture(8, 2, pixels, *lut);
    REQUIRE(texture);
    CHECK(texture->address % 8 == 0);
    CHECK(texture->mode == ColorMode::Lut16);
    CHECK(texture->colr == *lut / 8);
    CHECK(texture->size() == ((1 << 8) | 2));
    CHECK(vram.bytes()[texture->address] == 0x12);
    CHECK(vram.bytes()[texture->address + 7] == 0x0F);
}

TEST_CASE("VRAM runs out like the real thing")
{
    Vdp1Vram vram;
    const std::vector<ColorWord> big(504 * 255, rgb555(1, 2, 3));
    int uploaded = 0;
    while (vram.uploadRgbTexture(504, 255, big)) ++uploaded;
    CHECK(uploaded == static_cast<int>(Vdp1Vram::dataBytesTotal() / (504 * 255 * 2)));
    CHECK(vram.dataBytesUsed() <= Vdp1Vram::dataBytesTotal());
}

TEST_CASE("command table is terminated and gouraud tables are linked")
{
    Vdp1Vram vram;
    CHECK(vram.read16(0) == static_cast<std::uint16_t>(CommandType::End));

    Command command;
    command.ctrl = static_cast<std::uint16_t>(CommandType::Polygon);
    const GouraudTable gouraud{rgb555(1, 1, 1), rgb555(2, 2, 2), rgb555(3, 3, 3), rgb555(4, 4, 4)};
    REQUIRE(vram.pushCommand(command, &gouraud));
    CHECK(vram.commandCount() == 1);
    CHECK(vram.read16(32) == static_cast<std::uint16_t>(CommandType::End));

    const std::uint16_t pmod = vram.read16(4);
    CHECK((pmod & pmod::kGouraud) != 0);
    const std::uint32_t grda = vram.read16(28) * 8u;
    CHECK(grda == Vdp1Vram::kGouraudTableAddress);
    CHECK(vram.read16(grda + 6) == rgb555(4, 4, 4));
}

TEST_CASE("command table capacity is enforced")
{
    Vdp1Vram vram;
    std::uint32_t pushed = 0;
    while (vram.pushCommand(Command{})) ++pushed;
    CHECK(pushed == Vdp1Vram::kMaxCommands - 1);
}

TEST_CASE("VDP2 bitmaps must use hardware sizes")
{
    Vdp2Vram vram;
    const std::vector<std::uint8_t> pixels(512 * 256, 1);
    CHECK(vram.uploadBitmap(512, 256, pixels, 256));
    CHECK_FALSE(vram.uploadBitmap(256, 512, pixels, 256));
    CHECK_FALSE(vram.uploadBitmap(512, 256, pixels, 100)); // bank must be a multiple of 256
}
