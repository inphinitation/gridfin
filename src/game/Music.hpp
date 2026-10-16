#pragma once

#include "data/Vfs.hpp"
#include "game/SoundBank.hpp"
#include "saturn/Scsp.hpp"
#include "saturn/SoundDriver.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace gridfin {

/// The game's sound program: the SCSP, the sound driver, the tones and samples loaded into
/// sound RAM and the background song, all read from the game data (see gridfin.cfg).
/// Not thread safe; the audio output serialises access.
class Music {
public:
    /// Loads the tones and song named by the manifest. Problems are collected in errors();
    /// whatever loaded still plays.
    explicit Music(const data::Vfs& vfs, std::string_view manifest = "gridfin.cfg");

    /// Renders interleaved 16-bit stereo at saturn::kSoundSampleRate.
    void render(std::span<std::int16_t> interleaved) { m_driver.render(interleaved); }

    void setPlaying(bool playing) { m_driver.setPaused(!playing); }
    [[nodiscard]] bool playing() const noexcept { return !m_driver.paused(); }

    [[nodiscard]] const saturn::Scsp& scsp() const noexcept { return m_scsp; }
    [[nodiscard]] const saturn::SoundDriver& driver() const noexcept { return m_driver; }
    [[nodiscard]] const SoundBankStats& bank() const noexcept { return m_bank.stats(); }
    [[nodiscard]] const std::vector<std::string>& errors() const noexcept { return m_errors; }

    /// Bytes of sound RAM reserved for the 68000's program and sequence data.
    static constexpr std::uint32_t kDriverBytes = 64 * 1024;

private:
    saturn::Scsp m_scsp;
    saturn::SoundDriver m_driver{m_scsp};
    SoundBank m_bank{m_scsp, m_driver};
    std::vector<std::string> m_errors;
};

} // namespace gridfin
