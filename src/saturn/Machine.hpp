#pragma once

#include "saturn/Geometry.hpp"
#include "saturn/TextureCache.hpp"
#include "saturn/Vdp1.hpp"
#include "saturn/Vdp2.hpp"

namespace gridfin::saturn {

/// Everything a frame needs from the emulated hardware: both VDPs' memory and
/// registers, the texture streaming cache and the CPU-side geometry stage. The GPU renderer consumes this.
struct Machine {
    Vdp1Vram vdp1;
    /// Work RAM texture store streaming into VDP1 VRAM's texture area.
    TextureCache textures{vdp1};
    Vdp2Vram vdp2;
    Vdp2Registers vdp2Registers;
    Geometry geometry;
    View view;

    Machine()
    {
        // Reserve the per-line tables at fixed VDP2 addresses.
        vdp2Registers.lineColorAddress = vdp2.allocate(kScreenHeight * 2).value_or(0);
        vdp2Registers.windowTableAddress = vdp2.allocate(kScreenHeight * 4).value_or(0);
        vdp2Registers.reflectionTableAddress = vdp2.allocate(kScreenHeight * 4).value_or(0);
    }
};

} // namespace gridfin::saturn
