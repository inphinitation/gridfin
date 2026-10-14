#include "saturn/Vdp1Timing.hpp"

#include <algorithm>
#include <cstdlib>

namespace gridfin::saturn::timing {

namespace {

struct Point {
    int x;
    int y;
};

/// VDP1 steps along the longer axis of an edge, one pixel per step.
[[nodiscard]] std::uint32_t steps(Point a, Point b) noexcept
{
    return static_cast<std::uint32_t>(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y))) + 1;
}

} // namespace

std::uint32_t commandCycles(const Command& command) noexcept
{
    const auto type = static_cast<CommandType>(command.ctrl & 0xF);
    const std::uint32_t textureWidth = ((command.size >> 8) & 0x3Fu) * 8u;
    const std::uint32_t textureHeight = command.size & 0xFFu;
    const auto corner = [&](int i) {
        return Point{command.xy[static_cast<std::size_t>(i * 2)], command.xy[static_cast<std::size_t>(i * 2 + 1)]};
    };

    std::uint32_t lines = 0;
    std::uint32_t lineLength = 0;
    switch (type) {
    case CommandType::NormalSprite:
        lines = textureHeight;
        lineLength = textureWidth;
        break;
    case CommandType::ScaledSprite: {
        const Point a = corner(0);
        const Point c = corner(2);
        // Shrinking still walks every texture row (no high-speed shrink).
        lines = std::max(static_cast<std::uint32_t>(std::abs(c.y - a.y)) + 1, textureHeight);
        lineLength = static_cast<std::uint32_t>(std::abs(c.x - a.x)) + 1;
        break;
    }
    case CommandType::DistortedSprite:
    case CommandType::Polygon: {
        const Point a = corner(0), b = corner(1), c = corner(2), d = corner(3);
        lines = std::max(steps(a, d), steps(b, c));
        if (type == CommandType::DistortedSprite) lines = std::max(lines, textureHeight);
        lineLength = (steps(a, b) + steps(d, c)) / 2;
        break;
    }
    default: return kCommandCycles;
    }

    const auto calc = static_cast<ColorCalc>(command.pmod & 3u);
    const bool readsFramebuffer = calc == ColorCalc::Shadow || calc == ColorCalc::HalfTransparent;
    const std::uint32_t pixelCycles = lines * lineLength * (readsFramebuffer ? kReadModifyWriteFactor : 1u);
    const std::uint32_t gouraud = (command.pmod & pmod::kGouraud) ? kGouraudCycles : 0u;
    return kCommandCycles + gouraud + lines * kLineSetupCycles + pixelCycles;
}

} // namespace gridfin::saturn::timing
