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

/// Layer bits for the colour offset enable/select registers.
namespace layer {
inline constexpr std::uint8_t kBack = 1u << 0;
inline constexpr std::uint8_t kNbg0 = 1u << 1;
inline constexpr std::uint8_t kRbg0 = 1u << 2;
inline constexpr std::uint8_t kSprite = 1u << 3; ///< everything VDP1 drew
inline constexpr std::uint8_t kNbg1 = 1u << 4;   ///< the water reflection layer
inline constexpr std::uint8_t kAll = kBack | kNbg0 | kRbg0 | kSprite | kNbg1;
} // namespace layer

/// A colour offset register: signed values (-256..255) added to each 8-bit channel.
struct ColorOffset {
    int r = 0;
    int g = 0;
    int b = 0;

    friend constexpr bool operator==(const ColorOffset&, const ColorOffset&) = default;
};

/// One scanline of a line window: pixels with start <= x < end are inside.
struct LineSpan {
    std::int16_t start = 0;
    std::int16_t end = 0;
};

/// One line scroll table entry: which source line to show on this scanline, shifted sideways.
struct LineScroll {
    std::int16_t row = 0;
    std::int16_t offsetX = 0;
};

/// Per-frame VDP2 register state (scroll values, rotation parameters, ...).
struct Vdp2Registers {
    /// Colour offset: flashes, fades and tints at no polygon cost. Each layer whose bit is set in
    /// `colorOffsetEnable` gets offset A, or B if its bit is also set in `colorOffsetSelect`.
    ColorOffset colorOffsetA;
    ColorOffset colorOffsetB;
    std::uint8_t colorOffsetEnable = 0;
    std::uint8_t colorOffsetSelect = 0;

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

    /// Window 0 as a line window: a span per scanline, computed by the CPU each frame. VDP1
    /// commands with user clipping are clipped to it too (see pmod::kUserClip).
    std::uint32_t windowTableAddress = 0;
    std::array<LineSpan, kScreenHeight> window{};

    /// NBG1, the water layer: inside window 0 it replaces RBG0 with a reflection of the sky.
    /// Each scanline shows the line given by the line scroll table (mirrored about the
    /// horizon) shifted sideways for ripples, blended towards `waterColor`.
    bool waterEnabled = false;
    std::uint32_t reflectionTableAddress = 0;
    std::array<LineScroll, kScreenHeight> reflection{};
    ColorWord waterColor = rgb(40, 90, 120);
    int waterRatio = 10; ///< colour calculation ratio towards waterColor, 0..31

    /// NBG0 bitmap whose bottom row sits on the horizon, scrolled horizontally with the camera yaw.
    bool cloudsEnabled = false;
    Bitmap clouds;
    float cloudScrollX = 0.0f;
};

} // namespace gridfin::saturn
