#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gridfin::data {

/// A decoded WAVE file, mixed down to mono.
struct WavSound {
    std::vector<float> samples; ///< -1..1
    int sampleRate = 0;
    int sourceChannels = 0;
    int sourceBits = 0;
    /// The first loop of a `smpl` chunk, in samples (end exclusive), if the file has one.
    std::optional<std::uint32_t> loopStart, loopEnd;
    /// The MIDI root note from the `smpl` chunk, if present.
    std::optional<int> rootNote;
};

/// Decodes RIFF WAVE: integer PCM (8/16/24/32-bit) or 32-bit float, any number of channels.
[[nodiscard]] std::optional<WavSound> decodeWav(std::span<const std::uint8_t> file, std::string* error = nullptr);

/// Encodes interleaved 16-bit PCM as RIFF WAVE.
[[nodiscard]] std::vector<std::uint8_t> encodeWav(std::span<const std::int16_t> samples, int channels, int sampleRate);

/// Writes interleaved 16-bit PCM as a WAVE file. Returns false on I/O errors.
bool writeWav(const std::string& path, std::span<const std::int16_t> samples, int channels, int sampleRate);

} // namespace gridfin::data
