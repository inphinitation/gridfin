#pragma once

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gridfin::data {

/// A small `key = value` text format for manifests and asset definitions.
///
///     ; comment (also #)
///     title = Gridfin
///     [modulator]            ; later keys are read as "modulator.<key>"
///     decay1 = 15
///
/// Reading a key marks it as used, so typos can be reported with unusedKeys().
class Config {
public:
    /// Parses text; `name` is used in messages. Syntax errors are collected in errors().
    Config(std::string_view text, std::string name);

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> string(std::string_view key) const;
    [[nodiscard]] std::string string(std::string_view key, std::string_view fallback) const;
    /// Integer in [minimum, maximum]; out of range or malformed values are reported and give the fallback.
    [[nodiscard]] int integer(std::string_view key, int fallback, int minimum, int maximum) const;
    [[nodiscard]] double number(std::string_view key, double fallback, double minimum, double maximum) const;
    [[nodiscard]] bool boolean(std::string_view key, bool fallback) const;

    /// Reports a problem with a key's value (with its line).
    void error(std::string_view key, std::string_view message) const;

    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return m_errors; }
    [[nodiscard]] std::vector<std::string> unusedKeys() const;
    [[nodiscard]] const std::string& name() const noexcept { return m_name; }

private:
    struct Entry {
        std::string key;
        std::string value;
        int line = 0;
    };
    [[nodiscard]] const Entry* find(std::string_view key) const;

    std::string m_name;
    std::vector<Entry> m_entries;
    mutable std::set<std::string, std::less<>> m_used;
    mutable std::vector<std::string> m_errors;
};

} // namespace gridfin::data
