#include "saturn/Color.hpp"

#include <doctest/doctest.h>

using namespace gridfin::saturn;

TEST_CASE("RGB555 words use the Saturn bit layout")
{
    CHECK(rgb555(31, 0, 0) == 0x801F);
    CHECK(rgb555(0, 31, 0) == 0x83E0);
    CHECK(rgb555(0, 0, 31) == 0xFC00);
    CHECK(rgb(255, 255, 255) == 0xFFFF);
    CHECK(rgbHex(0x08F800) == rgb555(1, 31, 0));
}

TEST_CASE("unpack is the inverse of rgb555")
{
    CHECK(unpack(rgb555(3, 17, 29)) == Rgb5{3, 17, 29});
}

TEST_CASE("gouraud offsets are centred on 16 and clamped")
{
    CHECK(gouraudOffset(0, 0, 0) == kGouraudNeutral);
    CHECK(unpack(gouraudOffset(-4, 2, 15)) == Rgb5{12, 18, 31});
    CHECK(unpack(gouraudOffset(-40, 40, 0)) == Rgb5{0, 31, 16});
}

TEST_CASE("blend interpolates in 32 steps")
{
    const ColorWord black = rgb555(0, 0, 0);
    const ColorWord white = rgb555(31, 31, 31);
    CHECK(blend(black, white, 0) == black);
    CHECK(blend(black, white, 31) == white);
    CHECK(unpack(blend(black, white, 16)).r == 16);
}
