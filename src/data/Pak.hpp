#pragma once

#include "data/Vfs.hpp"

#include <filesystem>
#include <memory>
#include <string>

/// The packed game archive (`.pak`): a tree of folders and files in one image. Every folder's
/// entries are stored contiguously and sorted by name, so a lookup walks down from the root with
/// a binary search at each level, directly on the image. All integers are little-endian:
///
///     header   (32 bytes)
///       "GPAK", u32 version (1), u32 node count, u32 node table offset,
///       u32 name table offset, u32 name table size, u32 data offset, u32 reserved (0)
///     nodes    (16 bytes each; node 0 is the root folder)
///       u32 name offset (in the name table), u16 name length, u16 flags (bit 0: folder),
///       folder: u32 first child node, u32 child count
///       file:   u32 data offset, u32 size
///     a CRC-32 per file follows the node table: u32 for every node (0 for folders)
///     names    UTF-8, not terminated
///     data     the files, each aligned to 4 bytes
///
/// A folder's children always have higher node numbers than the folder, so the tree has no cycles.
namespace gridfin::data {

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data);

/// A mounted `.pak` image; reads come straight from it.
class PakSource final : public Source {
public:
    /// Checks the whole image (structure, names, order, checksums). Returns nullptr with a message on failure.
    [[nodiscard]] static std::unique_ptr<PakSource> open(Bytes image, std::string name, std::string* error = nullptr);

    [[nodiscard]] std::optional<Bytes> read(const std::string& path) const override;
    [[nodiscard]] std::vector<std::string> list() const override;
    [[nodiscard]] std::string describe() const override { return m_name; }

private:
    struct Node {
        std::uint32_t nameOffset;
        std::uint16_t nameLength;
        std::uint16_t flags;
        std::uint32_t a, b; ///< folder: first child, count; file: offset, size
    };
    PakSource(Bytes image, std::string name) : m_image(std::move(image)), m_name(std::move(name)) {}
    [[nodiscard]] Node node(std::uint32_t index) const;
    [[nodiscard]] std::string_view nodeName(const Node& node) const;
    void collect(std::uint32_t index, const std::string& prefix, std::vector<std::string>& out) const;

    Bytes m_image;
    std::string m_name;
    std::uint32_t m_nodeCount = 0, m_nodeTable = 0, m_nameTable = 0;
};

/// Packs every file of a source into a `.pak` image.
[[nodiscard]] Bytes buildPak(const Source& source);

/// Packs a source folder into a `.pak` file.
bool packFolder(const std::filesystem::path& folder, const std::filesystem::path& pak, std::string* error = nullptr);

} // namespace gridfin::data
