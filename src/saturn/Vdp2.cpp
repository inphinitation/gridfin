#include "saturn/Vdp2.hpp"

#include <algorithm>
#include <cstring>

namespace gridfin::saturn {

Vdp2Vram::Vdp2Vram() : m_bytes(kVdp2VramSize, 0) {}

std::optional<std::uint32_t> Vdp2Vram::allocate(std::uint32_t bytes)
{
    const std::uint32_t rounded = (bytes + 31u) & ~31u;
    if (rounded > kVdp2VramSize - m_top) return std::nullopt;
    const std::uint32_t address = m_top;
    m_top += rounded;
    vramDirty = true;
    return address;
}

std::optional<Bitmap> Vdp2Vram::uploadBitmap(int width, int height, std::span<const std::uint8_t> pixels,
                                             std::uint16_t bank)
{
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const bool validSize = (width == 512 || width == 1024) && (height == 256 || height == 512);
    if (!validSize || pixels.size() != count || bank % 256 != 0) return std::nullopt;
    const auto address = allocate(static_cast<std::uint32_t>(count));
    if (!address) return std::nullopt;
    std::memcpy(m_bytes.data() + *address, pixels.data(), count);
    return Bitmap{*address, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), bank};
}

void Vdp2Vram::setCram(std::uint16_t index, std::span<const ColorWord> colors)
{
    const std::size_t count = std::min(colors.size(), m_cram.size() - index);
    std::copy_n(colors.begin(), count, m_cram.begin() + index);
    cramDirty = true;
}

void Vdp2Vram::write16(std::uint32_t address, std::uint16_t value) noexcept
{
    m_bytes[address] = static_cast<std::uint8_t>(value & 0xFF);
    m_bytes[address + 1] = static_cast<std::uint8_t>(value >> 8);
}

} // namespace gridfin::saturn
