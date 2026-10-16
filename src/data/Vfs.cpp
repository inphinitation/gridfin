#include "data/Vfs.hpp"

#include "data/Archive.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace gridfin::data {

namespace fs = std::filesystem;

std::optional<std::string> normalizePath(std::string_view path)
{
    std::string result;
    std::size_t start = 0;
    if (path.empty()) return std::nullopt;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        const std::string_view part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return std::nullopt;
        if (part.find(':') != std::string_view::npos) return std::nullopt; // drive letters, streams
        for (const char c : part) {
            if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
        }
        if (!result.empty()) result += '/';
        result += part;
        start = end + 1;
    }
    return result;
}

std::optional<std::string> resolveRelative(std::string_view base, std::string_view relative)
{
    std::vector<std::string_view> parts;
    const std::size_t slash = base.rfind('/');
    std::string_view directory = slash == std::string_view::npos ? std::string_view{} : base.substr(0, slash);
    const auto split = [&](std::string_view text, bool allowParent) {
        std::size_t start = 0;
        while (start <= text.size() && !text.empty()) {
            std::size_t end = text.find_first_of("/\\", start);
            if (end == std::string_view::npos) end = text.size();
            const std::string_view part = text.substr(start, end - start);
            if (part == ".." && allowParent) {
                if (parts.empty()) return false;
                parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            start = end + 1;
        }
        return true;
    };
    if (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
        relative.remove_prefix(1); // relative to the source root
    } else if (!split(directory, false)) {
        return std::nullopt;
    }
    if (!split(relative, true)) return std::nullopt;
    std::string joined;
    for (const std::string_view part : parts) {
        if (!joined.empty()) joined += '/';
        joined += part;
    }
    return normalizePath(joined);
}

// --- DirectorySource ----------------------------------------------------------------------------

DirectorySource::DirectorySource(fs::path root) : m_root(std::move(root))
{
    m_stamps = scan();
}

std::optional<Bytes> DirectorySource::read(const std::string& path) const
{
    std::error_code error;
    const fs::path file = m_root / fs::path(path);
    if (!fs::is_regular_file(file, error)) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::map<std::string, fs::file_time_type> DirectorySource::scan() const
{
    std::map<std::string, fs::file_time_type> files;
    std::error_code error;
    fs::recursive_directory_iterator it(m_root, fs::directory_options::skip_permission_denied, error);
    for (; !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() == '.') {
            // Hidden files and folders (.git, editor swap files) are not game data.
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error)) continue;
        const auto path = normalizePath(fs::relative(it->path(), m_root, error).generic_string());
        if (path) files[*path] = it->last_write_time(error);
    }
    return files;
}

std::vector<std::string> DirectorySource::list() const
{
    std::vector<std::string> paths;
    for (const auto& [path, stamp] : scan()) paths.push_back(path);
    return paths;
}

std::vector<std::string> DirectorySource::pollChanges()
{
    auto current = scan();
    std::vector<std::string> changed;
    for (const auto& [path, stamp] : current) {
        const auto old = m_stamps.find(path);
        if (old == m_stamps.end() || old->second != stamp) changed.push_back(path);
    }
    for (const auto& [path, stamp] : m_stamps) {
        if (!current.contains(path)) changed.push_back(path);
    }
    m_stamps = std::move(current);
    return changed;
}

// --- MemorySource -------------------------------------------------------------------------------

void MemorySource::add(const std::string& path, Bytes data)
{
    if (const auto normalized = normalizePath(path)) m_files[*normalized] = std::move(data);
}

void MemorySource::add(const std::string& path, std::string_view text)
{
    add(path, Bytes(text.begin(), text.end()));
}

std::optional<Bytes> MemorySource::read(const std::string& path) const
{
    const auto it = m_files.find(path);
    if (it == m_files.end()) return std::nullopt;
    return it->second;
}

std::vector<std::string> MemorySource::list() const
{
    std::vector<std::string> paths;
    for (const auto& [path, data] : m_files) paths.push_back(path);
    return paths;
}

// --- Vfs ----------------------------------------------------------------------------------------

void Vfs::mount(std::unique_ptr<Source> source)
{
    m_sources.push_back(std::move(source));
}

bool Vfs::mountPath(const fs::path& path, std::string* error)
{
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        mount(std::make_unique<DirectorySource>(path));
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = std::format("{} is neither a folder nor a readable archive", path.string());
        return false;
    }
    const Bytes image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto archive = openArchive(image, path.string(), error);
    if (!archive) return false;
    mount(std::move(archive));
    return true;
}

std::optional<Bytes> Vfs::read(std::string_view path) const
{
    const auto normalized = normalizePath(path);
    if (!normalized) return std::nullopt;
    for (auto it = m_sources.rbegin(); it != m_sources.rend(); ++it) {
        if (auto data = (*it)->read(*normalized)) return data;
    }
    return std::nullopt;
}

std::optional<std::string> Vfs::readText(std::string_view path) const
{
    auto data = read(path);
    if (!data) return std::nullopt;
    return std::string(data->begin(), data->end());
}

std::vector<std::string> Vfs::list(std::string_view prefix) const
{
    std::set<std::string> paths;
    for (const auto& source : m_sources) {
        for (std::string& path : source->list()) {
            if (path.starts_with(prefix)) paths.insert(std::move(path));
        }
    }
    return {paths.begin(), paths.end()};
}

std::string Vfs::origin(std::string_view path) const
{
    const auto normalized = normalizePath(path);
    if (!normalized) return {};
    for (auto it = m_sources.rbegin(); it != m_sources.rend(); ++it) {
        if ((*it)->read(*normalized)) return (*it)->describe();
    }
    return {};
}

std::vector<std::string> Vfs::pollChanges()
{
    std::vector<std::string> changed;
    for (const auto& source : m_sources) {
        for (std::string& path : source->pollChanges()) changed.push_back(std::move(path));
    }
    return changed;
}

} // namespace gridfin::data
