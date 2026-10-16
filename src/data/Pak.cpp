#include "data/Pak.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <fstream>
#include <map>

namespace gridfin::data {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'G', 'P', 'A', 'K'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kHeaderSize = 32;
constexpr std::uint32_t kNodeSize = 16;
constexpr std::uint16_t kFolder = 1;

std::uint32_t get32(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8)
           | (static_cast<std::uint32_t>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t get16(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

void set32(Bytes& out, std::size_t at, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
}

void set16(Bytes& out, std::size_t at, std::uint16_t value)
{
    out[at] = static_cast<std::uint8_t>(value & 0xffu);
    out[at + 1] = static_cast<std::uint8_t>(value >> 8);
}

/// The folder tree while packing.
struct BuildNode {
    bool folder = true;
    std::string path; ///< for files
    std::map<std::string, BuildNode> children;
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

// --- Packing ------------------------------------------------------------------------------------

Bytes buildPak(const Source& source)
{
    BuildNode root;
    for (const std::string& path : source.list()) {
        BuildNode* folder = &root;
        std::size_t start = 0;
        while (true) {
            const std::size_t slash = path.find('/', start);
            const std::string part = path.substr(start, slash - start);
            if (slash == std::string::npos) {
                BuildNode& file = folder->children[part];
                file.folder = false;
                file.path = path;
                break;
            }
            BuildNode& next = folder->children[part];
            if (!next.folder) break; // a file and a folder of the same name: the file wins
            folder = &next;
            start = slash + 1;
        }
    }

    // Number the nodes breadth first, so every folder's children are contiguous and sorted
    // (std::map orders names bytewise, like the lookup).
    std::vector<const BuildNode*> order{&root};
    std::vector<std::string> names{""};
    std::vector<std::uint32_t> firstChild;
    for (std::size_t i = 0; i < order.size(); ++i) {
        firstChild.push_back(static_cast<std::uint32_t>(order.size()));
        for (const auto& [name, child] : order[i]->children) {
            order.push_back(&child);
            names.push_back(name);
        }
    }

    const auto count = static_cast<std::uint32_t>(order.size());
    const std::uint32_t nodeTable = kHeaderSize;
    const std::uint32_t crcTable = nodeTable + count * kNodeSize;
    const std::uint32_t nameTable = crcTable + count * 4;
    std::uint32_t nameSize = 0;
    for (const std::string& name : names) nameSize += static_cast<std::uint32_t>(name.size());
    const std::uint32_t dataStart = (nameTable + nameSize + 3u) & ~3u;

    Bytes out(dataStart, 0);
    for (std::size_t i = 0; i < kMagic.size(); ++i) out[i] = kMagic[i];
    set32(out, 4, kVersion);
    set32(out, 8, count);
    set32(out, 12, nodeTable);
    set32(out, 16, nameTable);
    set32(out, 20, nameSize);
    set32(out, 24, dataStart);

    std::uint32_t nameOffset = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const BuildNode& node = *order[i];
        const std::size_t at = nodeTable + i * kNodeSize;
        std::copy(names[i].begin(), names[i].end(), out.begin() + static_cast<std::ptrdiff_t>(nameTable + nameOffset));
        set32(out, at, nameOffset);
        set16(out, at + 4, static_cast<std::uint16_t>(names[i].size()));
        nameOffset += static_cast<std::uint32_t>(names[i].size());
        if (node.folder) {
            set16(out, at + 6, kFolder);
            set32(out, at + 8, node.children.empty() ? 0 : firstChild[i]);
            set32(out, at + 12, static_cast<std::uint32_t>(node.children.size()));
            continue;
        }
        const Bytes data = source.read(node.path).value_or(Bytes{});
        while (out.size() % 4 != 0) out.push_back(0);
        set32(out, at + 8, static_cast<std::uint32_t>(out.size()));
        set32(out, at + 12, static_cast<std::uint32_t>(data.size()));
        set32(out, crcTable + i * 4, crc32(data));
        out.insert(out.end(), data.begin(), data.end());
    }
    return out;
}

bool packFolder(const std::filesystem::path& folder, const std::filesystem::path& pak, std::string* error)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        if (error) *error = std::format("{} is not a folder", folder.string());
        return false;
    }
    const Bytes image = buildPak(DirectorySource(folder));
    std::ofstream out(pak, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!out) {
        if (error) *error = std::format("could not write {}", pak.string());
        return false;
    }
    return true;
}

// --- Reading ------------------------------------------------------------------------------------

PakSource::Node PakSource::node(std::uint32_t index) const
{
    const std::size_t at = m_nodeTable + static_cast<std::size_t>(index) * kNodeSize;
    return {get32(m_image, at), get16(m_image, at + 4), get16(m_image, at + 6), get32(m_image, at + 8),
            get32(m_image, at + 12)};
}

std::string_view PakSource::nodeName(const Node& n) const
{
    return {reinterpret_cast<const char*>(m_image.data()) + m_nameTable + n.nameOffset, n.nameLength};
}

std::unique_ptr<PakSource> PakSource::open(Bytes image, std::string name, std::string* error)
{
    const auto fail = [&](std::string_view why) -> std::unique_ptr<PakSource> {
        if (error) *error = std::format("{}: {}", name, why);
        return nullptr;
    };
    if (image.size() < kHeaderSize || !std::equal(kMagic.begin(), kMagic.end(), image.begin())) {
        return fail("not a Gridfin .pak");
    }
    if (const std::uint32_t version = get32(image, 4); version != kVersion) {
        return fail(std::format("unsupported .pak version {}", version));
    }
    const std::uint64_t count = get32(image, 8), nodeTable = get32(image, 12), nameTable = get32(image, 16),
                        nameSize = get32(image, 20);
    if (count == 0 || nodeTable < kHeaderSize || nodeTable + count * (kNodeSize + 4) > image.size()
        || nameTable + nameSize > image.size()) {
        return fail("tables outside the file");
    }

    auto pak = std::unique_ptr<PakSource>(new PakSource(std::move(image), std::move(name)));
    pak->m_nodeCount = static_cast<std::uint32_t>(count);
    pak->m_nodeTable = static_cast<std::uint32_t>(nodeTable);
    pak->m_nameTable = static_cast<std::uint32_t>(nameTable);
    const std::span<const std::uint8_t> bytes = pak->m_image;
    const std::uint64_t crcTable = nodeTable + count * kNodeSize;

    // Every node but the root must have exactly one parent; children come after their folder.
    std::vector<std::uint8_t> parents(static_cast<std::size_t>(count), 0);
    for (std::uint32_t i = 0; i < count; ++i) {
        const Node n = pak->node(i);
        if (static_cast<std::uint64_t>(n.nameOffset) + n.nameLength > nameSize) return fail("name outside the table");
        const std::string_view nodeName = pak->nodeName(n);
        if (i == 0) {
            if (!(n.flags & kFolder) || !nodeName.empty()) return fail("the root is not a folder");
        } else if (nodeName.find('/') != std::string_view::npos || normalizePath(nodeName) != nodeName) {
            return fail(std::format("invalid name '{}'", nodeName));
        }
        if (n.flags & kFolder) {
            if (n.b == 0) continue;
            if (n.a <= i || static_cast<std::uint64_t>(n.a) + n.b > count) return fail("invalid folder");
            for (std::uint32_t c = n.a; c < n.a + n.b; ++c) {
                if (++parents[c] > 1) return fail("a node has two parents");
                if (c > n.a && !(pak->nodeName(pak->node(c - 1)) < pak->nodeName(pak->node(c)))) {
                    return fail(std::format("entries of '{}' are not sorted", nodeName));
                }
            }
        } else {
            if (static_cast<std::uint64_t>(n.a) + n.b > bytes.size())
                return fail(std::format("'{}' is truncated", nodeName));
            if (crc32(bytes.subspan(n.a, n.b)) != get32(bytes, static_cast<std::size_t>(crcTable + i * 4u))) {
                return fail(std::format("'{}' is corrupt (checksum mismatch)", nodeName));
            }
        }
    }
    for (std::uint32_t i = 1; i < count; ++i) {
        if (parents[i] != 1) return fail("a node is not in the tree");
    }
    return pak;
}

std::optional<Bytes> PakSource::read(const std::string& path) const
{
    Node current = node(0);
    std::size_t start = 0;
    while (true) {
        if (!(current.flags & kFolder)) return std::nullopt;
        const std::size_t slash = path.find('/', start);
        const std::string_view part = std::string_view(path).substr(start, slash - start);
        // Binary search among the folder's sorted children.
        std::uint32_t low = current.a, high = current.a + current.b;
        bool found = false;
        while (low < high) {
            const std::uint32_t mid = low + (high - low) / 2;
            const Node candidate = node(mid);
            const int order = nodeName(candidate).compare(part);
            if (order == 0) {
                current = candidate;
                found = true;
                break;
            }
            if (order < 0) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        if (!found) return std::nullopt;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (current.flags & kFolder) return std::nullopt;
    const auto first = m_image.begin() + static_cast<std::ptrdiff_t>(current.a);
    return Bytes(first, first + static_cast<std::ptrdiff_t>(current.b));
}

void PakSource::collect(std::uint32_t index, const std::string& prefix, std::vector<std::string>& out) const
{
    const Node folder = node(index);
    for (std::uint32_t c = folder.a; c < folder.a + folder.b; ++c) {
        const Node child = node(c);
        const std::string path = prefix + std::string(nodeName(child));
        if (child.flags & kFolder) {
            collect(c, path + "/", out);
        } else {
            out.push_back(path);
        }
    }
}

std::vector<std::string> PakSource::list() const
{
    std::vector<std::string> paths;
    collect(0, "", paths);
    std::ranges::sort(paths); // depth-first order differs from full-path order ("a/b" vs "a-b")
    return paths;
}

} // namespace gridfin::data
