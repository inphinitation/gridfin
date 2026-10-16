#pragma once

#include "saturn/Scsp.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The 68000 side of Saturn audio: a sound driver that plays sequences on the SCSP.
///
/// On the console the 68000 ran a driver (Sega's, or the game's own) that read sequence data
/// from sound RAM, advanced it on a timer and wrote key-on/off and pitch to the SCSP slots.
/// This driver does the same with sequences compiled from MML (Music Macro Language).
namespace gridfin::saturn {

/// Ticks per quarter note.
inline constexpr int kTicksPerQuarter = 48;

/// One slot's worth of sound: a sample with its envelope and level.
struct Operator {
    std::uint32_t address = 0;
    SampleFormat format = SampleFormat::Pcm16;
    std::uint32_t length = 0; ///< in samples
    std::uint32_t loopStart = 0;
    LoopMode loop = LoopMode::Off;
    /// The sample's playback rate (Hz) and the pitch (Hz) it sounds at when played at that rate.
    double sampleRate = kSoundSampleRate;
    double rootFrequency = 261.6256;
    double frequencyRatio = 1.0; ///< FM modulators often run at a multiple of the note

    std::uint8_t attackRate = 31, decay1Rate = 0, decayLevel = 0, decay2Rate = 0, releaseRate = 20;
    std::uint8_t totalLevel = 0;
};

/// An instrument ("tone" in Sega's driver): a carrier slot, optionally modulated by a second slot.
struct Tone {
    Operator carrier;
    std::optional<Operator> modulator;
    std::uint8_t modulationLevel = 0; ///< MDL of the carrier

    std::uint8_t pan = 0;         ///< DIPAN
    std::uint8_t effectLevel = 0; ///< EFSDL (reverb send)
    std::uint8_t lfoFrequency = 0;
    std::uint8_t pitchLfoDepth = 0;
    std::uint8_t ampLfoDepth = 0;
    /// Fixed-pitch tones (drums) ignore the note and always play their root.
    bool fixedPitch = false;
};

/// A compiled track: a flat event list (repeats are unrolled) with an optional loop point.
struct TrackEvent {
    enum class Type : std::uint8_t { Note, Rest, Tone, Volume, Gate, Tempo, Pan };
    Type type = Type::Rest;
    std::uint16_t ticks = 0; ///< Note / Rest: length
    std::int16_t value = 0;  ///< Note: MIDI note number; otherwise the parameter
};

struct Track {
    std::vector<TrackEvent> events;
    std::optional<std::size_t> loopIndex; ///< where `L` was, if the track loops
    std::uint32_t lengthTicks = 0;        ///< ticks from the start to the end of the track
};

struct Sequence {
    std::vector<Track> tracks;
    int tempo = 120; ///< quarter notes per minute
};

/// A compile error.
struct Failure {
    std::string message;
    std::size_t offset = 0; ///< where in the MML text
};

/// The result of compiling MML: a value, or an error message.
template <typename T>
struct CompileResult {
    CompileResult(T result) : value(std::move(result)) {} // NOLINT(google-explicit-constructor)
    CompileResult(Failure failure)                        // NOLINT(google-explicit-constructor)
        : error(std::move(failure.message)), errorOffset(failure.offset)
    {}

    explicit operator bool() const noexcept { return value.has_value(); }
    T& operator*() { return *value; }
    const T& operator*() const { return *value; }
    T* operator->() { return &*value; }
    const T* operator->() const { return &*value; }

    std::optional<T> value;
    std::string error;
    std::size_t errorOffset = 0;
};

/// Compiles one MML track.
///
/// Commands: `c d e f g a b` notes with `+`/`#` (sharp) or `-` (flat), an optional length (1, 2, 4,
/// 8, 16, 32, 64, also 3, 6, 12, 24, 48) and dots; `r` rest; `o<n>` octave (4 holds middle C),
/// `<` / `>` octave down / up; `l<n>` default length; `v<n>` volume 0..15; `@<n>` tone; `q<n>` gate
/// in eighths of the note (8 = legato); `t<n>` tempo; `p<n>` pan -15..15; `[ ... ]<n>` repeat n times;
/// `L` loop point. Spaces, `|` and `;` comments to the end of a line are ignored.
[[nodiscard]] CompileResult<Track> compileMml(std::string_view mml);

/// Compiles a song from one MML string per track. The first `t` command sets the starting tempo.
[[nodiscard]] CompileResult<Sequence> compileSong(std::span<const std::string_view> tracks);

/// Compiles a song file: tracks start with a `#track <name>` line; before the first one only
/// comments and `#title <text>` may appear. A file without `#track` lines is a single track.
/// Errors are reported as "<line>:<column>: <message>".
[[nodiscard]] CompileResult<Sequence> compileSongFile(std::string_view text);

struct SoundDriverStats {
    int activeSlots = 0; ///< slots not in the Off phase
    int steals = 0;      ///< notes that took a slot from another note since the song started
};

class SoundDriver {
public:
    explicit SoundDriver(Scsp& scsp) : m_scsp(scsp) {}

    /// Registers a tone and returns its number for `@`.
    int addTone(const Tone& tone);
    /// Sets the tone for a program number (`@<program>`); unset programs are silent.
    void setTone(int program, const Tone& tone);
    [[nodiscard]] bool hasTone(int program) const;
    [[nodiscard]] const std::vector<Tone>& tones() const noexcept { return m_tones; }

    void play(Sequence sequence);
    void stop();
    /// Pausing releases sounding notes; resuming continues where the sequence was.
    void setPaused(bool paused);
    [[nodiscard]] bool paused() const noexcept { return m_paused; }
    [[nodiscard]] bool playing() const noexcept { return !m_sequence.tracks.empty(); }
    [[nodiscard]] std::uint64_t tick() const noexcept { return m_tick; }

    /// Advances the sequencer and renders interleaved stereo frames from the SCSP.
    void render(std::span<std::int16_t> interleaved);

    [[nodiscard]] SoundDriverStats stats() const;

    /// Picks a slot for a new note, taking one from an older note when all 32 are busy.
    /// Exposed for tests.
    int allocateSlot(int avoid = -1);

private:
    struct TrackState {
        std::size_t position = 0;
        std::uint32_t wait = 0;     ///< ticks until the next event
        std::uint32_t gateLeft = 0; ///< ticks until the sounding note is released
        int tone = 0;
        int volume = 15;
        int gate = 7;
        int pan = 0;
        std::array<int, 2> slots{-1, -1}; ///< carrier, modulator
        bool finished = false;
    };

    void step();
    void runTrack(std::size_t index);
    void startNote(TrackState& state, std::size_t trackIndex, int note);
    void releaseNote(TrackState& state);
    void releaseAll();

    Scsp& m_scsp;
    std::vector<Tone> m_tones;
    Sequence m_sequence;
    std::vector<TrackState> m_states;
    std::array<std::uint64_t, kScspSlots> m_slotStarted{}; ///< tick a slot was keyed on
    std::array<int, kScspSlots> m_slotOwner{};             ///< track + 1, 0 = none
    std::uint64_t m_tick = 0;
    double m_samplesToTick = 0.0;
    int m_tempo = 120;
    int m_steals = 0;
    bool m_paused = false;
};

} // namespace gridfin::saturn
