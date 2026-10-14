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

/// Fraction of a bounding box that lies on screen (coordinates relative to the screen centre).
[[nodiscard]] double visibleFraction(int minX, int minY, int maxX, int maxY) noexcept
{
    constexpr int kLeft = -kScreenWidth / 2, kRight = kScreenWidth / 2 - 1;
    constexpr int kTop = -kScreenHeight / 2, kBottom = kScreenHeight / 2 - 1;
    const long width = maxX - minX + 1;
    const long height = maxY - minY + 1;
    const long visibleWidth = std::max(0, std::min(maxX, kRight) - std::max(minX, kLeft) + 1);
    const long visibleHeight = std::max(0, std::min(maxY, kBottom) - std::max(minY, kTop) + 1);
    return static_cast<double>(visibleWidth * visibleHeight) / static_cast<double>(width * height);
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
    double visible = 1.0;
    switch (type) {
    case CommandType::NormalSprite: {
        lines = textureHeight;
        lineLength = textureWidth;
        const Point a = corner(0);
        visible = visibleFraction(a.x, a.y, a.x + static_cast<int>(textureWidth) - 1,
                                  a.y + static_cast<int>(textureHeight) - 1);
        break;
    }
    case CommandType::ScaledSprite: {
        const Point a = corner(0);
        const Point c = corner(2);
        visible = visibleFraction(std::min(a.x, c.x), std::min(a.y, c.y), std::max(a.x, c.x), std::max(a.y, c.y));
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
        visible = visibleFraction(std::min({a.x, b.x, c.x, d.x}), std::min({a.y, b.y, c.y, d.y}),
                                  std::max({a.x, b.x, c.x, d.x}), std::max({a.y, b.y, c.y, d.y}));
        break;
    }
    default: return kCommandCycles;
    }

    const auto calc = static_cast<ColorCalc>(command.pmod & 3u);
    const bool readsFramebuffer = calc == ColorCalc::Shadow || calc == ColorCalc::HalfTransparent;
    // Pixels outside the clip window are stepped over but not written. The bounding box
    // overlap approximates how much of the primitive is on screen.
    const double pixels = static_cast<double>(lines) * static_cast<double>(lineLength);
    const double onScreen = pixels * visible;
    const double offScreen = pixels - onScreen;
    const double pixelCycles = onScreen * (readsFramebuffer ? kReadModifyWriteFactor : 1u)
                               + offScreen / static_cast<double>(kClippedPixelsPerCycle);
    const std::uint32_t gouraud = (command.pmod & pmod::kGouraud) ? kGouraudCycles : 0u;
    return kCommandCycles + gouraud + lines * kLineSetupCycles + static_cast<std::uint32_t>(pixelCycles);
}

} // namespace gridfin::saturn::timing
