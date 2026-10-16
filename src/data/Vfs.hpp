#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The game's data lives in mounted sources: a source folder (for editing) or a `.pak` archive
/// (for shipping). Later mounts override files of the same path in earlier ones, so a mod
/// archive can replace single files of the base game, like a Doom PWAD.
namespace gridfin::data {

using Bytes = std::vector<std::uint8_t>;

/// Checks and normalises a data path: relative, `/`-separated, no `.`/`..` or empty parts,
/// backslashes turned into slashes. Paths are case-sensitive. Returns nullopt for invalid paths, so
/// data can't reach outside its source.
[[nodiscard]] std::optional<std::string> normalizePath(std::string_view path);

/// Joins a path relative to the directory of `base` ("sound/tones/bass.tone" + "../samples/a.wav").
[[nodiscard]] std::optional<std::string> resolveRelative(std::string_view base, std::string_view relative);

class Source {
public:
    virtual ~Source() = default;
    [[nodiscard]] virtual std::optional<Bytes> read(const std::string& path) const = 0;
    /// Every file path in the source, sorted.
    [[nodiscard]] virtual std::vector<std::string> list() const = 0;
    [[nodiscard]] virtual std::string describe() const = 0;
    /// Paths that changed, appeared or disappeared since the last call (for live reload).
    [[nodiscard]] virtual std::vector<std::string> pollChanges() { return {}; }
};

/// A source folder on disk.
class DirectorySource final : public Source {
public:
    explicit DirectorySource(std::filesystem::path root);
    [[nodiscard]] std::optional<Bytes> read(const std::string& path) const override;
    [[nodiscard]] std::vector<std::string> list() const override;
    [[nodiscard]] std::string describe() const override { return m_root.string(); }
    [[nodiscard]] std::vector<std::string> pollChanges() override;

private:
    [[nodiscard]] std::map<std::string, std::filesystem::file_time_type> scan() const;

    std::filesystem::path m_root;
    std::map<std::string, std::filesystem::file_time_type> m_stamps;
};

/// Files held in memory (test data, generated files).
class MemorySource final : public Source {
public:
    explicit MemorySource(std::string name = "memory") : m_name(std::move(name)) {}
    void add(const std::string& path, Bytes data);
    void add(const std::string& path, std::string_view text);
    [[nodiscard]] std::optional<Bytes> read(const std::string& path) const override;
    [[nodiscard]] std::vector<std::string> list() const override;
    [[nodiscard]] std::string describe() const override { return m_name; }

private:
    std::string m_name;
    std::map<std::string, Bytes> m_files;
};

class Vfs {
public:
    /// Adds a source on top of the existing ones.
    void mount(std::unique_ptr<Source> source);
    /// Mounts a folder or a `.pak` archive. Returns false with a message on failure.
    bool mountPath(const std::filesystem::path& path, std::string* error = nullptr);

    [[nodiscard]] std::optional<Bytes> read(std::string_view path) const;
    [[nodiscard]] std::optional<std::string> readText(std::string_view path) const;
    [[nodiscard]] bool exists(std::string_view path) const { return read(path).has_value(); }
    /// Every path under `prefix` (a folder such as "sound/tones/"), across all sources, sorted.
    [[nodiscard]] std::vector<std::string> list(std::string_view prefix = {}) const;
    /// Which source provides a path (for error messages and the editor).
    [[nodiscard]] std::string origin(std::string_view path) const;

    [[nodiscard]] bool empty() const noexcept { return m_sources.empty(); }
    [[nodiscard]] std::vector<std::string> pollChanges();

private:
    std::vector<std::unique_ptr<Source>> m_sources; ///< later ones win
};

} // namespace gridfin::data
