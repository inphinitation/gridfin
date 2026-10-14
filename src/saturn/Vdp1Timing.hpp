#pragma once

#include "saturn/Vdp1.hpp"

#include <cstdint>

/// An estimate of how long VDP1 needs to draw a command list.
///
/// VDP1 draws every primitive as a series of lines. For textured quads it walks
/// at least one line per texture row, however small the quad is on screen, and
/// every line costs its length in pixels. Colour calculation that reads the
/// framebuffer (half-transparency, shadow) roughly doubles the per-pixel cost.
/// When the list takes longer than a field, the frame is shown twice: the game
/// drops to 30 fps (or lower).
///
/// The constants are approximations chosen to produce realistic budgets, not
/// cycle-exact hardware timings.
namespace gridfin::saturn::timing {

/// VDP1 clock in the 320 pixel NTSC mode.
inline constexpr std::uint32_t kVdp1ClockHz = 28'636'360;
inline constexpr double kFieldRateHz = 59.94;
/// Cycles VDP1 can spend drawing in one field.
inline constexpr std::uint32_t kCyclesPerField = static_cast<std::uint32_t>(kVdp1ClockHz / kFieldRateHz);

inline constexpr std::uint32_t kCommandCycles = 40;        ///< fetching and decoding a 32-byte command
inline constexpr std::uint32_t kGouraudCycles = 8;         ///< fetching the gouraud table
inline constexpr std::uint32_t kLineSetupCycles = 4;       ///< per drawn line
inline constexpr std::uint32_t kReadModifyWriteFactor = 2; ///< shadow and half-transparency

/// Estimated cycles to draw one command.
[[nodiscard]] std::uint32_t commandCycles(const Command& command) noexcept;

} // namespace gridfin::saturn::timing
