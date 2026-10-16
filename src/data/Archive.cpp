#include "data/Archive.hpp"

#include <array>
#include <format>
#include <fstream>

namespace gridfin::data {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'G', 'F', 'A', '1'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;

void put32(Bytes& out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu));
}

void put32At(Bytes& out, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) out[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
}

struct Reader {
    std::span<const std::uint8_t> data;
    std::size_t pos = 0;

    bool get32(std::uint32_t& value)
    {
        if (data.size() - pos < 4 || pos > data.size()) return false;
        value = static_cast<std::uint32_t>(data[pos]) | (static_cast<std::uint32_t>(data[pos + 1]) << 8)
                | (static_cast<std::uint32_t>(data[pos + 2]) << 16) | (static_cast<std::uint32_t>(data[pos + 3]) << 24);
        pos += 4;
        return true;
    }
    bool get16(std::uint16_t& value)
    {
        if (data.size() - pos < 2 || pos > data.size()) return false;
        value = static_cast<std::uint16_t>(data[pos] | (data[pos + 1] << 8));
        pos += 2;
        return true;
    }
};

} // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data)
{
    static const auto kTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        return table;
    }();
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t byte : data) crc = kTable[(crc ^ byte) & 0xffu] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

Bytes packArchive(const Source& source)
{
    struct Entry {
        std::string path;
        std::uint32_t offset, size, crc;
    };
    std::vector<Entry> entries;
    Bytes out(kMagic.begin(), kMagic.end());
    put32(out, kVersion);
    put32(out, 0); // entry count
    put32(out, 0); // directory offset

    for (const std::string& path : source.list()) {
        const auto data = source.read(path);
        if (!data) continue;
        while (out.size() % 4 != 0) out.push_back(0);
        entries.push_back(
            {path, static_cast<std::uint32_t>(out.size()), static_cast<std::uint32_t>(data->size()), crc32(*data)});
        out.insert(out.end(), data->begin(), data->end());
    }

    while (out.size() % 4 != 0) out.push_back(0);
    const auto directory = static_cast<std::uint32_t>(out.size());
    for (const Entry& entry : entries) {
        const auto length = static_cast<std::uint16_t>(entry.path.size());
        out.push_back(static_cast<std::uint8_t>(length & 0xffu));
        out.push_back(static_cast<std::uint8_t>(length >> 8));
        out.insert(out.end(), entry.path.begin(), entry.path.end());
        put32(out, entry.offset);
        put32(out, entry.size);
        put32(out, entry.crc);
    }
    put32At(out, 8, static_cast<std::uint32_t>(entries.size()));
    put32At(out, 12, directory);
    return out;
}

std::unique_ptr<MemorySource> openArchive(std::span<const std::uint8_t> image, std::string name, std::string* error)
{
    const auto fail = [&](std::string_view why) -> std::unique_ptr<MemorySource> {
        if (error) *error = std::format("{}: {}", name, why);
        return nullptr;
    };
    if (image.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), image.begin())) {
        return fail("not a Gridfin archive");
    }
    Reader reader{image, 4};
    std::uint32_t version = 0, count = 0, directory = 0;
    reader.get32(version);
    reader.get32(count);
    reader.get32(directory);
    if (version != kVersion) return fail(std::format("unsupported archive version {}", version));
    if (directory > image.size()) return fail("directory outside the file");

    auto source = std::make_unique<MemorySource>(name);
    reader.pos = directory;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t length = 0;
        if (!reader.get16(length) || image.size() - reader.pos < length) return fail("truncated directory");
        const std::string rawPath(reinterpret_cast<const char*>(image.data() + reader.pos), length);
        reader.pos += length;
        std::uint32_t offset = 0, size = 0, crc = 0;
        if (!reader.get32(offset) || !reader.get32(size) || !reader.get32(crc)) return fail("truncated directory");
        const auto path = normalizePath(rawPath);
        if (!path || *path != rawPath) return fail(std::format("invalid path '{}'", rawPath));
        if (offset > image.size() || size > image.size() - offset) return fail(std::format("{} is truncated", *path));
        const auto data = image.subspan(offset, size);
        if (crc32(data) != crc) return fail(std::format("{} is corrupt (checksum mismatch)", *path));
        source->add(*path, Bytes(data.begin(), data.end()));
    }
    return source;
}

bool packDirectory(const std::filesystem::path& folder, const std::filesystem::path& archive, std::string* error)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        if (error) *error = std::format("{} is not a folder", folder.string());
        return false;
    }
    const Bytes image = packArchive(DirectorySource(folder));
    std::ofstream out(archive, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) {
        if (error) *error = std::format("could not write {}", archive.string());
        return false;
    }
    return true;
}

} // namespace gridfin::data
