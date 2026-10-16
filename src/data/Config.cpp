#include "data/Config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>

namespace gridfin::data {

namespace {

std::string_view trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    return text;
}

/// Cuts a `;` or `#` comment, unless it is inside double quotes.
std::string_view stripComment(std::string_view line)
{
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (!quoted && (line[i] == ';' || line[i] == '#')) return line.substr(0, i);
    }
    return line;
}

} // namespace

Config::Config(std::string_view text, std::string name) : m_name(std::move(name))
{
    std::string section;
    int lineNumber = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;

        line = trim(stripComment(line));
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.back() != ']') {
                m_errors.push_back(std::format("{}:{}: expected ']'", m_name, lineNumber));
                continue;
            }
            section = std::string(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            m_errors.push_back(std::format("{}:{}: expected 'key = value'", m_name, lineNumber));
            continue;
        }
        const std::string_view key = trim(line.substr(0, equals));
        std::string_view value = trim(line.substr(equals + 1));
        if (key.empty()) {
            m_errors.push_back(std::format("{}:{}: missing key", m_name, lineNumber));
            continue;
        }
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        std::string fullKey = section.empty() ? std::string(key) : std::format("{}.{}", section, key);
        if (find(fullKey)) {
            m_errors.push_back(std::format("{}:{}: '{}' is set twice", m_name, lineNumber, fullKey));
            continue;
        }
        m_entries.push_back({std::move(fullKey), std::string(value), lineNumber});
    }
}

const Config::Entry* Config::find(std::string_view key) const
{
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.key == key; });
    return it == m_entries.end() ? nullptr : &*it;
}

bool Config::has(std::string_view key) const
{
    return find(key) != nullptr;
}

std::optional<std::string> Config::string(std::string_view key) const
{
    const Entry* entry = find(key);
    if (!entry) return std::nullopt;
    m_used.insert(std::string(key));
    return entry->value;
}

std::string Config::string(std::string_view key, std::string_view fallback) const
{
    return string(key).value_or(std::string(fallback));
}

void Config::error(std::string_view key, std::string_view message) const
{
    const Entry* entry = find(key);
    m_errors.push_back(entry ? std::format("{}:{}: {}: {}", m_name, entry->line, key, message)
                             : std::format("{}: {}: {}", m_name, key, message));
}

int Config::integer(std::string_view key, int fallback, int minimum, int maximum) const
{
    const auto text = string(key);
    if (!text) return fallback;
    int value = 0;
    const auto [end, result] = std::from_chars(text->data(), text->data() + text->size(), value);
    if (result != std::errc{} || end != text->data() + text->size()) {
        error(key, std::format("'{}' is not a whole number", *text));
        return fallback;
    }
    if (value < minimum || value > maximum) {
        error(key, std::format("{} is outside {}..{}", value, minimum, maximum));
        return fallback;
    }
    return value;
}

double Config::number(std::string_view key, double fallback, double minimum, double maximum) const
{
    const auto text = string(key);
    if (!text) return fallback;
    // strtod instead of from_chars: floating point from_chars needs macOS 13.3+ runtime support.
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (text->empty() || end != text->c_str() + text->size()) {
        error(key, std::format("'{}' is not a number", *text));
        return fallback;
    }
    if (value < minimum || value > maximum) {
        error(key, std::format("{} is outside {}..{}", value, minimum, maximum));
        return fallback;
    }
    return value;
}

bool Config::boolean(std::string_view key, bool fallback) const
{
    const auto text = string(key);
    if (!text) return fallback;
    if (*text == "true" || *text == "yes" || *text == "on" || *text == "1") return true;
    if (*text == "false" || *text == "no" || *text == "off" || *text == "0") return false;
    error(key, std::format("'{}' is not true or false", *text));
    return fallback;
}

std::vector<std::string> Config::unusedKeys() const
{
    std::vector<std::string> unused;
    for (const Entry& entry : m_entries) {
        if (!m_used.contains(entry.key))
            unused.push_back(std::format("{}:{}: unknown key '{}'", m_name, entry.line, entry.key));
    }
    return unused;
}

} // namespace gridfin::data
