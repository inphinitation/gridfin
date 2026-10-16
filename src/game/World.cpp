#include "game/World.hpp"

#include <array>
#include <cmath>

namespace gridfin {

using saturn::Material;

namespace {

[[nodiscard]] int tileCount(float length)
{
    return std::max(1, static_cast<int>(std::ceil(length / World::kTileSize - 0.01f)));
}

/// Emits a face cut into tiles; `pick(row, rows)` chooses the texture per tile row.
template <typename PickTexture>
void drawFace(saturn::Geometry& geometry, const BoxFace& face, PickTexture pick)
{
    const int columns = tileCount(glm::length(face.u));
    const int rows = tileCount(glm::length(face.v));
    const glm::vec3 du = face.u / static_cast<float>(columns);
    const glm::vec3 dv = face.v / static_cast<float>(rows);
    Material material;
    for (int row = 0; row < rows; ++row) {
        material.texture = pick(row);
        for (int column = 0; column < columns; ++column) {
            const glm::vec3 a = face.a + du * static_cast<float>(column) + dv * static_cast<float>(row);
            geometry.quad({a, a + du, a + du + dv, a + dv}, material);
        }
    }
}

} // namespace

std::array<BoxFace, 6> boxFaces(const Aabb& box) noexcept
{
    const glm::vec3 lo = box.min;
    const glm::vec3 hi = box.max;
    const glm::vec3 size = hi - lo;
    return {{
        {{lo.x, hi.y, lo.z}, {size.x, 0, 0}, {0, 0, size.z}},   // top (+Y)
        {{lo.x, hi.y, hi.z}, {size.x, 0, 0}, {0, -size.y, 0}},  // +Z
        {{hi.x, hi.y, lo.z}, {-size.x, 0, 0}, {0, -size.y, 0}}, // -Z
        {{hi.x, hi.y, hi.z}, {0, 0, -size.z}, {0, -size.y, 0}}, // +X
        {{lo.x, hi.y, lo.z}, {0, 0, size.z}, {0, -size.y, 0}},  // -X
        {{lo.x, lo.y, hi.z}, {size.x, 0, 0}, {0, 0, -size.z}},  // bottom (-Y)
    }};
}

void World::addBlock(glm::vec3 min, glm::vec3 max, BlockStyle style)
{
    m_blocks.push_back({{min, max}, style});
    m_colliders.push_back({min, max});
}

void World::addTree(glm::vec3 base, float height)
{
    m_trees.push_back({base, height});
    m_colliders.push_back(Aabb::fromCenter(base + glm::vec3(0.0f, height * 0.5f, 0.0f), {0.25f, height * 0.5f, 0.25f}));
}

void World::addBrazier(glm::vec3 base)
{
    m_braziers.push_back({base});
    addBlock(base - glm::vec3(0.25f, 0.0f, 0.25f), base + glm::vec3(0.25f, Brazier::kPillarHeight, 0.25f),
             BlockStyle::Stone);
}

void World::buildTestLevel()
{
    m_blocks.clear();
    m_braziers.clear();
    m_trees.clear();
    m_colliders.clear();

    // The Gridfin monolith.
    addBlock({-2, 0, -16}, {2, 6, -12}, BlockStyle::Grid);

    // Stairs up to a grassy plateau.
    for (int step = 0; step < 6; ++step) {
        const float x = 4.0f + static_cast<float>(step);
        addBlock({x, 0, -4}, {x + 1, 0.3f * static_cast<float>(step + 1), -1}, BlockStyle::Stone);
    }
    addBlock({10, 0, -7}, {16, 1.8f, -1}, BlockStyle::Grass);

    // Floating platforms leading from the plateau to the monolith's roof.
    addBlock({10, 2.6f, -12}, {13, 3.1f, -9}, BlockStyle::Grid);
    addBlock({5, 3.6f, -15}, {8, 4.1f, -12}, BlockStyle::Grid);
    addBlock({2.5f, 4.8f, -15}, {4, 5.3f, -13}, BlockStyle::Grid);

    // Brick walls and a crate jumping route onto them.
    addBlock({-14, 0, -6}, {-6, 2.5f, -5}, BlockStyle::Brick);
    addBlock({-15, 0, -6}, {-14, 2.5f, 3}, BlockStyle::Brick);
    addBlock({-4, 0, -3}, {-3, 1, -2}, BlockStyle::Crate);
    addBlock({-5.5f, 0, -4.5f}, {-4.5f, 1, -3.5f}, BlockStyle::Crate);
    addBlock({-5.5f, 1, -4.5f}, {-4.5f, 2, -3.5f}, BlockStyle::Crate);
    addBlock({-2, 0, 4}, {-1, 1, 5}, BlockStyle::Crate);

    // A ring of stone pillars.
    for (int i = 0; i < 8; ++i) {
        const float angle = static_cast<float>(i) * kTau / 8.0f;
        const glm::vec3 center{std::cos(angle) * 22.0f, 0.0f, std::sin(angle) * 22.0f};
        addBlock(center - glm::vec3(0.5f, 0, 0.5f), center + glm::vec3(0.5f, 4.0f, 0.5f), BlockStyle::Stone);
    }

    // Braziers: their fires light the area around them (best seen at dusk).
    addBrazier({3.0f, 0.0f, 2.0f});
    addBrazier({-3.2f, 0.0f, -11.0f});
    addBrazier({14.5f, 1.8f, -2.5f});
    addBrazier({-7.0f, 0.0f, -3.5f});

    // Trees scattered outside the play area.
    for (int i = 0; i < 28; ++i) {
        const float angle = static_cast<float>(i) * 2.399963f; // golden angle
        const float radius = 16.0f + std::fmod(static_cast<float>(i) * 7.31f, 22.0f);
        addTree({std::cos(angle) * radius, kGroundHeight, std::sin(angle) * radius},
                3.5f + std::fmod(static_cast<float>(i) * 1.37f, 1.5f));
    }
}

float World::floorHeight(float x, float z, float maxY) const noexcept
{
    float best = kGroundHeight;
    for (const Block& block : m_blocks) {
        const Aabb& b = block.box;
        if (x > b.min.x && x < b.max.x && z > b.min.z && z < b.max.z && b.max.y <= maxY + 1e-3f) {
            best = std::max(best, b.max.y);
        }
    }
    return best;
}

float World::raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance) const noexcept
{
    float best = maxDistance;
    if (dir.y < -1e-6f) best = std::min(best, (kGroundHeight - origin.y) / dir.y);
    for (const Aabb& collider : m_colliders) {
        const float t = gridfin::raycast(collider, origin, dir, best);
        if (t >= 0.0f) best = std::min(best, t);
    }
    return std::max(best, 0.0f);
}

void World::draw(saturn::Geometry& geometry, const Assets& assets) const
{
    for (const Block& block : m_blocks) {
        const auto faces = boxFaces(block.box);
        for (std::size_t f = 0; f < faces.size(); ++f) {
            const bool top = f == kFaceTop;
            const bool bottom = f == kFaceBottom;
            if (bottom && block.box.min.y <= kGroundHeight) continue; // never visible

            switch (block.style) {
            case BlockStyle::Grid: drawFace(geometry, faces[f], [&](int) { return assets.gridTile; }); break;
            case BlockStyle::Stone: drawFace(geometry, faces[f], [&](int) { return assets.stone; }); break;
            case BlockStyle::Crate: drawFace(geometry, faces[f], [&](int) { return assets.crate; }); break;
            case BlockStyle::Brick:
                drawFace(geometry, faces[f], [&](int) { return top ? assets.stone : assets.brick; });
                break;
            case BlockStyle::Grass:
                drawFace(geometry, faces[f], [&](int row) {
                    if (top) return assets.grassTop;
                    return row == 0 && !bottom ? assets.grassSide : assets.stone;
                });
                break;
            }
        }
    }

    Material treeMaterial;
    treeMaterial.texture = assets.tree;
    for (const Tree& tree : m_trees) geometry.billboard(tree.base, {tree.height * 0.5f, tree.height}, treeMaterial);

    // Fire is its own light source: unlit, so it stays bright at night.
    Material fireMaterial;
    fireMaterial.texture = assets.fire;
    fireMaterial.lit = false;
    fireMaterial.depthBias = -0.3f; // in front of the pillar top it stands on
    for (const Brazier& brazier : m_braziers) geometry.billboard(brazier.flameBase(), {0.55f, 0.8f}, fireMaterial);
}

} // namespace gridfin
