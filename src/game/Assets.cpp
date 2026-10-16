#include "game/Assets.hpp"

#include "core/Log.hpp"
#include "core/Math.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace gridfin {

using namespace saturn;

namespace {

// --- Procedural helpers -----------------------------------------------------

[[nodiscard]] std::uint32_t hash(int x, int y, std::uint32_t seed) noexcept
{
    std::uint32_t h =
        static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^ seed * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

[[nodiscard]] float random01(int x, int y, std::uint32_t seed) noexcept
{
    return static_cast<float>(hash(x, y, seed) & 0xFFFFu) / 65535.0f;
}

[[nodiscard]] int wrap(int value, int period) noexcept
{
    return ((value % period) + period) % period;
}

/// Smooth value noise that tiles every `period` lattice cells.
[[nodiscard]] float valueNoise(float x, float y, int period, std::uint32_t seed) noexcept
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const auto at = [&](int ix, int iy) { return random01(wrap(ix, period), wrap(iy, period), seed); };
    const float top = glm::mix(at(x0, y0), at(x0 + 1, y0), sx);
    const float bottom = glm::mix(at(x0, y0 + 1), at(x0 + 1, y0 + 1), sx);
    return glm::mix(top, bottom, sy);
}

/// Fractal noise in 0..1 that tiles over `size` pixels.
[[nodiscard]] float fbm(int px, int py, int size, int baseCells, int octaves, std::uint32_t seed) noexcept
{
    float sum = 0.0f, amplitude = 0.5f, norm = 0.0f;
    int cells = baseCells;
    for (int octave = 0; octave < octaves; ++octave) {
        const float scale = static_cast<float>(cells) / static_cast<float>(size);
        sum += amplitude
               * valueNoise(static_cast<float>(px) * scale, static_cast<float>(py) * scale, cells,
                            seed + static_cast<std::uint32_t>(octave) * 101u);
        norm += amplitude;
        amplitude *= 0.5f;
        cells *= 2;
    }
    return sum / norm;
}

/// 4x4 ordered dither threshold in 0..1, the way many Saturn artists shaded.
[[nodiscard]] float bayer(int x, int y) noexcept
{
    static constexpr std::array<int, 16> kMatrix{0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    return (static_cast<float>(kMatrix[static_cast<std::size_t>((y & 3) * 4 + (x & 3))]) + 0.5f) / 16.0f;
}

/// Picks one of `count` consecutive palette indices for a 0..1 shade, dithered.
[[nodiscard]] std::uint8_t ramp(float shade, int x, int y, std::uint8_t first, int count) noexcept
{
    const float scaled = std::clamp(shade, 0.0f, 0.999f) * static_cast<float>(count - 1);
    const int base = static_cast<int>(scaled);
    const int index = (scaled - static_cast<float>(base)) > bayer(x, y) ? base + 1 : base;
    return static_cast<std::uint8_t>(first + std::min(index, count - 1));
}

struct Image {
    int width;
    int height;
    std::vector<std::uint8_t> pixels;

    Image(int w, int h) : width(w), height(h), pixels(static_cast<std::size_t>(w * h), 0) {}

    void fill(const std::function<std::uint8_t(int, int)>& shader)
    {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) pixels[static_cast<std::size_t>(y * width + x)] = shader(x, y);
    }
};

template <std::size_t N>
[[nodiscard]] Lut makeLut(const std::array<std::uint32_t, N>& hexColors)
{
    static_assert(N <= 15, "index 0 is reserved for transparency");
    Lut lut{};
    for (std::size_t i = 0; i < N; ++i) lut[i + 1] = rgbHex(hexColors[i]);
    return lut;
}

class Loader {
public:
    Loader(TextureCache& textures, Vdp2Vram& vdp2) : m_textures(textures), m_vdp2(vdp2) {}

    template <std::size_t N>
    TextureId texture(const char* name, const std::array<std::uint32_t, N>& colors, Image image)
    {
        return texture(name, makeLut(colors), std::move(image));
    }

    TextureId texture(const char* name, const Lut& lut, Image image)
    {
        const auto id = m_textures.addLutTexture(image.width, image.height, image.pixels, lut);
        if (!id) {
            log::error("Work RAM texture budget exhausted while loading texture '{}'", name);
            m_ok = false;
            return kNoTexture;
        }
        return *id;
    }

    template <std::size_t N>
    Bitmap bitmap(const char* name, std::uint16_t bank, const std::array<std::uint32_t, N>& colors, Image image)
    {
        std::array<ColorWord, N + 1> palette{};
        for (std::size_t i = 0; i < N; ++i) palette[i + 1] = rgbHex(colors[i]);
        m_vdp2.setCram(bank, palette);
        const auto bitmap = m_vdp2.uploadBitmap(image.width, image.height, image.pixels, bank);
        if (!bitmap) {
            log::error("VDP2 VRAM exhausted while loading bitmap '{}'", name);
            m_ok = false;
            return {};
        }
        return *bitmap;
    }

    [[nodiscard]] bool ok() const noexcept { return m_ok; }

private:
    TextureCache& m_textures;
    Vdp2Vram& m_vdp2;
    bool m_ok = true;
};

// --- VDP1 textures ----------------------------------------------------------

Image gridTileImage()
{
    Image image(32, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        if (x == 0 && y == 0) return 5;
        if (x == 0 || y == 0) return 4;
        if (x == 16 || y == 16) return 6;
        const float shade = 0.35f + 0.4f * fbm(x, y, 32, 4, 2, 7) + 0.25f * (1.0f - static_cast<float>(x + y) / 62.0f);
        return ramp(shade, x, y, 1, 3);
    });
    return image;
}

Image brickImage()
{
    Image image(32, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        const int row = y / 8;
        const int offset = (row % 2) * 8;
        const int column = ((x + offset) / 16) % 2;
        if (y % 8 == 7 || (x + offset) % 16 == 15) return 1;
        if (y % 8 == 0) return 5;
        const float shade = 0.25f * random01(column, row, 3) + 0.55f * fbm(x, y, 32, 8, 2, 11) + 0.1f;
        return ramp(shade, x, y, 2, 3);
    });
    return image;
}

Image stoneImage()
{
    // Tileable Voronoi cobbles.
    std::array<glm::vec2, 6> seeds{};
    for (std::size_t i = 0; i < seeds.size(); ++i) {
        seeds[i] = {random01(static_cast<int>(i), 0, 21) * 32.0f, random01(static_cast<int>(i), 1, 21) * 32.0f};
    }
    Image image(32, 32);
    image.fill([&](int x, int y) -> std::uint8_t {
        float best = 1e9f, second = 1e9f;
        std::size_t bestIndex = 0;
        for (std::size_t i = 0; i < seeds.size(); ++i) {
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const glm::vec2 p =
                        seeds[i] + glm::vec2(static_cast<float>(ox) * 32.0f, static_cast<float>(oy) * 32.0f);
                    const float d =
                        glm::distance(p, glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
                    if (d < best) {
                        second = best;
                        best = d;
                        bestIndex = i;
                    } else if (d < second) {
                        second = d;
                    }
                }
            }
        }
        if (second - best < 1.4f) return 5;
        const float shade = 0.2f + 0.5f * random01(static_cast<int>(bestIndex), 9, 5) + 0.3f * fbm(x, y, 32, 8, 1, 13);
        return ramp(shade, x, y, 1, 4);
    });
    return image;
}

Image grassTopImage()
{
    Image image(32, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        if (random01(x, y, 41) > 0.97f) return 6;
        const float shade = 0.15f + 0.75f * fbm(x, y, 32, 4, 3, 17);
        return ramp(shade, x, y, 1, 5);
    });
    return image;
}

Image grassSideImage()
{
    Image image(32, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        const int fringe = 5 + static_cast<int>(random01(x, 0, 19) * 4.0f);
        if (y < fringe) return ramp(0.4f + 0.5f * random01(x, y, 23), x, y, 1, 3);
        if (y == fringe) return 4;
        if (random01(x, y, 29) > 0.95f) return 8;
        const float shade = 0.2f + 0.6f * fbm(x, y, 32, 4, 2, 31);
        return ramp(shade, x, y, 5, 3);
    });
    return image;
}

Image crateImage()
{
    Image image(32, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        if (x < 3 || y < 3 || x > 28 || y > 28) return (x == 1 || y == 1) ? 4 : 5;
        if (std::abs(x - y) < 2 || std::abs(x + y - 31) < 2) return 4;
        if (y % 7 == 0) return 1;
        const float shade = 0.2f + 0.6f * fbm(x / 2, y * 3, 32, 4, 2, 37);
        return ramp(shade, x, y, 1, 3);
    });
    return image;
}

Image robotBodyImage()
{
    Image image(16, 16);
    image.fill([](int x, int y) -> std::uint8_t {
        if ((x == 2 || x == 13) && (y == 2 || y == 13)) return 5;
        if (x == 0 || y == 0) return 3;
        if (x == 15 || y == 15) return 4;
        if (y == 8) return 4;
        return ramp(0.8f - static_cast<float>(x + y) / 40.0f, x, y, 1, 3);
    });
    return image;
}

Image robotFaceImage()
{
    Image image(16, 16);
    image.fill([](int x, int y) -> std::uint8_t {
        if (y >= 5 && y <= 10 && x >= 2 && x <= 13) {
            const bool eye = (y == 7 || y == 8) && ((x >= 4 && x <= 5) || (x >= 10 && x <= 11));
            return eye ? 7 : 6;
        }
        if (x == 0 || y == 0) return 3;
        if (x == 15 || y == 15) return 4;
        return ramp(0.8f - static_cast<float>(x + y) / 40.0f, x, y, 1, 3);
    });
    return image;
}

Image finImage()
{
    Image image(16, 16);
    image.fill([](int x, int y) -> std::uint8_t {
        if (y % 5 == 4) return 3;
        return ramp(1.0f - static_cast<float>(y) / 16.0f + 0.1f * random01(x, y, 43), x, y, 1, 2);
    });
    return image;
}

Image shadowImage()
{
    Image image(16, 16);
    image.fill([](int x, int y) -> std::uint8_t {
        const float dx = static_cast<float>(x) - 7.5f;
        const float dy = static_cast<float>(y) - 7.5f;
        return dx * dx + dy * dy < 7.6f * 7.6f ? 1 : 0;
    });
    return image;
}

Image treeImage()
{
    struct Blob {
        float x, y, r;
    };
    static constexpr std::array<Blob, 5> kBlobs{
        {{16, 9, 8}, {9, 21, 8.5f}, {23, 21, 8.5f}, {16, 28, 9.5f}, {16, 18, 9}}};
    Image image(32, 64);
    image.fill([](int x, int y) -> std::uint8_t {
        const float fx = static_cast<float>(x) + 0.5f;
        const float fy = static_cast<float>(y) + 0.5f;
        for (const Blob& blob : kBlobs) {
            const float dx = fx - blob.x;
            const float dy = fy - blob.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < blob.r * blob.r) {
                const float light = 0.55f - (dx + dy) / (2.5f * blob.r) + 0.25f * random01(x, y, 47);
                return ramp(light, x, y, 1, 5);
            }
        }
        if (x >= 13 && x <= 18 && y >= 30) return (x <= 14 || y % 6 == 0) ? 7 : 6;
        if (y >= 61 && x >= 10 && x <= 21) return 8;
        return 0;
    });
    return image;
}

// Fire uses two groups of 7 palette entries: indices 1-7 for the hot core, 8-14 for the
// outer flame. Each pixel picks an entry by its height and some noise, so rotating the
// colours within a group makes bands of light climb up the flame.
constexpr int kFireBand = 7;

Image fireImage()
{
    Image image(16, 32);
    image.fill([](int x, int y) -> std::uint8_t {
        const float fx = (static_cast<float>(x) + 0.5f - 8.0f) / 8.0f;
        const float fy = static_cast<float>(y) / 31.0f; // 0 = top, 1 = bottom
        // A teardrop that widens towards the bottom, with a ragged edge.
        const float width = 0.15f + 0.85f * std::sqrt(fy) - 0.25f * fy * fy;
        const float edge = width * (0.8f + 0.35f * fbm(x, y, 16, 4, 2, 83));
        const float distance = std::abs(fx) / std::max(edge, 1e-3f);
        if (distance > 1.0f || fy < 0.05f) return 0;
        const int band = (31 - y) / 3 + static_cast<int>(random01(x, y / 2, 89) * 2.0f);
        const bool core = distance < 0.55f && fy > 0.35f;
        return static_cast<std::uint8_t>((core ? 1 : 1 + kFireBand) + band % kFireBand);
    });
    return image;
}

// --- VDP2 bitmaps -------------------------------------------------------------

Image groundImage()
{
    Image image(512, 512);
    image.fill([](int x, int y) -> std::uint8_t {
        if (random01(x, y, 53) > 0.996f) return (hash(x, y, 59) & 1u) ? 9 : 10;
        const bool light = ((x / 64) + (y / 64)) % 2 == 0;
        const float shade = 0.15f + 0.7f * fbm(x, y, 512, 16, 3, 61);
        return ramp(shade, x, y, light ? 4 : 1, 4);
    });
    return image;
}

Image cloudImage()
{
    Image image(512, 256);
    // Mountain ridges along the bottom (the horizon) and clouds above.
    std::array<int, 512> farRidge{};
    std::array<int, 512> nearRidge{};
    for (int x = 0; x < 512; ++x) {
        farRidge[static_cast<std::size_t>(x)] = 18 + static_cast<int>(fbm(x, 0, 512, 6, 4, 71) * 40.0f);
        nearRidge[static_cast<std::size_t>(x)] = 4 + static_cast<int>(fbm(x, 0, 512, 10, 4, 73) * 28.0f);
    }
    image.fill([&](int x, int y) -> std::uint8_t {
        const int height = 255 - y;
        const auto column = static_cast<std::size_t>(x);
        const int nearTop = nearRidge[column];
        if (height < nearTop) {
            const bool sunny = nearRidge[static_cast<std::size_t>(wrap(x - 1, 512))] < nearTop;
            return sunny ? 5 : 4;
        }
        const int farTop = farRidge[column];
        if (height < farTop) return height > farTop - 4 && farTop > 42 ? 6 : 7;

        const float band = std::clamp(1.0f - std::abs(static_cast<float>(y) - 175.0f) / 70.0f, 0.0f, 1.0f);
        const float density = fbm(x, y * 2, 512, 8, 4, 79) * band;
        if (density < 0.36f) return 0;
        const float underside = fbm(x, y * 2 + 6, 512, 8, 4, 79) * band;
        if (underside < 0.36f) return 3;
        return density > 0.45f ? 1 : 2;
    });
    return image;
}

} // namespace

Lut Assets::fireLut(float time)
{
    // Brightness waves for each band; the core runs white-yellow to orange, the flame orange to red.
    const int phase = static_cast<int>(time * 15.0f);
    Lut lut{};
    for (int i = 0; i < kFireBand; ++i) {
        const float wave = 0.5f + 0.5f * std::cos(kTau * static_cast<float>(i + phase) / kFireBand);
        const auto mix = [wave](std::uint32_t dark, std::uint32_t bright) {
            const auto channel = [&](int shift) {
                const float a = static_cast<float>((dark >> shift) & 0xFF);
                const float b = static_cast<float>((bright >> shift) & 0xFF);
                return static_cast<unsigned>(a + (b - a) * wave);
            };
            return rgb(channel(16), channel(8), channel(0));
        };
        lut[static_cast<std::size_t>(1 + i)] = mix(0xFF9020, 0xFFF0A0);
        lut[static_cast<std::size_t>(1 + kFireBand + i)] = mix(0x901808, 0xF06818);
    }
    return lut;
}

bool Assets::load(TextureCache& textures, Vdp2Vram& vdp2)
{
    Loader loader(textures, vdp2);

    gridTile =
        loader.texture("grid", std::array<std::uint32_t, 6>{0x1A3A6A, 0x22487E, 0x2C5794, 0x3CD0F0, 0xB0F8FF, 0x3070B0},
                       gridTileImage());
    brick = loader.texture("brick", std::array<std::uint32_t, 5>{0xB8B0A0, 0x6E2C22, 0x8C3A2A, 0xA2482F, 0xC46A48},
                           brickImage());
    stone = loader.texture("stone", std::array<std::uint32_t, 5>{0x5A5E66, 0x6E737C, 0x858A93, 0x9CA1AA, 0x383B42},
                           stoneImage());
    grassTop = loader.texture("grass",
                              std::array<std::uint32_t, 6>{0x24561F, 0x2E6B2A, 0x3C8433, 0x4E9C3C, 0x67B54A, 0xD8E070},
                              grassTopImage());
    grassSide = loader.texture(
        "grass side",
        std::array<std::uint32_t, 8>{0x2E6B2A, 0x3C8433, 0x4E9C3C, 0x24561F, 0x5A3D24, 0x6B4A2B, 0x7E5833, 0xA08060},
        grassSideImage());
    crate = loader.texture("crate", std::array<std::uint32_t, 5>{0x734A22, 0x8A5A2B, 0x9E6B35, 0xC08A44, 0x4E3216},
                           crateImage());
    robotBody = loader.texture(
        "robot body", std::array<std::uint32_t, 5>{0x135058, 0x1F7A80, 0x2A9AA0, 0x7FE0E0, 0xD0F0F0}, robotBodyImage());
    robotFace = loader.texture(
        "robot face",
        std::array<std::uint32_t, 7>{0x135058, 0x1F7A80, 0x2A9AA0, 0x7FE0E0, 0xD0F0F0, 0x101828, 0xFFD040},
        robotFaceImage());
    fin = loader.texture("fin", std::array<std::uint32_t, 3>{0xC05010, 0xFF9A40, 0x803008}, finImage());
    shadow = loader.texture("shadow", std::array<std::uint32_t, 1>{0x000000}, shadowImage());
    fire = loader.texture("fire", fireLut(0.0f), fireImage());
    tree = loader.texture(
        "tree",
        std::array<std::uint32_t, 8>{0x1E4A1C, 0x2A6426, 0x3A7E2E, 0x52983A, 0x74B44C, 0x5A3A1E, 0x6E4826, 0x2A3A20},
        treeImage());

    ground = loader.bitmap("ground", 256,
                           std::array<std::uint32_t, 10>{0x2F6A2B, 0x367530, 0x3F8236, 0x488C3C, 0x3C7E33, 0x468C3A,
                                                         0x509842, 0x5AA44A, 0xF0E070, 0xF8F8F0},
                           groundImage());
    clouds = loader.bitmap(
        "clouds", 512,
        std::array<std::uint32_t, 7>{0xF8F8F8, 0xD8E4F0, 0xB4C8DE, 0x5E7EA6, 0x7896BA, 0xE8F0F8, 0x9AB4D2},
        cloudImage());

    const TextureCacheStats& stats = textures.stats();
    log::info("Textures: {} in work RAM ({} bytes), VDP1 texture cache {} bytes; VDP2 VRAM: {} / {} bytes",
              stats.registered, stats.workRamBytes, stats.vramBytesTotal, vdp2.bytesUsed(), kVdp2VramSize);
    return loader.ok();
}

} // namespace gridfin
