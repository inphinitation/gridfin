#pragma once

#include "data/Config.hpp"
#include "data/Vfs.hpp"
#include "saturn/Scsp.hpp"
#include "saturn/SoundDriver.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Loads tone definitions (`.tone` files) and their samples into sound RAM.
///
/// A tone file describes one instrument for MML's `@<program>`:
///
///     program = 3
///     sample = ../samples/kick.wav   ; imported: converted to PCM8/PCM16 for the SCSP
///     wave = 1 0.5 0.33              ; or: a single-cycle waveform from harmonic amplitudes
///     level = 24                     ; total level, 0.375 dB steps
///     attack = 31 | decay1 = 0 | decay-level = 0 | decay2 = 0 | release = 14
///     [modulator]                    ; optional second slot for two-operator FM
///
/// See docs/SOUND.md for every key.
namespace gridfin {

/// Parses a note name in MML octave numbering ("c4" is 60, "a#3" is 58) or a MIDI number.
[[nodiscard]] std::optional<int> parseNote(std::string_view text);

struct SoundBankStats {
    int tones = 0;
    int samples = 0;               ///< distinct samples in sound RAM
    std::uint32_t sampleBytes = 0; ///< sound RAM used by them
};

class SoundBank {
public:
    SoundBank(saturn::Scsp& scsp, saturn::SoundDriver& driver) : m_scsp(scsp), m_driver(driver) {}

    /// Loads every `.tone` file under `folder`. Problems are appended to `errors`.
    void loadFolder(const data::Vfs& vfs, std::string_view folder, std::vector<std::string>& errors);

    [[nodiscard]] const SoundBankStats& stats() const noexcept { return m_stats; }

private:
    bool loadTone(const data::Vfs& vfs, const std::string& path, std::vector<std::string>& errors);
    std::optional<saturn::Operator> loadOperator(const data::Vfs& vfs, const std::string& path,
                                                 const data::Config& config, std::string_view prefix);
    std::optional<saturn::Operator> importSample(const data::Vfs& vfs, const std::string& path,
                                                 const data::Config& config, std::string_view prefix);
    std::optional<saturn::Operator> buildWave(const data::Config& config, std::string_view prefix);
    std::optional<saturn::Operator> upload(const data::Config& config, std::string_view key,
                                           std::span<const float> samples, saturn::SampleFormat format);

    saturn::Scsp& m_scsp;
    saturn::SoundDriver& m_driver;
    std::map<std::string, saturn::Operator> m_samples; ///< by source and conversion, so tones share them
    SoundBankStats m_stats;
};

} // namespace gridfin
