#include "game/SoundBank.hpp"

#include "data/Wav.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <sstream>

namespace gridfin {

namespace {

using saturn::LoopMode;
using saturn::Operator;
using saturn::SampleFormat;

/// SCSP loop end and loop start registers are 16 bits wide.
constexpr std::size_t kMaxSampleLength = 65'535;

std::string key(std::string_view prefix, std::string_view name)
{
    return std::string(prefix) + std::string(name);
}

double noteFrequency(int note)
{
    return 440.0 * std::exp2(static_cast<double>(note - 69) / 12.0);
}

std::optional<SampleFormat> parseFormat(const data::Config& config, const std::string& name)
{
    const auto text = config.string(name);
    if (!text) return std::nullopt;
    if (*text == "pcm8") return SampleFormat::Pcm8;
    if (*text == "pcm16") return SampleFormat::Pcm16;
    config.error(name, std::format("'{}' is not pcm8 or pcm16", *text));
    return std::nullopt;
}

} // namespace

std::optional<int> parseNote(std::string_view text)
{
    if (text.empty()) return std::nullopt;
    int value = 0;
    if (std::isdigit(static_cast<unsigned char>(text.front()))) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value > 127) return std::nullopt;
        return value;
    }
    constexpr std::array<int, 7> kSemitone = {9, 11, 0, 2, 4, 5, 7}; // a..g
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(text.front())));
    if (letter < 'a' || letter > 'g') return std::nullopt;
    int note = kSemitone[static_cast<std::size_t>(letter - 'a')];
    std::size_t pos = 1;
    while (pos < text.size() && (text[pos] == '#' || text[pos] == '+' || text[pos] == '-')) {
        note += text[pos] == '-' ? -1 : 1;
        ++pos;
    }
    int octave = 0;
    const auto [end, error] = std::from_chars(text.data() + pos, text.data() + text.size(), octave);
    if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    note += (octave + 1) * 12;
    if (note < 0 || note > 127) return std::nullopt;
    return note;
}

void SoundBank::loadFolder(const data::Vfs& vfs, std::string_view folder, std::vector<std::string>& errors)
{
    for (const std::string& path : vfs.list(folder)) {
        if (path.ends_with(".tone")) loadTone(vfs, path, errors);
    }
}

bool SoundBank::loadTone(const data::Vfs& vfs, const std::string& path, std::vector<std::string>& errors)
{
    const auto text = vfs.readText(path);
    if (!text) {
        errors.push_back(std::format("{}: cannot read", path));
        return false;
    }
    data::Config config(*text, path);
    const int program = config.integer("program", -1, 0, 255);
    if (program < 0 && !config.has("program")) config.error("program", "missing (0..255)");

    saturn::Tone tone;
    auto carrier = loadOperator(vfs, path, config, "");
    std::optional<Operator> modulator;
    if (config.has("modulator.sample") || config.has("modulator.wave")) {
        modulator = loadOperator(vfs, path, config, "modulator.");
    }
    tone.modulationLevel = static_cast<std::uint8_t>(config.integer("fm-depth", modulator ? 9 : 0, 0, 15));
    tone.fixedPitch = config.boolean("fixed-pitch", false);
    const int pan = config.integer("pan", 0, -15, 15);
    tone.pan = static_cast<std::uint8_t>(std::abs(pan) | (pan > 0 ? 0x10 : 0x00));
    tone.effectLevel = static_cast<std::uint8_t>(config.integer("reverb", 0, 0, 7));
    tone.lfoFrequency = static_cast<std::uint8_t>(config.integer("lfo-rate", 0, 0, 31));
    tone.pitchLfoDepth = static_cast<std::uint8_t>(config.integer("vibrato", 0, 0, 7));
    tone.ampLfoDepth = static_cast<std::uint8_t>(config.integer("tremolo", 0, 0, 7));

    if (program >= 0 && m_driver.hasTone(program))
        config.error("program", std::format("{} is used by another tone", program));
    const bool ok = config.errors().empty() && carrier;
    for (const std::string& error : config.errors()) errors.push_back(error);
    for (const std::string& unused : config.unusedKeys()) errors.push_back(unused);
    if (!ok || program < 0) return false;

    tone.carrier = *carrier;
    tone.modulator = modulator;
    m_driver.setTone(program, tone);
    ++m_stats.tones;
    return true;
}

std::optional<Operator> SoundBank::loadOperator(const data::Vfs& vfs, const std::string& path,
                                                const data::Config& config, std::string_view prefix)
{
    const bool hasSample = config.has(key(prefix, "sample"));
    const bool hasWave = config.has(key(prefix, "wave"));
    if (hasSample == hasWave) {
        config.error(key(prefix, "sample"), "set exactly one of 'sample' (a .wav file) or 'wave' (harmonics)");
        return std::nullopt;
    }
    auto op = hasSample ? importSample(vfs, path, config, prefix) : buildWave(config, prefix);
    if (!op) return std::nullopt;

    op->attackRate = static_cast<std::uint8_t>(config.integer(key(prefix, "attack"), 31, 0, 31));
    op->decay1Rate = static_cast<std::uint8_t>(config.integer(key(prefix, "decay1"), 0, 0, 31));
    op->decayLevel = static_cast<std::uint8_t>(config.integer(key(prefix, "decay-level"), 0, 0, 31));
    op->decay2Rate = static_cast<std::uint8_t>(config.integer(key(prefix, "decay2"), 0, 0, 31));
    op->releaseRate = static_cast<std::uint8_t>(config.integer(key(prefix, "release"), 20, 0, 31));
    op->totalLevel = static_cast<std::uint8_t>(config.integer(key(prefix, "level"), 0, 0, 255));
    op->frequencyRatio = config.number(key(prefix, "frequency-ratio"), 1.0, 1.0 / 64.0, 64.0);
    return op;
}

std::optional<Operator> SoundBank::buildWave(const data::Config& config, std::string_view prefix)
{
    const std::string waveKey = key(prefix, "wave");
    std::vector<double> harmonics;
    std::istringstream stream(config.string(waveKey, ""));
    std::string word;
    while (stream >> word) {
        char* end = nullptr;
        const double amplitude = std::strtod(word.c_str(), &end);
        if (end != word.c_str() + word.size()) {
            config.error(waveKey, std::format("'{}' is not a number", word));
            return std::nullopt;
        }
        harmonics.push_back(amplitude);
    }
    if (harmonics.empty() || harmonics.size() > 64) {
        config.error(waveKey, "list 1 to 64 harmonic amplitudes");
        return std::nullopt;
    }
    const int length = config.integer(key(prefix, "wave-length"), 64, 16, 1024);
    if (harmonics.size() * 2 >= static_cast<std::size_t>(length)) {
        config.error(waveKey,
                     std::format("{} harmonics need a wave-length above {}", harmonics.size(), harmonics.size() * 2));
        return std::nullopt;
    }
    const SampleFormat format = parseFormat(config, key(prefix, "format")).value_or(SampleFormat::Pcm16);

    std::vector<float> samples(static_cast<std::size_t>(length));
    float peak = 0.0f;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const double phase = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(length);
        double value = 0.0;
        for (std::size_t h = 0; h < harmonics.size(); ++h)
            value += harmonics[h] * std::sin(phase * static_cast<double>(h + 1));
        samples[i] = static_cast<float>(value);
        peak = std::max(peak, std::abs(samples[i]));
    }
    if (peak <= 0.0f) {
        config.error(waveKey, "the waveform is silent");
        return std::nullopt;
    }
    for (float& sample : samples) sample = sample / peak * 0.92f;

    const std::string cacheKey =
        std::format("wave:{}:{}:{}", config.string(waveKey, ""), length, static_cast<int>(format));
    Operator op;
    if (const auto cached = m_samples.find(cacheKey); cached != m_samples.end()) {
        op = cached->second;
    } else {
        auto uploaded = upload(config, waveKey, samples, format);
        if (!uploaded) return std::nullopt;
        op = *uploaded;
        m_samples[cacheKey] = op;
    }
    op.loop = LoopMode::Forward;
    op.loopStart = 0;
    op.sampleRate = saturn::kSoundSampleRate;
    op.rootFrequency = saturn::kSoundSampleRate / static_cast<double>(length); // one cycle per pass
    return op;
}

std::optional<Operator> SoundBank::importSample(const data::Vfs& vfs, const std::string& path,
                                                const data::Config& config, std::string_view prefix)
{
    const std::string sampleKey = key(prefix, "sample");
    const auto samplePath = data::resolveRelative(path, config.string(sampleKey, ""));
    if (!samplePath) {
        config.error(sampleKey, "invalid path");
        return std::nullopt;
    }
    const auto file = vfs.read(*samplePath);
    if (!file) {
        config.error(sampleKey, std::format("{} not found", *samplePath));
        return std::nullopt;
    }
    std::string error;
    const auto wav = data::decodeWav(*file, &error);
    if (!wav) {
        config.error(sampleKey, std::format("{}: {}", *samplePath, error));
        return std::nullopt;
    }

    const int downsample = config.integer(key(prefix, "downsample"), 1, 1, 8);
    const SampleFormat format = parseFormat(config, key(prefix, "format"))
                                    .value_or(wav->sourceBits == 8 ? SampleFormat::Pcm8 : SampleFormat::Pcm16);

    // Loop points: the tone file wins over the WAVE file's smpl chunk.
    const std::size_t sourceLength = wav->samples.size();
    const bool loop = config.boolean(key(prefix, "loop"), wav->loopEnd.has_value());
    const int maxPoint = static_cast<int>(std::min<std::size_t>(sourceLength, 1u << 30));
    const auto loopStart = static_cast<std::size_t>(
        config.integer(key(prefix, "loop-start"), static_cast<int>(wav->loopStart.value_or(0)), 0, maxPoint));
    const auto loopEnd = static_cast<std::size_t>(
        config.integer(key(prefix, "loop-end"),
                       static_cast<int>(wav->loopEnd.value_or(static_cast<std::uint32_t>(sourceLength))), 1, maxPoint));
    if (loop && loopStart >= loopEnd) {
        config.error(key(prefix, "loop-start"), "must be before loop-end");
        return std::nullopt;
    }
    // A looping sample has no use for anything after its loop.
    const std::size_t usedLength = loop ? loopEnd : sourceLength;

    // Downsampling by averaging (a crude low-pass, like the tools of the time).
    const auto factor = static_cast<std::size_t>(downsample);
    std::vector<float> samples(usedLength / factor);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < factor; ++k) sum += wav->samples[i * factor + k];
        samples[i] = sum / static_cast<float>(factor);
    }
    if (samples.empty()) {
        config.error(sampleKey, std::format("{} has no samples", *samplePath));
        return std::nullopt;
    }
    if (samples.size() > kMaxSampleLength) {
        config.error(sampleKey, std::format("{} is {} samples long; the SCSP plays at most {} (try downsample = {})",
                                            *samplePath, samples.size(), kMaxSampleLength,
                                            (samples.size() + kMaxSampleLength - 1) / kMaxSampleLength * factor));
        return std::nullopt;
    }

    const std::string cacheKey =
        std::format("{}:{}:{}:{}", *samplePath, downsample, static_cast<int>(format), usedLength);
    Operator op;
    if (const auto cached = m_samples.find(cacheKey); cached != m_samples.end()) {
        op = cached->second;
    } else {
        auto uploaded = upload(config, sampleKey, samples, format);
        if (!uploaded) return std::nullopt;
        op = *uploaded;
        m_samples[cacheKey] = op;
    }
    op.loop = loop ? LoopMode::Forward : LoopMode::Off;
    op.loopStart = loop ? static_cast<std::uint32_t>(loopStart / factor) : 0;
    op.sampleRate = static_cast<double>(wav->sampleRate) / static_cast<double>(downsample);

    int root = wav->rootNote.value_or(60);
    if (const auto rootText = config.string(key(prefix, "root"))) {
        if (const auto note = parseNote(*rootText)) {
            root = *note;
        } else {
            config.error(key(prefix, "root"),
                         std::format("'{}' is not a note (like c4 or a#3) or MIDI number", *rootText));
        }
    }
    op.rootFrequency = noteFrequency(root);
    return op;
}

std::optional<Operator> SoundBank::upload(const data::Config& config, std::string_view key,
                                          std::span<const float> samples, SampleFormat format)
{
    std::vector<std::uint8_t> bytes;
    if (format == SampleFormat::Pcm8) {
        for (const float sample : samples) {
            bytes.push_back(static_cast<std::uint8_t>(
                static_cast<std::int8_t>(std::clamp(std::lround(sample * 127.0f), -128L, 127L))));
        }
    } else {
        // Sound RAM holds 16-bit samples little-endian here (the Saturn is big-endian; see docs/SATURN.md).
        for (const float sample : samples) {
            const auto value = static_cast<std::int16_t>(std::clamp(std::lround(sample * 32767.0f), -32768L, 32767L));
            bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
            bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
        }
    }
    const auto address = m_scsp.allocate(static_cast<std::uint32_t>(bytes.size()));
    if (!address) {
        config.error(key, std::format("sound RAM is full ({} more bytes needed)", bytes.size()));
        return std::nullopt;
    }
    m_scsp.write(*address, bytes);
    ++m_stats.samples;
    m_stats.sampleBytes += static_cast<std::uint32_t>(bytes.size());

    Operator op;
    op.address = *address;
    op.format = format;
    op.length = static_cast<std::uint32_t>(samples.size());
    return op;
}

} // namespace gridfin
