#include "saturn/Vdp1.hpp"

#include <bit>
#include <cstring>

namespace gridfin::saturn {

bool isValidTextureSize(int width, int height) noexcept
{
    return width > 0 && height > 0 && width % 8 == 0 && width <= kMaxTextureWidth && height <= kMaxTextureHeight;
}

Vdp1Vram::Vdp1Vram() : m_bytes(kVdp1VramSize, 0)
{
    clearCommands();
}

std::optional<std::uint32_t> Vdp1Vram::allocate(std::uint32_t bytes)
{
    const std::uint32_t rounded = (bytes + 7u) & ~7u;
    if (rounded > kVdp1VramSize - m_dataTop) return std::nullopt;
    const std::uint32_t address = m_dataTop;
    m_dataTop += rounded;
    dataDirty = true;
    return address;
}

std::uint16_t Vdp1Vram::read16(std::uint32_t address) const noexcept
{
    return static_cast<std::uint16_t>(m_bytes[address] | (m_bytes[address + 1] << 8));
}

void Vdp1Vram::write16(std::uint32_t address, std::uint16_t value) noexcept
{
    m_bytes[address] = static_cast<std::uint8_t>(value & 0xFF);
    m_bytes[address + 1] = static_cast<std::uint8_t>(value >> 8);
}

std::optional<std::uint32_t> Vdp1Vram::uploadLut(const Lut& lut)
{
    const auto address = allocate(32);
    if (!address) return std::nullopt;
    for (std::uint32_t i = 0; i < lut.size(); ++i) write16(*address + i * 2, lut[i]);
    return address;
}

std::optional<Texture> Vdp1Vram::uploadLutTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                  std::uint32_t lutAddress)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!isValidTextureSize(width, height) || pixels.size() != count || lutAddress % 8 != 0) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count / 2));
    if (!address) return std::nullopt;
    // Two pixels per byte, the left pixel in the high nibble.
    for (std::size_t i = 0; i < count; i += 2) {
        m_bytes[*address + i / 2] = static_cast<std::uint8_t>(((pixels[i] & 0xF) << 4) | (pixels[i + 1] & 0xF));
    }
    return Texture{*address, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Lut16,
                   static_cast<std::uint16_t>(lutAddress / 8)};
}

std::optional<Texture> Vdp1Vram::uploadBankTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                   std::uint16_t bank)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!isValidTextureSize(width, height) || pixels.size() != count || bank % 256 != 0) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count));
    if (!address) return std::nullopt;
    std::memcpy(m_bytes.data() + *address, pixels.data(), count);
    return Texture{*address, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Bank256,
                   bank};
}

std::optional<Texture> Vdp1Vram::uploadRgbTexture(int width, int height, std::span<const ColorWord> pixels)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!isValidTextureSize(width, height) || pixels.size() != count) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count * 2));
    if (!address) return std::nullopt;
    for (std::size_t i = 0; i < count; ++i) write16(*address + static_cast<std::uint32_t>(i * 2), pixels[i]);
    return Texture{*address, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Rgb, 0};
}

void Vdp1Vram::clearCommands() noexcept
{
    m_commandCount = 0;
    write16(kCommandTableAddress, static_cast<std::uint16_t>(CommandType::End));
}

bool Vdp1Vram::pushCommand(Command command, const GouraudTable* gouraud) noexcept
{
    // Keep one slot for the terminating end command.
    if (m_commandCount + 1 >= kMaxCommands) return false;

    if (gouraud) {
        const std::uint32_t gouraudAddress = kGouraudTableAddress + m_commandCount * 8;
        for (std::uint32_t i = 0; i < 4; ++i) write16(gouraudAddress + i * 2, (*gouraud)[i]);
        command.grda = static_cast<std::uint16_t>(gouraudAddress / 8);
        command.pmod |= pmod::kGouraud;
    }

    static_assert(std::endian::native == std::endian::little, "VRAM image is assumed little-endian");
    std::memcpy(m_bytes.data() + kCommandTableAddress + m_commandCount * sizeof(Command), &command, sizeof(Command));
    ++m_commandCount;
    write16(kCommandTableAddress + m_commandCount * 32, static_cast<std::uint16_t>(CommandType::End));
    return true;
}

} // namespace gridfin::saturn
