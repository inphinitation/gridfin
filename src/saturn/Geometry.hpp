#pragma once

#include "core/Math.hpp"
#include "saturn/Vdp1.hpp"

#include <array>
#include <cstdint>
#include <vector>

/// The geometry stage: what the SH-2 CPUs did on a Saturn.
///
/// World-space quads are transformed, culled, lit and projected on the CPU
/// into 2D VDP1 commands with integer vertex coordinates. Visibility comes
/// from a Z-sorted ordering table rather than a depth buffer, so the familiar
/// artefacts come for free: wobbling (snapped) vertices, affine/bilinear
/// texture warping, sort-order popping and polygons vanishing at the near plane.
namespace gridfin::saturn {

/// Camera description in world space (Y up).
struct Camera {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;   ///< radians, 0 looks down -Z, positive turns right
    float pitch = 0.0f; ///< radians, positive looks up
    float fovY = radians(60.0f);
    float nearZ = 0.1f;
    float farZ = 64.0f;
};

/// Derived view data shared by VDP1 projection and the VDP2 ground plane.
struct View {
    glm::vec3 position{0.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float focalX = 1.0f; ///< pixels per unit at depth 1
    float focalY = 1.0f;
    float nearZ = 0.1f;
    float farZ = 64.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;

    [[nodiscard]] static View fromCamera(const Camera& camera) noexcept;

    [[nodiscard]] glm::vec3 toView(glm::vec3 world) const noexcept
    {
        const glm::vec3 d = world - position;
        return {glm::dot(d, right), glm::dot(d, up), glm::dot(d, forward)};
    }

    /// Projects a view-space point to screen coordinates relative to the screen centre (Y down).
    [[nodiscard]] glm::vec2 project(glm::vec3 viewPoint) const noexcept
    {
        const float invZ = 1.0f / viewPoint.z;
        return {viewPoint.x * focalX * invZ, -viewPoint.y * focalY * invZ};
    }

    /// Screen row (from the top) where the horizon of the Y=const ground plane appears.
    [[nodiscard]] float horizonRow() const noexcept;
};

struct Lighting {
    glm::vec3 sunDirection = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f)); ///< towards the sun
    float ambient = 0.6f;
    float diffuse = 0.55f;
    /// Distance haze added through the gouraud table, in 5-bit colour steps.
    float hazeStart = 24.0f;
    float hazeEnd = 60.0f;
    int hazeSteps = 9;
};

enum class SortMode : std::uint8_t { Center, Farthest, Nearest };

struct Material {
    Texture texture{};                   ///< leave invalid for a flat polygon
    ColorWord color = rgb555(31, 0, 31); ///< flat polygon colour
    ColorCalc colorCalc = ColorCalc::Replace;
    bool mesh = false;
    bool doubleSided = false;
    bool lit = true;
    bool flipH = false;
    bool flipV = false;
    SortMode sort = SortMode::Center;
    /// Added to the sort depth; negative values draw later (in front).
    float depthBias = 0.0f;
};

struct GeometryStats {
    std::uint32_t submitted = 0;
    std::uint32_t drawn = 0;
    std::uint32_t culledBackface = 0;
    std::uint32_t culledNear = 0;
    std::uint32_t culledFar = 0;
    std::uint32_t culledOffscreen = 0;
    std::uint32_t overflow = 0;
};

class Geometry {
public:
    static constexpr int kOrderingTableSize = 1024;

    void begin(const View& view, const Lighting& lighting);

    /// Submits a quad. Corners A B C D map to the texture's
    /// top-left, top-right, bottom-right and bottom-left; the front face shows
    /// them clockwise on screen.
    void quad(const std::array<glm::vec3, 4>& corners, const Material& material);

    /// Camera facing scaled sprite whose bottom centre sits at `base`.
    void billboard(glm::vec3 base, glm::vec2 size, const Material& material);

    /// Sorts everything submitted since begin() back-to-front and writes the VDP1 command table.
    void end(Vdp1Vram& vram);

    [[nodiscard]] const GeometryStats& stats() const noexcept { return m_stats; }
    [[nodiscard]] const View& view() const noexcept { return m_view; }

private:
    struct Entry {
        Command command;
        GouraudTable gouraud;
        bool useGouraud;
        std::int32_t next;
    };

    [[nodiscard]] GouraudTable shade(const std::array<glm::vec3, 4>& viewCorners, glm::vec3 normal, bool lit) const;
    void insert(const Command& command, const GouraudTable* gouraud, float sortDepth);
    [[nodiscard]] static Command baseCommand(CommandType type, const Material& material);

    View m_view;
    Lighting m_lighting;
    GeometryStats m_stats;
    std::vector<Entry> m_entries;
    std::array<std::int32_t, kOrderingTableSize> m_heads{};
    std::array<std::int32_t, kOrderingTableSize> m_tails{};
};

} // namespace gridfin::saturn
