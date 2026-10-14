#pragma once

#include "saturn/Color.hpp"
#include "saturn/Vdp1.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Texture streaming between work RAM and VDP1 VRAM.
///
/// On a Saturn the textures of a level sit in the 2 MB work RAM (loaded from CD)
/// and only those on screen live in VDP1 VRAM. Each frame the CPU copies the ones
/// that became visible into VRAM with DMA, within whatever time the frame leaves.
///
/// TextureCache models that: registered textures count against a work RAM budget,
/// VDP1 VRAM's texture area is a cache with least-recently-used eviction, and each
/// frame may only transfer a limited number of bytes. A texture that isn't resident
/// yet is drawn as a flat polygon in its average colour until it arrives.
namespace gridfin::saturn {

using TextureId = std::uint16_t;
inline constexpr TextureId kNoTexture = 0xFFFF;

struct TextureCacheStats {
    std::uint32_t registered = 0;
    std::uint32_t resident = 0;
    std::uint32_t workRamBytes = 0;
    std::uint32_t vramBytesUsed = 0;
    std::uint32_t vramBytesTotal = 0;
    // Per frame
    std::uint32_t dmaBytes = 0;
    std::uint32_t uploads = 0;
    std::uint32_t evictions = 0;
    std::uint32_t misses = 0; ///< requests that had to fall back to a flat polygon
};

class TextureCache {
public:
    /// Work RAM set aside for texture data: the 1 MB low work RAM bank.
    static constexpr std::uint32_t kWorkRamBudget = 1024 * 1024;
    /// Default DMA budget per frame. An approximation of what fits in vertical blank.
    static constexpr std::uint32_t kDefaultDmaBytesPerFrame = 32 * 1024;

    /// Takes `poolBytes` of VDP1 VRAM's data region (0 = all that is left) as the cache.
    explicit TextureCache(Vdp1Vram& vram, std::uint32_t poolBytes = 0);

    // Registration (load time): stores the texture in work RAM ---------------
    /// 4bpp texture with its own 16-colour lookup table; index 0 is transparent.
    [[nodiscard]] std::optional<TextureId> addLutTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                         const Lut& lut);
    /// 8bpp texture coloured through a 256 colour CRAM bank. `bankColors` (index 0 = transparent)
    /// is only used to compute the fallback colour.
    [[nodiscard]] std::optional<TextureId> addBankTexture(int width, int height, std::span<const std::uint8_t> pixels,
                                                          std::uint16_t bank, std::span<const ColorWord> bankColors);
    [[nodiscard]] std::optional<TextureId> addRgbTexture(int width, int height, std::span<const ColorWord> pixels);

    // Per frame ----------------------------------------------------------------
    /// Starts a frame: resets the DMA budget and the per-frame statistics.
    void beginFrame();

    /// Returns the texture if it is in VRAM, transferring it first if the frame's
    /// DMA budget and the cache allow. Marks it as used this frame.
    [[nodiscard]] std::optional<Texture> acquire(TextureId id);

    /// Average colour, drawn while the texture is not resident.
    [[nodiscard]] ColorWord fallbackColor(TextureId id) const { return m_textures.at(id).fallback; }
    [[nodiscard]] bool isResident(TextureId id) const { return m_textures.at(id).resident; }

    /// Drops every texture from VRAM (for example when changing areas).
    void evictAll();

    [[nodiscard]] const TextureCacheStats& stats() const noexcept { return m_stats; }
    std::uint32_t dmaBytesPerFrame = kDefaultDmaBytesPerFrame;

private:
    struct Entry {
        std::vector<std::uint8_t> lut;    ///< packed lookup table, uploaded in front of the pixels (Lut16 only)
        std::vector<std::uint8_t> pixels; ///< VRAM format
        Texture texture;                  ///< address fields valid while resident
        ColorWord fallback = 0;
        std::uint32_t lastUsedFrame = 0;
        std::uint32_t blockAddress = 0;
        bool resident = false;
    };
    struct Block {
        std::uint32_t address;
        std::uint32_t size;
        TextureId owner; ///< kNoTexture when free
    };

    [[nodiscard]] std::optional<TextureId> add(Entry entry);
    [[nodiscard]] std::optional<std::uint32_t> allocate(std::uint32_t size);
    [[nodiscard]] bool evictOne();
    void release(TextureId id);
    [[nodiscard]] static std::uint32_t blockSize(const Entry& entry);

    Vdp1Vram& m_vram;
    std::vector<Entry> m_textures;
    std::vector<Block> m_blocks; ///< covers the whole pool, sorted by address
    std::uint32_t m_frame = 0;
    std::uint32_t m_dmaUsed = 0;
    TextureCacheStats m_stats;
};

} // namespace gridfin::saturn
