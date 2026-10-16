#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace gridfin {

/// Writes interleaved 16-bit PCM as a RIFF WAVE file. Returns false on I/O errors.
bool writeWav(const std::string& path, std::span<const std::int16_t> samples, int channels, int sampleRate);

} // namespace gridfin
