#pragma once

#include "saturn/Scsp.hpp"
#include "saturn/SoundDriver.hpp"

#include <cstdint>
#include <span>

namespace gridfin {

/// The game's sound program: the SCSP, the sound driver, the samples it loads into sound RAM
/// and the background song. Not thread safe; the audio output serialises access.
class Music {
public:
    Music();

    /// Renders interleaved 16-bit stereo at saturn::kSoundSampleRate.
    void render(std::span<std::int16_t> interleaved) { m_driver.render(interleaved); }

    void setPlaying(bool playing) { m_driver.setPaused(!playing); }
    [[nodiscard]] bool playing() const noexcept { return !m_driver.paused(); }

    [[nodiscard]] const saturn::Scsp& scsp() const noexcept { return m_scsp; }
    [[nodiscard]] const saturn::SoundDriver& driver() const noexcept { return m_driver; }

    /// Bytes of sound RAM reserved for the 68000's program and sequence data.
    static constexpr std::uint32_t kDriverBytes = 64 * 1024;
    /// Bytes of sound RAM given to the DSP's reverb delay lines.
    static constexpr std::uint32_t kReverbBytes = 32 * 1024;

private:
    saturn::Scsp m_scsp;
    saturn::SoundDriver m_driver{m_scsp};
};

} // namespace gridfin
