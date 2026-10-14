#pragma once

#include <cstdint>

/// Hardware constants of the (NTSC) Sega Saturn that the renderer emulates.
namespace gridfin::saturn {

/// VDP1/VDP2 "low resolution" NTSC mode, non-interlaced.
inline constexpr int kScreenWidth = 320;
inline constexpr int kScreenHeight = 224;

/// The picture is shown at 4:3, so pixels are slightly narrower than tall.
inline constexpr float kDisplayAspect = 4.0f / 3.0f;
inline constexpr float kPixelAspect =
    kDisplayAspect * static_cast<float>(kScreenHeight) / static_cast<float>(kScreenWidth);

/// VDP1 vertex coordinates are 13-bit signed integers.
inline constexpr int kVdp1CoordinateMin = -4096;
inline constexpr int kVdp1CoordinateMax = 4095;

inline constexpr std::uint32_t kVdp1VramSize = 512 * 1024;
inline constexpr std::uint32_t kVdp2VramSize = 512 * 1024;
/// VDP2 colour RAM in mode 1: 2048 RGB555 entries.
inline constexpr std::uint32_t kCramEntries = 2048;

/// Texture widths must be a multiple of 8 (max 504), heights at most 255.
inline constexpr int kMaxTextureWidth = 504;
inline constexpr int kMaxTextureHeight = 255;

} // namespace gridfin::saturn
