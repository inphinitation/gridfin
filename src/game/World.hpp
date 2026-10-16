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

/// A stone pillar with a fire on top that lights its surroundings.
struct Brazier {
    static constexpr float kPillarHeight = 0.9f;
    glm::vec3 base{0.0f};

    [[nodiscard]] glm::vec3 flameBase() const noexcept { return base + glm::vec3(0.0f, kPillarHeight, 0.0f); }
    [[nodiscard]] glm::vec3 lightPosition() const noexcept { return flameBase() + glm::vec3(0.0f, 0.45f, 0.0f); }
};

/// A shallow pond: VDP1 water surface tiles over a VDP2 reflection layer.
struct Pond {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
    float surfaceY = 0.0f;

    /// Outline of the surface, for the VDP2 line window.
    [[nodiscard]] std::array<glm::vec3, 4> outline() const noexcept
    {
        return {glm::vec3(minX, surfaceY, minZ), glm::vec3(maxX, surfaceY, minZ), glm::vec3(maxX, surfaceY, maxZ),
                glm::vec3(minX, surfaceY, maxZ)};
    }
    /// The area whose objects show up in the reflection.
    [[nodiscard]] Aabb reflectedArea(float margin) const noexcept
    {
        return {{minX - margin, -1e6f, minZ - margin}, {maxX + margin, 1e6f, maxZ + margin}};
    }
};

/// The level: textured blocks standing on the infinite VDP2 ground plane.
class World {
public:
    static constexpr float kGroundHeight = 0.0f;
    /// Blocks are cut into quads of at most this size; each quad shows its texture once.
    static constexpr float kTileSize = 1.0f;
    /// The pond surface sorts behind everything standing in or around it.
    static constexpr float kWaterDepthBias = 500.0f;

    void buildTestLevel();

    [[nodiscard]] const std::vector<Block>& blocks() const noexcept { return m_blocks; }
    [[nodiscard]] const std::vector<Tree>& trees() const noexcept { return m_trees; }
    [[nodiscard]] const std::vector<Brazier>& braziers() const noexcept { return m_braziers; }
    [[nodiscard]] const Pond& pond() const noexcept { return m_pond; }

    /// Collision boxes: blocks plus tree trunks.
    [[nodiscard]] const std::vector<Aabb>& colliders() const noexcept { return m_colliders; }

    /// Highest walkable surface at (x, z) that is not above `maxY`.
    [[nodiscard]] float floorHeight(float x, float z, float maxY) const noexcept;

    /// Distance to the first collider or the ground along a ray, or `maxDistance` on a miss.
    [[nodiscard]] float raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance) const noexcept;

    /// Draws the level. With `region`, draws only what overlaps it and leaves out the water
    /// surface: that is the set of objects drawn mirrored for the pond's reflection.
    void draw(saturn::Geometry& geometry, const Assets& assets, const Aabb* region = nullptr) const;

private:
    void addBlock(glm::vec3 min, glm::vec3 max, BlockStyle style);
    void addTree(glm::vec3 base, float height);
    void addBrazier(glm::vec3 base);

    std::vector<Block> m_blocks;
    std::vector<Tree> m_trees;
    std::vector<Brazier> m_braziers;
    Pond m_pond;
    std::vector<Aabb> m_colliders;
};

} // namespace gridfin
