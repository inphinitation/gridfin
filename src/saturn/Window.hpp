#pragma once

#include "saturn/Geometry.hpp"
#include "saturn/Vdp2.hpp"

#include <array>
#include <span>

/// VDP2 line windows computed on the CPU.
///
/// A line window is a table with a horizontal span per scanline. Games filled it
/// each frame from the on-screen outline of a shape (a pond, a spotlight, a
/// cockpit) to switch layers on or off inside it.
namespace gridfin::saturn {

/// Rasterises a convex world-space polygon into a line window. Parts behind the near plane
/// are clipped away. Returns the window's bounding box relative to the screen centre
/// (empty if the polygon is not visible).
ScreenRect computeLineWindow(const View& view, std::span<const glm::vec3> polygon,
                             std::array<LineSpan, kScreenHeight>& window);

} // namespace gridfin::saturn
