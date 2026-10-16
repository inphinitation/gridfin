#include "data/Wav.hpp"

#include <bit>
#include <cstring>
#include <format>
#include <fstream>

namespace gridfin::data {

namespace {

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8)
           | (static_cast<std::uint32_t>(data[at + 2]) << 16) | (static_cast<std::uint32_t>(data[at + 3]) << 24);
}

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t at)
{
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

bool tagIs(std::span<const std::uint8_t> data, std::size_t at, const char* tag)
{
    return std::memcmp(data.data() + at, tag, 4) == 0;
}

} // namespace

std::optional<WavSound> decodeWav(std::span<const std::uint8_t> file, std::string* error)
{
    const auto fail = [&](std::string message) -> std::optional<WavSound> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    if (file.size() < 12 || !tagIs(file, 0, "RIFF") || !tagIs(file, 8, "WAVE")) return fail("not a WAVE file");

    int format = 0, channels = 0, bits = 0, rate = 0, blockAlign = 0;
    std::span<const std::uint8_t> pcm;
    WavSound sound;
    std::size_t pos = 12;
    while (pos + 8 <= file.size()) {
        const std::uint32_t size = read32(file, pos + 4);
        const std::size_t body = pos + 8;
        if (size > file.size() - body) return fail("truncated chunk");
        const auto chunk = file.subspan(body, size);
        if (tagIs(file, pos, "fmt ") && size >= 16) {
            format = read16(chunk, 0);
            channels = read16(chunk, 2);
            rate = static_cast<int>(read32(chunk, 4));
            blockAlign = read16(chunk, 12);
            bits = read16(chunk, 14);
            if (format == 0xfffe && size >= 26) format = read16(chunk, 24); // WAVE_FORMAT_EXTENSIBLE
        } else if (tagIs(file, pos, "data")) {
            pcm = chunk;
        } else if (tagIs(file, pos, "smpl") && size >= 36) {
            sound.rootNote = static_cast<int>(read32(chunk, 12) & 0x7fu);
            if (read32(chunk, 28) > 0 && size >= 36 + 24) {
                sound.loopStart = read32(chunk, 36 + 8);
                sound.loopEnd = read32(chunk, 36 + 12) + 1; // smpl loop ends are inclusive
            }
        }
        pos = body + size + (size & 1u); // chunks are padded to even sizes
    }

    if (channels <= 0 || rate <= 0 || blockAlign <= 0) return fail("missing or invalid fmt chunk");
    const bool isFloat = format == 3 && bits == 32;
    if (!(format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) && !isFloat) {
        return fail(
            std::format("unsupported sample format {} with {} bits (use integer PCM or 32-bit float)", format, bits));
    }
    const int bytesPerSample = bits / 8;
    if (blockAlign < bytesPerSample * channels) return fail("invalid block alignment");

    const std::size_t frames = pcm.size() / static_cast<std::size_t>(blockAlign);
    sound.samples.assign(frames, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float sum = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            const std::size_t at =
                frame * static_cast<std::size_t>(blockAlign) + static_cast<std::size_t>(channel * bytesPerSample);
            float value = 0.0f;
            if (isFloat) {
                value = std::bit_cast<float>(read32(pcm, at));
            } else if (bits == 8) {
                value = (static_cast<float>(pcm[at]) - 128.0f) / 128.0f; // 8-bit WAVE is unsigned
            } else if (bits == 16) {
                value = static_cast<float>(static_cast<std::int16_t>(read16(pcm, at))) / 32768.0f;
            } else if (bits == 24) {
                const auto raw = static_cast<std::int32_t>(static_cast<std::uint32_t>(pcm[at]) << 8
                                                           | static_cast<std::uint32_t>(pcm[at + 1]) << 16
                                                           | static_cast<std::uint32_t>(pcm[at + 2]) << 24);
                value = static_cast<float>(raw) / 2147483648.0f;
            } else {
                value = static_cast<float>(static_cast<std::int32_t>(read32(pcm, at))) / 2147483648.0f;
            }
            sum += value;
        }
        sound.samples[frame] = sum / static_cast<float>(channels);
    }
    sound.sampleRate = rate;
    sound.sourceChannels = channels;
    sound.sourceBits = bits;
    if (sound.loopEnd && (*sound.loopEnd > frames || *sound.loopStart >= *sound.loopEnd)) {
        sound.loopStart.reset();
        sound.loopEnd.reset();
    }
    return sound;
}

std::vector<std::uint8_t> encodeWav(std::span<const std::int16_t> samples, int channels, int sampleRate)
{
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<std::uint8_t> file;
    file.reserve(44 + dataBytes);
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu));
    };
    const auto tag = [&](const char* text) { file.insert(file.end(), text, text + 4); };

    tag("RIFF");
    put(36 + dataBytes, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(static_cast<std::uint32_t>(channels), 2);
    put(static_cast<std::uint32_t>(sampleRate), 4);
    put(static_cast<std::uint32_t>(sampleRate * channels * 2), 4);
    put(static_cast<std::uint32_t>(channels * 2), 2);
    put(16, 2);
    tag("data");
    put(dataBytes, 4);
    for (const std::int16_t sample : samples) put(static_cast<std::uint16_t>(sample), 2);
    return file;
}

bool writeWav(const std::string& path, std::span<const std::int16_t> samples, int channels, int sampleRate)
{
    const auto file = encodeWav(samples, channels, sampleRate);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

} // namespace gridfin::data
