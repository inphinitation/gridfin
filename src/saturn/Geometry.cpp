#include "saturn/Geometry.hpp"

#include "saturn/Vdp1Timing.hpp"

#include <cmath>

namespace gridfin::saturn {

namespace {

constexpr float kHalfWidth = kScreenWidth * 0.5f;
constexpr float kHalfHeight = kScreenHeight * 0.5f;

[[nodiscard]] bool fitsVdp1(float value) noexcept
{
    return value >= static_cast<float>(kVdp1CoordinateMin) && value <= static_cast<float>(kVdp1CoordinateMax);
}

[[nodiscard]] std::int16_t snap(float value) noexcept
{
    return static_cast<std::int16_t>(std::lround(value));
}

/// True when every point is beyond the same screen edge.
[[nodiscard]] bool offscreen(const std::array<glm::vec2, 4>& points) noexcept
{
    bool left = true, right = true, top = true, bottom = true;
    for (const glm::vec2& p : points) {
        left = left && p.x < -kHalfWidth;
        right = right && p.x > kHalfWidth;
        top = top && p.y < -kHalfHeight;
        bottom = bottom && p.y > kHalfHeight;
    }
    return left || right || top || bottom;
}

} // namespace

View View::fromCamera(const Camera& camera) noexcept
{
    View view;
    view.position = camera.position;
    view.yaw = camera.yaw;
    view.pitch = camera.pitch;
    const float cp = std::cos(camera.pitch);
    view.forward = {std::sin(camera.yaw) * cp, std::sin(camera.pitch), -std::cos(camera.yaw) * cp};
    view.right = {std::cos(camera.yaw), 0.0f, std::sin(camera.yaw)};
    view.up = glm::cross(view.right, view.forward);
    view.focalY = kHalfHeight / std::tan(camera.fovY * 0.5f);
    // Pixels are narrower than they are tall on a 4:3 display.
    view.focalX = view.focalY / kPixelAspect;
    view.nearZ = camera.nearZ;
    view.farZ = camera.farZ;
    return view;
}

float View::horizonRow() const noexcept
{
    return kHalfHeight + focalY * std::tan(pitch);
}

float GeometryStats::vdp1Load() const noexcept
{
    return static_cast<float>(vdp1Cycles) / static_cast<float>(timing::kCyclesPerField);
}

std::uint32_t GeometryStats::fieldsNeeded() const noexcept
{
    return std::max<std::uint32_t>(1, (vdp1Cycles + timing::kCyclesPerField - 1) / timing::kCyclesPerField);
}

void Geometry::begin(const View& view, const Lighting& lighting, TextureCache* textures)
{
    m_view = view;
    m_lighting = lighting;
    m_textures = textures;
    if (m_textures) m_textures->beginFrame();
    m_stats = {};
    m_entries.clear();
    m_heads.fill(-1);
    m_tails.fill(-1);
}

std::optional<Texture> Geometry::resolve(const Material& material)
{
    if (material.texture == kNoTexture || !m_textures) return std::nullopt;
    const std::optional<Texture> texture = m_textures->acquire(material.texture);
    if (!texture) ++m_stats.textureMisses;
    return texture;
}

Command Geometry::baseCommand(CommandType type, const Material& material, const Texture* texture)
{
    Command command;
    command.ctrl = static_cast<std::uint16_t>(type);
    if (material.flipH) command.ctrl |= kFlipH;
    if (material.flipV) command.ctrl |= kFlipV;
    command.pmod = static_cast<std::uint16_t>(pmod::kEndCodeDisable | pmod::colorCalc(material.colorCalc));
    if (material.mesh) command.pmod |= pmod::kMesh;
    if (!texture) {
        command.colr = material.color;
    } else {
        command.pmod |= pmod::colorMode(texture->mode);
        command.colr = texture->colr;
        command.srca = texture->srca();
        command.size = texture->size();
    }
    return command;
}

GouraudTable Geometry::shade(const std::array<glm::vec3, 4>& worldCorners, const std::array<glm::vec3, 4>& viewCorners,
                             glm::vec3 normal, bool lit, bool sprite) const
{
    // Unlit materials (fire, effects) keep their texture colours; lit ones get the sun.
    glm::vec3 base{1.0f};
    if (lit) {
        base = glm::vec3(m_lighting.ambient
                         + m_lighting.diffuse * std::max(0.0f, glm::dot(normal, m_lighting.sunDirection)));
    }

    GouraudTable table{};
    const float hazeRange = std::max(m_lighting.hazeEnd - m_lighting.hazeStart, 1e-3f);
    for (std::size_t i = 0; i < 4; ++i) {
        glm::vec3 light = base;
        for (std::size_t l = 0; l < m_lighting.pointLightCount; ++l) {
            const PointLight& point = m_lighting.pointLights[l];
            const glm::vec3 toLight = point.position - worldCorners[i];
            const float distance = glm::length(toLight);
            if (distance >= point.radius) continue;
            const float falloff = 1.0f - distance / point.radius;
            // Surfaces facing away get nothing; sprites have no real normal and are lit from all sides.
            const float facing =
                sprite || distance < 1e-4f ? 1.0f : std::max(0.0f, glm::dot(normal, toLight / distance));
            light += point.color * (falloff * falloff * facing);
        }

        const float t = std::clamp((viewCorners[i].z - m_lighting.hazeStart) / hazeRange, 0.0f, 1.0f);
        const int haze = static_cast<int>(std::lround(t * static_cast<float>(m_lighting.hazeSteps)));
        const auto steps = [](float value) { return static_cast<int>(std::lround((value - 1.0f) * 16.0f)); };
        // Haze pulls colours towards the pale blue horizon.
        table[i] =
            gouraudOffset(steps(light.x) + haze, steps(light.y) + haze, steps(light.z) + haze + (haze > 0 ? 2 : 0));
    }
    return table;
}

void Geometry::insert(const Command& command, const GouraudTable* gouraud, float sortDepth)
{
    const float range = m_view.farZ - m_view.nearZ;
    const int bucket = std::clamp(static_cast<int>((sortDepth - m_view.nearZ) / range * kOrderingTableSize), 0,
                                  kOrderingTableSize - 1);
    const auto index = static_cast<std::int32_t>(m_entries.size());
    m_entries.push_back({command, gouraud ? *gouraud : GouraudTable{}, gouraud != nullptr, -1});
    const auto b = static_cast<std::size_t>(bucket);
    if (m_tails[b] < 0) {
        m_heads[b] = index;
    } else {
        m_entries[static_cast<std::size_t>(m_tails[b])].next = index;
    }
    m_tails[b] = index;
}

void Geometry::quad(const std::array<glm::vec3, 4>& corners, const Material& material)
{
    ++m_stats.submitted;

    std::array<glm::vec3, 4> viewCorners;
    float minZ = 1e30f, maxZ = -1e30f, sumZ = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        viewCorners[i] = m_view.toView(corners[i]);
        minZ = std::min(minZ, viewCorners[i].z);
        maxZ = std::max(maxZ, viewCorners[i].z);
        sumZ += viewCorners[i].z;
    }
    // No near plane clipping on a Saturn: polygons that cross it are dropped.
    if (minZ < m_view.nearZ) {
        ++m_stats.culledNear;
        return;
    }
    if (minZ > m_view.farZ) {
        ++m_stats.culledFar;
        return;
    }

    std::array<glm::vec2, 4> screen;
    for (std::size_t i = 0; i < 4; ++i) screen[i] = m_view.project(viewCorners[i]);

    if (!material.doubleSided) {
        float area = 0.0f;
        for (std::size_t i = 0; i < 4; ++i) {
            const glm::vec2 a = screen[i];
            const glm::vec2 b = screen[(i + 1) % 4];
            area += a.x * b.y - b.x * a.y;
        }
        if (area <= 0.0f) {
            ++m_stats.culledBackface;
            return;
        }
    }

    if (offscreen(screen)) {
        ++m_stats.culledOffscreen;
        return;
    }

    for (const glm::vec2& point : screen) {
        if (!fitsVdp1(point.x) || !fitsVdp1(point.y)) {
            ++m_stats.culledOffscreen;
            return;
        }
    }

    // Only now that the quad is known to be drawn is its texture streamed in. Until it
    // arrives the quad is drawn as a flat polygon in the texture's average colour.
    const std::optional<Texture> texture = resolve(material);
    Material flat = material;
    if (material.texture != kNoTexture && !texture && m_textures)
        flat.color = m_textures->fallbackColor(material.texture);
    Command command =
        baseCommand(texture ? CommandType::DistortedSprite : CommandType::Polygon, flat, texture ? &*texture : nullptr);
    for (std::size_t i = 0; i < 4; ++i) {
        command.xy[i * 2] = snap(screen[i].x);
        command.xy[i * 2 + 1] = snap(screen[i].y);
    }

    // Normal from the diagonals stays valid for quads with collapsed corners (triangles).
    const glm::vec3 cross = glm::cross(corners[3] - corners[1], corners[2] - corners[0]);
    const float length = glm::length(cross);
    const glm::vec3 normal = length > 1e-6f ? cross / length : glm::vec3(0.0f, 1.0f, 0.0f);
    const GouraudTable gouraud = shade(corners, viewCorners, normal, material.lit);

    float sortDepth = sumZ * 0.25f;
    if (material.sort == SortMode::Farthest) sortDepth = maxZ;
    if (material.sort == SortMode::Nearest) sortDepth = minZ;
    insert(command, &gouraud, sortDepth + material.depthBias);
}

void Geometry::billboard(glm::vec3 base, glm::vec2 size, const Material& material)
{
    ++m_stats.submitted;
    if (material.texture == kNoTexture) return;

    const glm::vec3 viewBase = m_view.toView(base);
    if (viewBase.z < std::max(m_view.nearZ, kBillboardCullDistance)) {
        ++m_stats.culledNear;
        return;
    }
    if (viewBase.z > m_view.farZ) {
        ++m_stats.culledFar;
        return;
    }

    const glm::vec2 anchor = m_view.project(viewBase);
    const float width = size.x * m_view.focalX / viewBase.z;
    const float height = size.y * m_view.focalY / viewBase.z;
    const glm::vec2 topLeft{anchor.x - width * 0.5f, anchor.y - height};
    const glm::vec2 bottomRight{anchor.x + width * 0.5f, anchor.y};

    const std::array<glm::vec2, 4> rect{topLeft, glm::vec2{bottomRight.x, topLeft.y}, bottomRight,
                                        glm::vec2{topLeft.x, bottomRight.y}};
    if (offscreen(rect)) {
        ++m_stats.culledOffscreen;
        return;
    }
    if (!fitsVdp1(topLeft.x) || !fitsVdp1(topLeft.y) || !fitsVdp1(bottomRight.x) || !fitsVdp1(bottomRight.y)) {
        ++m_stats.culledOffscreen;
        return;
    }

    // A sprite has no sensible flat fallback, so it simply pops in once its texture arrives.
    const std::optional<Texture> texture = resolve(material);
    if (!texture) return;
    Command command = baseCommand(CommandType::ScaledSprite, material, &*texture);
    if (viewBase.z < kBillboardMeshDistance) command.pmod |= pmod::kMesh;
    command.xy[0] = snap(topLeft.x);
    command.xy[1] = snap(topLeft.y);
    // Scaled sprites name their opposite corner inclusively.
    command.xy[4] = static_cast<std::int16_t>(snap(bottomRight.x) - 1);
    command.xy[5] = static_cast<std::int16_t>(snap(bottomRight.y) - 1);

    // A sprite is shaded as a whole, from its middle.
    const glm::vec3 centre = base + glm::vec3(0.0f, size.y * 0.5f, 0.0f);
    const std::array<glm::vec3, 4> world{centre, centre, centre, centre};
    const std::array<glm::vec3, 4> depth{viewBase, viewBase, viewBase, viewBase};
    const GouraudTable gouraud = shade(world, depth, glm::vec3(0.0f, 1.0f, 0.0f), material.lit, true);
    insert(command, &gouraud, viewBase.z + material.depthBias);
}

void Geometry::end(Vdp1Vram& vram)
{
    vram.clearCommands();
    for (int bucket = kOrderingTableSize - 1; bucket >= 0; --bucket) {
        for (std::int32_t i = m_heads[static_cast<std::size_t>(bucket)]; i >= 0;) {
            const Entry& entry = m_entries[static_cast<std::size_t>(i)];
            if (vram.pushCommand(entry.command, entry.useGouraud ? &entry.gouraud : nullptr)) {
                ++m_stats.drawn;
                Command timed = entry.command;
                if (entry.useGouraud) timed.pmod |= pmod::kGouraud;
                m_stats.vdp1Cycles += timing::commandCycles(timed);
            } else {
                ++m_stats.overflow;
            }
            i = entry.next;
        }
    }
}

} // namespace gridfin::saturn
