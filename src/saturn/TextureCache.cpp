#include "saturn/TextureCache.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace gridfin::saturn {

namespace {

[[nodiscard]] constexpr std::uint32_t align8(std::uint32_t value) noexcept
{
    return (value + 7u) & ~7u;
}

/// Averages the opaque colours of a texture into one RGB word.
template <typename ColorOf>
[[nodiscard]] ColorWord averageColor(std::size_t count, ColorOf colorOf)
{
    long r = 0, g = 0, b = 0, n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::optional<ColorWord> color = colorOf(i);
        if (!color) continue;
        const Rgb5 c = unpack(*color);
        r += c.r;
        g += c.g;
        b += c.b;
        ++n;
    }
    if (n == 0) return rgb555(0, 0, 0);
    return rgb555(static_cast<unsigned>(r / n), static_cast<unsigned>(g / n), static_cast<unsigned>(b / n));
}

} // namespace

TextureCache::TextureCache(Vdp1Vram& vram, std::uint32_t poolBytes) : m_vram(vram)
{
    const std::uint32_t available = Vdp1Vram::dataBytesTotal() - vram.dataBytesUsed();
    const std::uint32_t size = (poolBytes == 0 ? available : std::min(poolBytes, available)) & ~7u;
    const auto address = vram.allocate(size);
    if (!address) throw std::runtime_error("VDP1 VRAM has no room for the texture cache");
    m_blocks.push_back({*address, size, kNoTexture});
    m_stats.vramBytesTotal = size;
}

std::uint32_t TextureCache::blockSize(const Entry& entry)
{
    return align8(static_cast<std::uint32_t>(entry.lut.size()))
           + align8(static_cast<std::uint32_t>(entry.pixels.size()));
}

std::optional<TextureId> TextureCache::add(Entry entry)
{
    const auto bytes = static_cast<std::uint32_t>(entry.lut.size() + entry.pixels.size());
    if (m_textures.size() >= kNoTexture || m_stats.workRamBytes + bytes > kWorkRamBudget) return std::nullopt;
    if (blockSize(entry) > m_stats.vramBytesTotal) return std::nullopt; // could never be resident
    m_stats.workRamBytes += bytes;
    m_textures.push_back(std::move(entry));
    m_stats.registered = static_cast<std::uint32_t>(m_textures.size());
    return static_cast<TextureId>(m_textures.size() - 1);
}

std::optional<TextureId> TextureCache::addLutTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                     const Lut& lut)
{
    if (!isValidTextureSize(width, height) || pixels.size() != static_cast<std::size_t>(width * height)) {
        return std::nullopt;
    }
    Entry entry;
    entry.lut = packLut(lut);
    entry.pixels = packLut16(pixels);
    entry.texture = {0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Lut16, 0};
    entry.fallback = averageColor(pixels.size(), [&](std::size_t i) -> std::optional<ColorWord> {
        if (pixels[i] == 0) return std::nullopt;
        return lut[pixels[i] & 0xF];
    });
    return add(std::move(entry));
}

std::optional<TextureId> TextureCache::addBankTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                      std::uint16_t bank, std::span<const ColorWord> bankColors)
{
    if (!isValidTextureSize(width, height) || pixels.size() != static_cast<std::size_t>(width * height)
        || bank % 256 != 0) {
        return std::nullopt;
    }
    Entry entry;
    entry.pixels.assign(pixels.begin(), pixels.end());
    entry.texture = {0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Bank256,
                     bank};
    entry.fallback = averageColor(pixels.size(), [&](std::size_t i) -> std::optional<ColorWord> {
        if (pixels[i] == 0 || pixels[i] >= bankColors.size()) return std::nullopt;
        return bankColors[pixels[i]];
    });
    return add(std::move(entry));
}

std::optional<TextureId> TextureCache::addRgbTexture(int width, int height, std::span<const ColorWord> pixels)
{
    if (!isValidTextureSize(width, height) || pixels.size() != static_cast<std::size_t>(width * height)) {
        return std::nullopt;
    }
    Entry entry;
    entry.pixels = packRgb(pixels);
    entry.texture = {0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), ColorMode::Rgb, 0};
    entry.fallback = averageColor(pixels.size(), [&](std::size_t i) -> std::optional<ColorWord> {
        if (pixels[i] == kTransparentRgb) return std::nullopt;
        return pixels[i];
    });
    return add(std::move(entry));
}

void TextureCache::beginFrame()
{
    ++m_frame;
    m_dmaUsed = 0;
    m_stats.dmaBytes = 0;
    m_stats.uploads = 0;
    m_stats.evictions = 0;
    m_stats.misses = 0;
}

std::optional<std::uint32_t> TextureCache::allocate(std::uint32_t size)
{
    for (;;) {
        for (std::size_t i = 0; i < m_blocks.size(); ++i) {
            Block& block = m_blocks[i];
            if (block.owner != kNoTexture || block.size < size) continue;
            const std::uint32_t address = block.address;
            if (block.size > size) {
                m_blocks.insert(m_blocks.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                Block{address + size, block.size - size, kNoTexture});
                m_blocks[i].size = size;
            }
            return address;
        }
        if (!evictOne()) return std::nullopt;
    }
}

bool TextureCache::evictOne()
{
    // Least recently used texture that the current frame doesn't need.
    TextureId victim = kNoTexture;
    std::uint32_t oldest = std::numeric_limits<std::uint32_t>::max();
    for (std::size_t id = 0; id < m_textures.size(); ++id) {
        const Entry& entry = m_textures[id];
        if (entry.resident && entry.lastUsedFrame != m_frame && entry.lastUsedFrame < oldest) {
            oldest = entry.lastUsedFrame;
            victim = static_cast<TextureId>(id);
        }
    }
    if (victim == kNoTexture) return false;
    release(victim);
    ++m_stats.evictions;
    return true;
}

void TextureCache::release(TextureId id)
{
    Entry& entry = m_textures[id];
    entry.resident = false;
    --m_stats.resident;
    m_stats.vramBytesUsed -= blockSize(entry);

    const auto it = std::find_if(m_blocks.begin(), m_blocks.end(),
                                 [&](const Block& block) { return block.address == entry.blockAddress; });
    it->owner = kNoTexture;
    // Coalesce with free neighbours.
    auto index = static_cast<std::size_t>(it - m_blocks.begin());
    if (index + 1 < m_blocks.size() && m_blocks[index + 1].owner == kNoTexture) {
        m_blocks[index].size += m_blocks[index + 1].size;
        m_blocks.erase(m_blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1);
    }
    if (index > 0 && m_blocks[index - 1].owner == kNoTexture) {
        m_blocks[index - 1].size += m_blocks[index].size;
        m_blocks.erase(m_blocks.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void TextureCache::evictAll()
{
    for (std::size_t id = 0; id < m_textures.size(); ++id) {
        if (m_textures[id].resident) release(static_cast<TextureId>(id));
    }
}

std::optional<Texture> TextureCache::acquire(TextureId id)
{
    if (id >= m_textures.size()) return std::nullopt;
    Entry& entry = m_textures[id];
    entry.lastUsedFrame = m_frame;
    if (entry.resident) return entry.texture;

    const std::uint32_t bytes = static_cast<std::uint32_t>(entry.lut.size() + entry.pixels.size());
    if (m_dmaUsed + bytes > dmaBytesPerFrame) {
        ++m_stats.misses;
        return std::nullopt;
    }
    const std::uint32_t size = blockSize(entry);
    const auto address = allocate(size);
    if (!address) {
        ++m_stats.misses;
        return std::nullopt;
    }

    // Claim the block and "DMA" the data into VRAM: lookup table first, then the pixels.
    for (Block& block : m_blocks) {
        if (block.address == *address) block.owner = id;
    }
    std::uint32_t pixelAddress = *address;
    if (!entry.lut.empty()) {
        m_vram.writeData(*address, entry.lut);
        entry.texture.colr = static_cast<std::uint16_t>(*address / 8);
        pixelAddress += align8(static_cast<std::uint32_t>(entry.lut.size()));
    }
    m_vram.writeData(pixelAddress, entry.pixels);
    entry.texture.address = pixelAddress;
    entry.blockAddress = *address;
    entry.resident = true;

    m_dmaUsed += bytes;
    m_stats.dmaBytes += bytes;
    ++m_stats.uploads;
    ++m_stats.resident;
    m_stats.vramBytesUsed += size;
    return entry.texture;
}

} // namespace gridfin::saturn
