#pragma once

#include "data/Vfs.hpp"

#include <filesystem>
#include <memory>
#include <string>

/// The packed archive format (`.gfa`), all integers little-endian:
///
///     header     "GFA1", u32 version (1), u32 entry count, u32 directory offset
///     data       the files, each aligned to 4 bytes
///     directory  per entry: u16 path length, path (UTF-8), u32 offset, u32 size, u32 CRC-32
///
/// Paths follow normalizePath(). Entries are sorted by path.
namespace gridfin::data {

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data);

/// Packs every file of a source into an archive image.
[[nodiscard]] Bytes packArchive(const Source& source);

/// Reads an archive image, checking its structure and checksums.
[[nodiscard]] std::unique_ptr<MemorySource> openArchive(std::span<const std::uint8_t> image, std::string name,
                                                        std::string* error = nullptr);

/// Packs a source folder into an archive file.
bool packDirectory(const std::filesystem::path& folder, const std::filesystem::path& archive,
                   std::string* error = nullptr);

} // namespace gridfin::data
