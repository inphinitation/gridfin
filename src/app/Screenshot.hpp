#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace gridfin {

/// Writes tightly packed RGBA8 pixels as a 24-bit BMP. Returns false on I/O errors.
bool writeBmp(const std::string& path, std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);

} // namespace gridfin
