#pragma once

#include "saturn/Geometry.hpp"
#include "saturn/Vdp1.hpp"
#include "saturn/Vdp2.hpp"

namespace gridfin::saturn {

/// Everything a frame needs from the emulated hardware: both VDPs' memory and
/// registers plus the CPU-side geometry stage. The GPU renderer consumes this.
struct Machine {
    Vdp1Vram vdp1;
    Vdp2Vram vdp2;
    Vdp2Registers vdp2Registers;
    Geometry geometry;
    View view;

    Machine()
    {
        // Reserve the line colour table at a fixed VDP2 address.
        vdp2Registers.lineColorAddress = vdp2.allocate(kScreenHeight * 2).value_or(0);
    }
};

} // namespace gridfin::saturn
