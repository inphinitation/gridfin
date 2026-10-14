#pragma once

#include "core/Math.hpp"
#include "game/Assets.hpp"
#include "saturn/Geometry.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace gridfin {

enum class BlockStyle { Grid, Brick, Stone, Grass, Crate };

struct Block {
    Aabb box;
    BlockStyle style = BlockStyle::Grid;
};

/// One side of a box with corners ordered for VDP1: `a` is the top-left corner
/// seen from outside, `u` points to the top-right and `v` to the bottom-left corner.
struct BoxFace {
    glm::vec3 a;
    glm::vec3 u;
    glm::vec3 v;
};

enum BoxFaceIndex : std::size_t { kFaceTop, kFacePosZ, kFaceNegZ, kFacePosX, kFaceNegX, kFaceBottom };

[[nodiscard]] std::array<BoxFace, 6> boxFaces(const Aabb& box) noexcept;

struct Tree {
    glm::vec3 base{0.0f};
    float height = 4.0f;
};

/// The level: textured blocks standing on the infinite VDP2 ground plane.
class World {
public:
    static constexpr float kGroundHeight = 0.0f;
    /// Blocks are cut into quads of at most this size; each quad shows its texture once.
    static constexpr float kTileSize = 1.0f;

    void buildTestLevel();

    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return m_blocks; }
    [[nodiscard]] const std::vector<Tree>& trees() const noexcept { return m_trees; }

    /// Collision boxes: blocks plus tree trunks.
    [[nodiscard]] const std::vector<Aabb>& colliders() const noexcept { return m_colliders; }

    /// Highest walkable surface at (x, z) that is not above `maxY`.
    [[nodiscard]] float floorHeight(float x, float z, float maxY) const noexcept;

    /// Distance to the first collider or the ground along a ray, or `maxDistance` on a miss.
    [[nodiscard]] float raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance) const noexcept;

    void draw(saturn::Geometry& geometry, const Assets& assets) const;

private:
    void addBlock(glm::vec3 min, glm::vec3 max, BlockStyle style);
    void addTree(glm::vec3 base, float height);

    std::vector<Block> m_blocks;
    std::vector<Tree> m_trees;
    std::vector<Aabb> m_colliders;
};

} // namespace gridfin
