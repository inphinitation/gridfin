#pragma once

#include "saturn/Color.hpp"
#include "saturn/Hardware.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// VDP2: the Saturn's background processor.
///
/// VDP2 draws the scrolling and rotating background planes that VDP1 sprites
/// and polygons are composited on top of. Gridfin uses three of its features:
///
///  - the back screen with a per-line colour table (the sky gradient),
///  - a normal scroll plane, NBG0, with a cloud bitmap that scrolls with the camera,
///  - the rotation plane, RBG0, as an infinite textured ground ("Mode 7" style).
///
/// Bitmaps live in a byte-accurate 512 KB VRAM image and resolve colours
/// through the 2048 entry colour RAM (CRAM), which VDP1 colour bank modes share.
namespace gridfin::saturn {

struct Bitmap {
    std::uint32_t address = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t bank = 0; ///< CRAM index of palette entry 0 (8bpp bitmaps)
};

class Vdp2Vram {
public:
    Vdp2Vram();

    [[nodiscard]] std::optional<std::uint32_t> allocate(std::uint32_t bytes);

    /// 8bpp palettised bitmap. Valid sizes on hardware: 512x256, 512x512, 1024x256, 1024x512.
    [[nodiscard]] std::optional<Bitmap> uploadBitmap(int width, int height, std::span<const std::uint8_t> pixels,
                                                     std::uint16_t bank);

    /// Writes `colors` into CRAM starting at `index`.
    void setCram(std::uint16_t index, std::span<const ColorWord> colors);
    [[nodiscard]] ColorWord cram(std::uint16_t index) const noexcept { return m_cram[index]; }

    void write16(std::uint32_t address, std::uint16_t value) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::span<const ColorWord> cramWords() const noexcept { return m_cram; }
    [[nodiscard]] std::uint32_t bytesUsed() const noexcept { return m_top; }

    bool vramDirty = true;
    bool cramDirty = true;

private:
    std::vector<std::uint8_t> m_bytes;
    std::array<ColorWord, kCramEntries> m_cram{};
    std::uint32_t m_top = 0;
};

/// Per-frame VDP2 register state (scroll values, rotation parameters, ...).
struct Vdp2Registers {
    /// Back screen line colour table: one RGB colour per scanline, stored in VRAM.
    std::uint32_t lineColorAddress = 0;
    std::array<ColorWord, kScreenHeight> lineColors{};

    /// RBG0 rotation plane used as an infinite ground at `groundHeight`.
    bool groundEnabled = false;
    Bitmap ground;
    float groundHeight = 0.0f;
    float groundTexelsPerUnit = 16.0f;
    /// The ground fades into `hazeColor` between these distances, in 32 steps.
    float fadeStart = 20.0f;
    float fadeEnd = 120.0f;
    ColorWord hazeColor = rgb(200, 220, 240);

    /// NBG0 bitmap whose bottom row sits on the horizon, scrolled horizontally with the camera yaw.
    bool cloudsEnabled = false;
    Bitmap clouds;
    float cloudScrollX = 0.0f;
};

} // namespace gridfin::saturn
