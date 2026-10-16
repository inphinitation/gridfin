#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// SCSP: the Saturn Custom Sound Processor (Yamaha YMF292).
///
/// 32 slots (voices) play PCM samples out of a 512 KB sound RAM, each with its own
/// envelope, LFO, pan and send levels; a slot can frequency-modulate another
/// (FM). A small DSP adds reverb, using part of sound RAM as its delay lines.
/// Output is 16-bit stereo at 44.1 kHz.
///
/// Register ranges follow the chip (5-bit envelope rates, 10-bit envelope
/// attenuation, OCT/FNS pitch, 0.375 dB total level steps, ...). The exact
/// curves are approximations; see docs/SATURN.md.
namespace gridfin::saturn {

inline constexpr std::uint32_t kSoundRamSize = 512 * 1024;
inline constexpr int kSoundSampleRate = 44100;
inline constexpr int kScspSlots = 32;

enum class SampleFormat : std::uint8_t { Pcm16, Pcm8 };
enum class LoopMode : std::uint8_t { Off, Forward };
enum class LfoWave : std::uint8_t { Saw, Square, Triangle, Noise };
enum class EnvelopePhase : std::uint8_t { Attack, Decay1, Decay2, Release, Off };

/// Playback rate as the SCSP stores it: 2^OCT * (1 + FNS / 1024), relative to 44.1 kHz.
struct Pitch {
    std::int8_t octave = 0; ///< -8..7
    std::uint16_t fns = 0;  ///< 0..1023

    /// The nearest representable rate (clamped to the chip's range).
    [[nodiscard]] static Pitch fromRatio(double ratio) noexcept;
    [[nodiscard]] double ratio() const noexcept;
};

/// The registers of one slot that this model uses.
struct SlotRegisters {
    // Sample
    std::uint32_t startAddress = 0; ///< byte address in sound RAM
    SampleFormat format = SampleFormat::Pcm16;
    std::uint32_t loopStart = 0; ///< in samples
    std::uint32_t loopEnd = 0;   ///< in samples; the sample's length when not looping
    LoopMode loop = LoopMode::Off;
    Pitch pitch;

    // Envelope: rates 0 (never) .. 31 (instant), decay level in 5-bit steps of attenuation.
    std::uint8_t attackRate = 31;
    std::uint8_t decay1Rate = 0;
    std::uint8_t decayLevel = 0;
    std::uint8_t decay2Rate = 0;
    std::uint8_t releaseRate = 20;

    std::uint8_t totalLevel = 0;  ///< attenuation in 0.375 dB steps (0..255)
    std::uint8_t directLevel = 7; ///< DISDL: 0 = off, 7 = 0 dB, 6 dB steps
    std::uint8_t pan = 0;         ///< DIPAN: bits 0-3 attenuate one side in 3 dB steps, bit 4 picks the side
    std::uint8_t effectLevel = 0; ///< EFSDL: send to the DSP, same scale as directLevel

    // LFO
    std::uint8_t lfoFrequency = 0; ///< 0..31, about 0.17 Hz .. 172 Hz
    LfoWave pitchLfoWave = LfoWave::Triangle;
    std::uint8_t pitchLfoDepth = 0; ///< 0..7
    LfoWave ampLfoWave = LfoWave::Triangle;
    std::uint8_t ampLfoDepth = 0; ///< 0..7

    // FM: this slot's sample position is offset by another slot's output.
    std::int8_t modulationSource = -1; ///< slot index, -1 = none
    std::uint8_t modulationLevel = 0;  ///< MDL: 0..4 = off, 5..15 = ±2^(MDL-5) samples
};

class Scsp {
public:
    Scsp();

    // Sound RAM -----------------------------------------------------------------
    /// Reserves bytes (2-byte aligned) in sound RAM, as a sound driver's memory map would.
    [[nodiscard]] std::optional<std::uint32_t> allocate(std::uint32_t bytes);
    void write(std::uint32_t address, std::span<const std::uint8_t> data);
    [[nodiscard]] std::uint32_t ramUsed() const noexcept { return m_ramTop; }
    [[nodiscard]] std::span<const std::uint8_t> ram() const noexcept { return m_ram; }

    // Slots -----------------------------------------------------------------------
    [[nodiscard]] SlotRegisters& slot(int index) { return m_slots[static_cast<std::size_t>(index)].regs; }
    void keyOn(int index);
    void keyOff(int index);
    [[nodiscard]] EnvelopePhase phase(int index) const { return m_slots[static_cast<std::size_t>(index)].phase; }
    /// Current envelope attenuation, 0 (loudest) .. 1023 (silent).
    [[nodiscard]] float attenuation(int index) const { return m_slots[static_cast<std::size_t>(index)].envelope; }

    // DSP and mixer ---------------------------------------------------------------
    /// Sets up the reverb program; its delay lines take `bytes` of sound RAM.
    bool enableReverb(std::uint32_t bytes, float roomSize, float damping);
    void setReverbLevel(std::uint8_t level) noexcept { m_reverbLevel = level; }     ///< 0..7 like EFSDL
    void setMasterVolume(std::uint8_t volume) noexcept { m_masterVolume = volume; } ///< MVOL, 0..15

    /// Renders interleaved 16-bit stereo frames.
    void render(std::span<std::int16_t> interleaved);

private:
    struct Slot {
        SlotRegisters regs;
        EnvelopePhase phase = EnvelopePhase::Off;
        double position = 0.0; ///< in samples
        float envelope = 1023.0f;
        float lfoPhase = 0.0f;
        float lfoNoise = 0.0f;
        float output = 0.0f; ///< last output, -1..1, for FM
    };
    struct DelayLine {
        std::uint32_t address = 0;
        std::uint32_t length = 0;
        std::uint32_t index = 0;
        float filter = 0.0f;
    };

    [[nodiscard]] float readSample(const SlotRegisters& regs, double position) const;
    float stepSlot(Slot& slot);
    [[nodiscard]] std::int16_t delayRead(const DelayLine& line) const;
    void delayWrite(DelayLine& line, float value);
    float reverbChannel(std::span<DelayLine> combs, std::span<DelayLine> allpasses, float input);

    std::vector<std::uint8_t> m_ram;
    std::uint32_t m_ramTop = 0;
    std::array<Slot, kScspSlots> m_slots{};
    std::uint32_t m_noise = 0x1234567u;

    bool m_reverb = false;
    std::array<DelayLine, 8> m_combs{};     ///< 4 per channel
    std::array<DelayLine, 4> m_allpasses{}; ///< 2 per channel
    float m_roomSize = 0.8f;
    float m_damping = 0.3f;
    std::uint8_t m_reverbLevel = 5;
    std::uint8_t m_masterVolume = 15;
};

} // namespace gridfin::saturn
