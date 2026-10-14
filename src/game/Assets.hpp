#pragma once

#include "saturn/Vdp1.hpp"
#include "saturn/Vdp2.hpp"

namespace gridfin {

/// All graphics, generated procedurally straight into Saturn formats:
/// 4bpp lookup-table textures in VDP1 VRAM and 8bpp bitmaps in VDP2 VRAM.
struct Assets {
    // VDP1 textures
    saturn::Texture gridTile;
    saturn::Texture brick;
    saturn::Texture stone;
    saturn::Texture grassTop;
    saturn::Texture grassSide;
    saturn::Texture crate;
    saturn::Texture robotBody;
    saturn::Texture robotFace;
    saturn::Texture fin;
    saturn::Texture shadow;
    saturn::Texture tree;

    // VDP2 bitmaps
    saturn::Bitmap ground;
    saturn::Bitmap clouds;

    /// Generates everything; returns false if VRAM ran out.
    [[nodiscard]] bool load(saturn::Vdp1Vram& vdp1, saturn::Vdp2Vram& vdp2);
};

} // namespace gridfin
