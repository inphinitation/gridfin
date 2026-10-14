#pragma once

#include <cstdint>

namespace gridfin::saturn {

/// A Saturn colour word. Bit 15 set means direct RGB (5 bits per channel,
/// laid out B G R from high to low); bit 15 clear means a colour bank code
/// that indexes VDP2 colour RAM.
using ColorWord = std::uint16_t;

inline constexpr ColorWord kRgbFlag = 0x8000;
/// RGB pixel value that VDP1 treats as transparent (unless SPD is set).
inline constexpr ColorWord kTransparentRgb = 0x0000;

[[nodiscard]] constexpr ColorWord rgb555(unsigned r5, unsigned g5, unsigned b5) noexcept
{
    return static_cast<ColorWord>(kRgbFlag | ((b5 & 31u) << 10) | ((g5 & 31u) << 5) | (r5 & 31u));
}

/// Converts 8-bit-per-channel colour, truncating like the hardware DAC input would.
[[nodiscard]] constexpr ColorWord rgb(unsigned r8, unsigned g8, unsigned b8) noexcept
{
    return rgb555(r8 >> 3, g8 >> 3, b8 >> 3);
}

[[nodiscard]] constexpr ColorWord rgbHex(std::uint32_t hex) noexcept
{
    return rgb((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

struct Rgb5 {
    int r = 0;
    int g = 0;
    int b = 0;

    friend constexpr bool operator==(const Rgb5&, const Rgb5&) = default;
};

[[nodiscard]] constexpr Rgb5 unpack(ColorWord color) noexcept
{
    return {color & 31, (color >> 5) & 31, (color >> 10) & 31};
}

[[nodiscard]] constexpr int clamp5(int value) noexcept
{
    return value < 0 ? 0 : (value > 31 ? 31 : value);
}

/// Linear blend in 5-bit space; `t` in 0..31 like the VDP2 colour calculation ratio.
[[nodiscard]] constexpr ColorWord blend(ColorWord a, ColorWord b, int t) noexcept
{
    const Rgb5 ca = unpack(a);
    const Rgb5 cb = unpack(b);
    const auto mix = [t](int x, int y) { return clamp5(x + ((y - x) * t) / 31); };
    return rgb555(static_cast<unsigned>(mix(ca.r, cb.r)), static_cast<unsigned>(mix(ca.g, cb.g)),
                  static_cast<unsigned>(mix(ca.b, cb.b)));
}

/// Neutral gouraud value: VDP1 adds (gouraud - 16) to each channel.
inline constexpr ColorWord kGouraudNeutral = rgb555(16, 16, 16);

/// Builds a gouraud table entry from a signed per-channel offset (-16..15).
[[nodiscard]] constexpr ColorWord gouraudOffset(int r, int g, int b) noexcept
{
    return rgb555(static_cast<unsigned>(clamp5(r + 16)), static_cast<unsigned>(clamp5(g + 16)),
                  static_cast<unsigned>(clamp5(b + 16)));
}

} // namespace gridfin::saturn
