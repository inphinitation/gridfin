#include "app/Wav.hpp"

#include <fstream>
#include <vector>

namespace gridfin {

bool writeWav(const std::string& path, std::span<const std::int16_t> samples, int channels, int sampleRate)
{
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    std::vector<std::uint8_t> file;
    file.reserve(44 + dataBytes);
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xff));
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

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return static_cast<bool>(out);
}

} // namespace gridfin
