#pragma once

#include "saturn/Color.hpp"
#include "saturn/Hardware.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

/// VDP1: the Saturn's sprite/polygon processor.
///
/// The CPU (the SH-2s on real hardware) writes 32-byte commands into VDP1 VRAM
/// and VDP1 draws them in list order into its framebuffer. There is no depth
/// buffer: draw order *is* visibility. Every primitive is a quad whose texture
/// is stretched over it exactly once (no tiling, no filtering, no perspective
/// correction).
///
/// This file models VDP1 VRAM byte-for-byte. The GPU shader decodes the same
/// bytes, so whatever fits here would fit on the real machine.
/// (Fields are stored little-endian; the real console is big-endian.)
namespace gridfin::saturn {

enum class CommandType : std::uint16_t {
    NormalSprite = 0x0,
    ScaledSprite = 0x1,
    DistortedSprite = 0x2,
    Polygon = 0x4,
    End = 0x8000,
};

/// CMDCTRL direction bits (texture flip).
inline constexpr std::uint16_t kFlipH = 1u << 4;
inline constexpr std::uint16_t kFlipV = 1u << 5;

/// CMDPMOD colour modes (bits 5-3).
enum class ColorMode : std::uint16_t {
    Bank16 = 0, ///< 4bpp, 16 colour bank in CRAM
    Lut16 = 1,  ///< 4bpp, 16 entry lookup table in VDP1 VRAM
    Bank64 = 2, ///< 8bpp, 64 colour bank
    Bank128 = 3,
    Bank256 = 4,
    Rgb = 5, ///< 16bpp direct colour
};

/// CMDPMOD colour calculation (bits 1-0).
enum class ColorCalc : std::uint16_t {
    Replace = 0,
    Shadow = 1,        ///< halves the brightness of what is already in the framebuffer
    HalfLuminance = 2, ///< draws at half brightness
    HalfTransparent = 3,
};

namespace pmod {
/// User clipping: draw only inside the clip region. Gridfin's clip region is the VDP2 line window
/// (see Vdp2Registers::window) rather than VDP1's user clip rectangle; see docs/SATURN.md.
inline constexpr std::uint16_t kUserClip = 1u << 10;
inline constexpr std::uint16_t kMesh = 1u << 8; ///< checkerboard "screen door" transparency
inline constexpr std::uint16_t kEndCodeDisable = 1u << 7;
inline constexpr std::uint16_t kTransparentPixelDisable = 1u << 6; ///< SPD: colour 0 is drawn
inline constexpr std::uint16_t kGouraud = 1u << 2;

[[nodiscard]] constexpr std::uint16_t colorMode(ColorMode mode) noexcept
{
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(mode) << 3);
}
[[nodiscard]] constexpr std::uint16_t colorCalc(ColorCalc calc) noexcept
{
    return static_cast<std::uint16_t>(calc);
}
} // namespace pmod

/// One VDP1 command table entry, exactly 32 bytes.
struct Command {
    std::uint16_t ctrl = 0;
    std::uint16_t link = 0;
    std::uint16_t pmod = 0;
    std::uint16_t colr = 0;
    std::uint16_t srca = 0;
    std::uint16_t size = 0;
    std::array<std::int16_t, 8> xy{}; ///< XA YA XB YB XC YC XD YD
    std::uint16_t grda = 0;
    std::uint16_t reserved = 0;
};
static_assert(sizeof(Command) == 32);

/// A texture that lives in VDP1 VRAM, plus what a command needs to draw it.
struct Texture {
    std::uint32_t address = 0; ///< byte address, multiple of 8
    std::uint16_t width = 0;   ///< multiple of 8
    std::uint16_t height = 0;
    ColorMode mode = ColorMode::Rgb;
    std::uint16_t colr = 0; ///< LUT address / 8, or the colour bank

    [[nodiscard]] std::uint16_t srca() const noexcept { return static_cast<std::uint16_t>(address / 8); }
    [[nodiscard]] std::uint16_t size() const noexcept
    {
        return static_cast<std::uint16_t>(((width / 8u) << 8) | height);
    }
    [[nodiscard]] bool valid() const noexcept { return width != 0; }
};

using Lut = std::array<ColorWord, 16>;
using GouraudTable = std::array<ColorWord, 4>;

/// Byte-accurate model of the 512 KB VDP1 VRAM.
///
/// Layout:
///   0x00000  command table   (kMaxCommands * 32 bytes, rewritten every frame)
///   ...      gouraud tables  (kMaxCommands * 8 bytes, rewritten every frame)
///   ...      textures + LUTs (the TextureCache streams these in from work RAM)
class Vdp1Vram {
public:
    static constexpr std::uint32_t kMaxCommands = 2048;
    static constexpr std::uint32_t kCommandTableAddress = 0;
    static constexpr std::uint32_t kGouraudTableAddress = kCommandTableAddress + kMaxCommands * 32;
    static constexpr std::uint32_t kDataAddress = kGouraudTableAddress + kMaxCommands * 8;

    Vdp1Vram();

    /// Reserves `bytes` (rounded up to 8) in the data region.
    [[nodiscard]] std::optional<std::uint32_t> allocate(std::uint32_t bytes);

    [[nodiscard]] std::optional<std::uint32_t> uploadLut(const Lut& lut);
    /// `pixels` holds one palette index (0..15) per pixel. Index 0 is transparent.
    [[nodiscard]] std::optional<Texture> uploadLutTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                          std::uint32_t lutAddress);
    /// 8bpp texture coloured through a 256 colour CRAM bank (bank = first CRAM index, multiple of 256).
    [[nodiscard]] std::optional<Texture> uploadBankTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                           std::uint16_t bank);
    [[nodiscard]] std::optional<Texture> uploadRgbTexture(int width, int height, std::span<const ColorWord> pixels);

    // Per-frame command list -------------------------------------------------
    void clearCommands() noexcept;
    /// Appends a command; returns false once the table is full.
    bool pushCommand(Command command, const GouraudTable* gouraud = nullptr) noexcept;
    [[nodiscard]] std::uint32_t commandCount() const noexcept { return m_commandCount; }

    // Raw access -------------------------------------------------------------
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return m_bytes; }
    [[nodiscard]] std::uint16_t read16(std::uint32_t address) const noexcept;
    void write16(std::uint32_t address, std::uint16_t value) noexcept;
    /// Copies bytes into the data region (a "DMA transfer") and marks them for upload to the GPU.
    void writeData(std::uint32_t address, std::span<const std::uint8_t> data);

    [[nodiscard]] std::uint32_t dataBytesUsed() const noexcept { return m_dataTop - kDataAddress; }
    [[nodiscard]] static constexpr std::uint32_t dataBytesTotal() noexcept { return kVdp1VramSize - kDataAddress; }

    /// Byte ranges of the data region changed since the last call, merged and sorted.
    /// The renderer takes them every frame to update the GPU copy.
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint32_t>> takeDirtyRanges();

private:
    std::vector<std::uint8_t> m_bytes;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> m_dirty; ///< [begin, end)
    std::uint32_t m_dataTop = kDataAddress;
    std::uint32_t m_commandCount = 0;
};

[[nodiscard]] bool isValidTextureSize(int width, int height) noexcept;

/// Texture data in the VRAM byte format of each colour mode.
[[nodiscard]] std::vector<std::uint8_t> packLut16(std::span<const std::uint8_t> indices); ///< 2 pixels per byte
[[nodiscard]] std::vector<std::uint8_t> packLut(const Lut& lut);
[[nodiscard]] std::vector<std::uint8_t> packRgb(std::span<const ColorWord> pixels);

} // namespace gridfin::saturn
