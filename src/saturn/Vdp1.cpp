#include "saturn/Vdp1.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace gridfin::saturn {

bool isValidTextureSize(int width, int height) noexcept
{
    return width > 0 && height > 0 && width % 8 == 0 && width <= kMaxTextureWidth && height <= kMaxTextureHeight;
}

std::vector<std::uint8_t> packLut16(std::span<const std::uint8_t> indices)
{
    // Two pixels per byte, the left pixel in the high nibble.
    std::vector<std::uint8_t> packed((indices.size() + 1) / 2, 0);
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto nibble = static_cast<std::uint8_t>(indices[i] & 0xF);
        packed[i / 2] |= (i % 2 == 0) ? static_cast<std::uint8_t>(nibble << 4) : nibble;
    }
    return packed;
}

std::vector<std::uint8_t> packRgb(std::span<const ColorWord> pixels)
{
    std::vector<std::uint8_t> packed(pixels.size() * 2);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        packed[i * 2] = static_cast<std::uint8_t>(pixels[i] & 0xFF);
        packed[i * 2 + 1] = static_cast<std::uint8_t>(pixels[i] >> 8);
    }
    return packed;
}

std::vector<std::uint8_t> packLut(const Lut& lut)
{
    return packRgb(lut);
}

Vdp1Vram::Vdp1Vram() : m_bytes(kVdp1VramSize, 0)
{
    clearCommands();
}

void Vdp1Vram::writeData(std::uint32_t address, std::span<const std::uint8_t> data)
{
    if (data.empty()) return;
    std::memcpy(m_bytes.data() + address, data.data(), data.size());
    m_dirty.emplace_back(address, address + static_cast<std::uint32_t>(data.size()));
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> Vdp1Vram::takeDirtyRanges()
{
    std::sort(m_dirty.begin(), m_dirty.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
    for (const auto& range : m_dirty) {
        if (!merged.empty() && range.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, range.second);
        } else {
            merged.push_back(range);
        }
    }
    m_dirty.clear();
    return merged;
}

std::optional<std::uint32_t> Vdp1Vram::allocate(std::uint32_t bytes)
{
    const std::uint32_t rounded = (bytes + 7u) & ~7u;
    if (rounded > kVdp1VramSize - m_dataTop) return std::nullopt;
    const std::uint32_t address = m_dataTop;
    m_dataTop += rounded;
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
    writeData(*address, packLut(lut));
    return address;
}

std::optional<Texture> Vdp1Vram::uploadLutTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                  std::uint32_t lutAddress)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!isValidTextureSize(width, height) || pixels.size() != count || lutAddress % 8 != 0) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count / 2));
    if (!address) return std::nullopt;
    writeData(*address, packLut16(pixels));
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
    writeData(*address, pixels);
    return Texture{*address, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Bank256,
                   bank};
}

std::optional<Texture> Vdp1Vram::uploadRgbTexture(int width, int height, std::span<const ColorWord> pixels)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (!isValidTextureSize(width, height) || pixels.size() != count) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count * 2));
    if (!address) return std::nullopt;
    writeData(*address, packRgb(pixels));
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
