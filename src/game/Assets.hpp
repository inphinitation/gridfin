#pragma once

#include "saturn/TextureCache.hpp"
#include "saturn/Vdp2.hpp"

namespace gridfin {

/// All graphics, generated procedurally straight into Saturn formats:
/// 4bpp lookup-table textures in work RAM (streamed into VDP1 VRAM on use) and 8bpp bitmaps in VDP2 VRAM.
struct Assets {
    // VDP1 textures
    saturn::TextureId gridTile = saturn::kNoTexture;
    saturn::TextureId brick = saturn::kNoTexture;
    saturn::TextureId stone = saturn::kNoTexture;
    saturn::TextureId grassTop = saturn::kNoTexture;
    saturn::TextureId grassSide = saturn::kNoTexture;
    saturn::TextureId crate = saturn::kNoTexture;
    saturn::TextureId robotBody = saturn::kNoTexture;
    saturn::TextureId robotFace = saturn::kNoTexture;
    saturn::TextureId fin = saturn::kNoTexture;
    saturn::TextureId shadow = saturn::kNoTexture;
    saturn::TextureId tree = saturn::kNoTexture;

    // VDP2 bitmaps
    saturn::Bitmap ground;
    saturn::Bitmap clouds;

    /// Generates everything; returns false if work RAM or VDP2 VRAM ran out.
    [[nodiscard]] bool load(saturn::TextureCache& textures, saturn::Vdp2Vram& vdp2);
};

} // namespace gridfin
