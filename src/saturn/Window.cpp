#include "saturn/Window.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gridfin::saturn {

ScreenRect computeLineWindow(const View& view, std::span<const glm::vec3> polygon,
                             std::array<LineSpan, kScreenHeight>& window)
{
    window.fill({});

    // Clip against the near plane in view space (Sutherland-Hodgman with one plane).
    std::vector<glm::vec3> clipped;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const glm::vec3 a = view.toView(polygon[i]);
        const glm::vec3 b = view.toView(polygon[(i + 1) % polygon.size()]);
        const bool aIn = a.z >= view.nearZ;
        const bool bIn = b.z >= view.nearZ;
        if (aIn) clipped.push_back(a);
        if (aIn != bIn) clipped.push_back(glm::mix(a, b, (view.nearZ - a.z) / (b.z - a.z)));
    }
    if (clipped.size() < 3) return {};

    // Project to screen pixels (origin top-left).
    constexpr float kCentreX = kScreenWidth * 0.5f;
    constexpr float kCentreY = kScreenHeight * 0.5f;
    std::vector<glm::vec2> points;
    points.reserve(clipped.size());
    for (const glm::vec3& point : clipped) points.push_back(view.project(point) + glm::vec2(kCentreX, kCentreY));

    ScreenRect bounds{kScreenWidth, kScreenHeight, -1, -1};
    for (int line = 0; line < kScreenHeight; ++line) {
        // Where the scanline through the pixel centres crosses the outline.
        const float y = static_cast<float>(line) + 0.5f;
        float left = 1e30f, right = -1e30f;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const glm::vec2 a = points[i];
            const glm::vec2 b = points[(i + 1) % points.size()];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
                const float x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        if (left > right) continue;

        // Pixels whose centres are inside.
        const int start = std::clamp(static_cast<int>(std::ceil(left - 0.5f)), 0, kScreenWidth);
        const int end = std::clamp(static_cast<int>(std::floor(right - 0.5f)) + 1, 0, kScreenWidth);
        if (start >= end) continue;
        window[static_cast<std::size_t>(line)] = {static_cast<std::int16_t>(start), static_cast<std::int16_t>(end)};
        bounds.minX = std::min(bounds.minX, start);
        bounds.maxX = std::max(bounds.maxX, end - 1);
        bounds.minY = std::min(bounds.minY, line);
        bounds.maxY = std::max(bounds.maxY, line);
    }
    if (bounds.empty()) return {};

    // Relative to the screen centre, like VDP1 coordinates.
    const int cx = kScreenWidth / 2;
    const int cy = kScreenHeight / 2;
    return {bounds.minX - cx, bounds.minY - cy, bounds.maxX - cx, bounds.maxY - cy};
}

} // namespace gridfin::saturn
