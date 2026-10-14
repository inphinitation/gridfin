#pragma once

#include <cstdio>
#include <format>
#include <string_view>

namespace gridfin::log {

enum class Level { Info, Warn, Error };

inline void write(Level level, std::string_view message)
{
    static constexpr std::string_view kPrefixes[] = {"info", "warn", "error"};
    std::FILE* stream = level == Level::Info ? stdout : stderr;
    std::fprintf(stream, "[gridfin:%s] %.*s\n", kPrefixes[static_cast<int>(level)].data(),
                 static_cast<int>(message.size()), message.data());
    std::fflush(stream);
}

template <typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace gridfin::log
