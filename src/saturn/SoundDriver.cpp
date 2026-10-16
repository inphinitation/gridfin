#include "saturn/SoundDriver.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>

namespace gridfin::saturn {

namespace {

constexpr int kWholeNote = kTicksPerQuarter * 4;
constexpr std::size_t kMaxEvents = 1u << 16; // unrolled repeats can't grow without bound

class MmlParser {
public:
    explicit MmlParser(std::string_view text) : m_text(text) {}

    CompileResult<Track> parse()
    {
        Track track;
        while (true) {
            skipSpace();
            if (m_pos >= m_text.size()) break;
            const char c = lower(m_text[m_pos++]);
            switch (c) {
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'g':
            case 'a':
            case 'b': {
                constexpr std::array<int, 7> kSemitone = {9, 11, 0, 2, 4, 5, 7}; // a..g
                int note = (m_octave + 1) * 12 + kSemitone[static_cast<std::size_t>(c - 'a')];
                while (m_pos < m_text.size()) {
                    if (m_text[m_pos] == '+' || m_text[m_pos] == '#') {
                        ++note;
                    } else if (m_text[m_pos] == '-') {
                        --note;
                    } else {
                        break;
                    }
                    ++m_pos;
                }
                const auto ticks = length();
                if (!ticks) return m_failure;
                if (note < 0 || note > 127) return fail("note out of range");
                track.events.push_back({TrackEvent::Type::Note, *ticks, static_cast<std::int16_t>(note)});
                track.lengthTicks += *ticks;
                break;
            }
            case 'r': {
                const auto ticks = length();
                if (!ticks) return m_failure;
                track.events.push_back({TrackEvent::Type::Rest, *ticks, 0});
                track.lengthTicks += *ticks;
                break;
            }
            case 'o': {
                const auto value = number(0, 8);
                if (!value) return m_failure;
                m_octave = *value;
                break;
            }
            case '<': --m_octave; break;
            case '>': ++m_octave; break;
            case 'l': {
                const auto ticks = length(/*required=*/true);
                if (!ticks) return m_failure;
                m_defaultLength = *ticks;
                break;
            }
            case 'v':
                if (!parameter(track, TrackEvent::Type::Volume, 0, 15)) return m_failure;
                break;
            case '@':
                if (!parameter(track, TrackEvent::Type::Tone, 0, 255)) return m_failure;
                break;
            case 'q':
                if (!parameter(track, TrackEvent::Type::Gate, 1, 8)) return m_failure;
                break;
            case 't':
                if (!parameter(track, TrackEvent::Type::Tempo, 20, 300)) return m_failure;
                break;
            case 'p':
                if (!parameter(track, TrackEvent::Type::Pan, -15, 15)) return m_failure;
                break;
            case '[': m_repeats.push_back({m_pos, -1}); break;
            case ']': {
                if (m_repeats.empty()) return fail("']' without '['");
                const auto count = optionalNumber(1, 255);
                if (!count) return m_failure;
                Repeat& repeat = m_repeats.back();
                if (repeat.remaining < 0) repeat.remaining = (*count ? *count : 2) - 1;
                if (repeat.remaining > 0) {
                    --repeat.remaining;
                    m_pos = repeat.start;
                } else {
                    m_repeats.pop_back();
                }
                break;
            }
            case 'L':
                if (!m_repeats.empty()) return fail("loop point inside a repeat");
                if (track.loopIndex) return fail("second loop point");
                track.loopIndex = track.events.size();
                m_loopTicks = track.lengthTicks;
                break;
            default: --m_pos; return fail(std::format("unexpected '{}'", m_text[m_pos]));
            }
            if (track.events.size() > kMaxEvents) return fail("too many events");
        }
        if (!m_repeats.empty()) return fail("'[' without ']'");
        if (track.loopIndex && track.lengthTicks == m_loopTicks) return fail("the loop has no notes or rests");
        return track;
    }

private:
    struct Repeat {
        std::size_t start;
        int remaining;
    };

    /// Commands are case-insensitive except `L` (loop point) and `l` (length).
    static char lower(char c)
    {
        return c == 'L' ? 'L' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    Failure fail(std::string message)
    {
        m_failure = Failure{std::move(message), m_pos};
        return m_failure;
    }

    void skipSpace()
    {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == ';') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
            } else if (std::isspace(static_cast<unsigned char>(c)) || c == '|') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    /// Reads digits if there are any: nullopt on error, 0 when absent.
    std::optional<int> optionalNumber(int minimum, int maximum)
    {
        bool negative = false;
        if (m_pos < m_text.size() && m_text[m_pos] == '-' && minimum < 0) {
            negative = true;
            ++m_pos;
        }
        const char* begin = m_text.data() + m_pos;
        int value = 0;
        const auto [end, error] = std::from_chars(begin, m_text.data() + m_text.size(), value);
        if (error != std::errc{}) {
            if (negative) {
                fail("expected a number");
                return std::nullopt;
            }
            return 0;
        }
        m_pos += static_cast<std::size_t>(end - begin);
        if (negative) value = -value;
        if (value < minimum || value > maximum) {
            fail(std::format("{} is outside {}..{}", value, minimum, maximum));
            return std::nullopt;
        }
        return value;
    }

    std::optional<int> number(int minimum, int maximum)
    {
        if (m_pos >= m_text.size()
            || !(std::isdigit(static_cast<unsigned char>(m_text[m_pos])) || (m_text[m_pos] == '-' && minimum < 0))) {
            fail("expected a number");
            return std::nullopt;
        }
        return optionalNumber(minimum, maximum);
    }

    bool parameter(Track& track, TrackEvent::Type type, int minimum, int maximum)
    {
        const auto value = number(minimum, maximum);
        if (!value) return false;
        track.events.push_back({type, 0, static_cast<std::int16_t>(*value)});
        return true;
    }

    /// A note length (`4`, `8.`, ...) in ticks; without digits, the default length (plus dots).
    std::optional<std::uint16_t> length(bool required = false)
    {
        int ticks = m_defaultLength;
        if (m_pos < m_text.size() && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
            const auto divisor = optionalNumber(1, kWholeNote);
            if (!divisor) return std::nullopt;
            if (kWholeNote % *divisor != 0) {
                fail(std::format("length {} is not a whole number of ticks", *divisor));
                return std::nullopt;
            }
            ticks = kWholeNote / *divisor;
        } else if (required) {
            fail("expected a length");
            return std::nullopt;
        }
        int dot = ticks;
        while (m_pos < m_text.size() && m_text[m_pos] == '.') {
            if (dot % 2 != 0) {
                fail("too many dots");
                return std::nullopt;
            }
            dot /= 2;
            ticks += dot;
            ++m_pos;
        }
        return static_cast<std::uint16_t>(ticks);
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_octave = 4;
    std::uint16_t m_defaultLength = kTicksPerQuarter;
    std::uint32_t m_loopTicks = 0;
    std::vector<Repeat> m_repeats;
    Failure m_failure;
};

double noteFrequency(int note)
{
    return 440.0 * std::exp2(static_cast<double>(note - 69) / 12.0);
}

void configureOperator(SlotRegisters& regs, const Operator& op, double frequency, int extraLevel)
{
    regs.startAddress = op.address;
    regs.format = op.format;
    regs.loopStart = op.loopStart;
    regs.loopEnd = op.length;
    regs.loop = op.loop;
    regs.pitch = Pitch::fromRatio(op.sampleRate / kSoundSampleRate * frequency * op.frequencyRatio / op.rootFrequency);
    regs.attackRate = op.attackRate;
    regs.decay1Rate = op.decay1Rate;
    regs.decayLevel = op.decayLevel;
    regs.decay2Rate = op.decay2Rate;
    regs.releaseRate = op.releaseRate;
    regs.totalLevel = static_cast<std::uint8_t>(std::clamp(op.totalLevel + extraLevel, 0, 255));
    regs.modulationSource = -1;
    regs.modulationLevel = 0;
    regs.lfoFrequency = 0;
    regs.pitchLfoDepth = 0;
    regs.ampLfoDepth = 0;
}

} // namespace

CompileResult<Track> compileMml(std::string_view mml)
{
    return MmlParser(mml).parse();
}

CompileResult<Sequence> compileSong(std::span<const std::string_view> tracks)
{
    Sequence sequence;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto track = compileMml(tracks[i]);
        if (!track) return Failure{std::format("track {}: {}", i, track.error), track.errorOffset};
        for (const TrackEvent& event : track->events) {
            if (event.type == TrackEvent::Type::Note || event.type == TrackEvent::Type::Rest) break;
            if (event.type == TrackEvent::Type::Tempo) sequence.tempo = event.value;
        }
        sequence.tracks.push_back(std::move(*track));
    }
    return sequence;
}

CompileResult<Sequence> compileSongFile(std::string_view text)
{
    const auto location = [&](std::size_t offset) {
        offset = std::min(offset, text.size());
        const auto line = std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n') + 1;
        const std::size_t lineStart = text.rfind('\n', offset == 0 ? 0 : offset - 1);
        const std::size_t column = lineStart == std::string_view::npos || offset == 0 ? offset + 1 : offset - lineStart;
        return std::format("{}:{}", line, column);
    };

    struct Part {
        std::size_t begin, end;
    };
    std::vector<Part> tracks;
    std::size_t pos = 0;
    bool sawTrack = false;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        if (line.starts_with("#track")) {
            if (!tracks.empty()) tracks.back().end = pos;
            tracks.push_back({end, text.size()});
            sawTrack = true;
        } else if (line.starts_with("#title")) {
            // Metadata; the title isn't used yet.
        } else if (line.starts_with("#")) {
            return Failure{std::format("{}: unknown directive", location(pos)), pos};
        } else if (!sawTrack) {
            const std::size_t content = line.find_first_not_of(" \t\r");
            if (content != std::string_view::npos && line[content] != ';') {
                // No #track header at all: the whole file is one track.
                tracks.clear();
                break;
            }
        }
        pos = end + 1;
    }
    if (tracks.empty()) tracks.push_back({0, text.size()});
    // Directives inside a track's body belong to the parser's view of the text, so hide them.
    std::vector<std::string> bodies;
    for (const Part& part : tracks) {
        std::string body(text.substr(part.begin, part.end - part.begin));
        for (std::size_t i = 0; i < body.size(); ++i) {
            if ((i == 0 || body[i - 1] == '\n') && body[i] == '#') {
                while (i < body.size() && body[i] != '\n') body[i++] = ' ';
            }
        }
        bodies.push_back(std::move(body));
    }

    Sequence sequence;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        auto track = compileMml(bodies[i]);
        if (!track) {
            const std::size_t offset = tracks[i].begin + track.errorOffset;
            return Failure{std::format("{}: {}", location(offset), track.error), offset};
        }
        for (const TrackEvent& event : track->events) {
            if (event.type == TrackEvent::Type::Note || event.type == TrackEvent::Type::Rest) break;
            if (event.type == TrackEvent::Type::Tempo) sequence.tempo = event.value;
        }
        sequence.tracks.push_back(std::move(*track));
    }
    return sequence;
}

// --- Driver -------------------------------------------------------------------------------------

int SoundDriver::addTone(const Tone& tone)
{
    m_tones.push_back(tone);
    return static_cast<int>(m_tones.size()) - 1;
}

void SoundDriver::setTone(int program, const Tone& tone)
{
    if (program < 0) return;
    const auto index = static_cast<std::size_t>(program);
    if (m_tones.size() <= index) m_tones.resize(index + 1);
    m_tones[index] = tone;
}

bool SoundDriver::hasTone(int program) const
{
    return program >= 0 && static_cast<std::size_t>(program) < m_tones.size()
           && m_tones[static_cast<std::size_t>(program)].carrier.length > 0;
}

void SoundDriver::play(Sequence sequence)
{
    stop();
    m_sequence = std::move(sequence);
    m_states.assign(m_sequence.tracks.size(), TrackState{});
    m_tempo = m_sequence.tempo;
    m_tick = 0;
    m_samplesToTick = 0.0;
    m_steals = 0;
}

void SoundDriver::stop()
{
    releaseAll();
    m_sequence = {};
    m_states.clear();
}

void SoundDriver::setPaused(bool paused)
{
    if (paused && !m_paused) releaseAll();
    m_paused = paused;
}

void SoundDriver::releaseAll()
{
    for (TrackState& state : m_states) releaseNote(state);
}

int SoundDriver::allocateSlot(int avoid)
{
    // A free slot, else the quietest released one, else the oldest held note.
    int best = -1;
    for (int i = 0; i < kScspSlots; ++i) {
        if (i != avoid && m_scsp.phase(i) == EnvelopePhase::Off && m_slotOwner[static_cast<std::size_t>(i)] == 0) {
            return i;
        }
    }
    for (int i = 0; i < kScspSlots; ++i) {
        if (i == avoid || m_scsp.phase(i) == EnvelopePhase::Off) continue;
        const bool released = m_scsp.phase(i) == EnvelopePhase::Release;
        if (!released) continue;
        if (best < 0 || m_scsp.attenuation(i) > m_scsp.attenuation(best)) best = i;
    }
    if (best < 0) {
        for (int i = 0; i < kScspSlots; ++i) {
            if (i == avoid) continue;
            if (best < 0
                || m_slotStarted[static_cast<std::size_t>(i)] < m_slotStarted[static_cast<std::size_t>(best)]) {
                best = i;
            }
        }
    }
    if (m_scsp.phase(best) != EnvelopePhase::Off) ++m_steals;

    // The note that had this slot no longer owns it.
    const int owner = m_slotOwner[static_cast<std::size_t>(best)];
    if (owner > 0 && static_cast<std::size_t>(owner - 1) < m_states.size()) {
        for (int& slot : m_states[static_cast<std::size_t>(owner - 1)].slots) {
            if (slot == best) slot = -1;
        }
    }
    m_slotOwner[static_cast<std::size_t>(best)] = 0;
    m_scsp.keyOff(best);
    return best;
}

void SoundDriver::releaseNote(TrackState& state)
{
    for (int& slot : state.slots) {
        if (slot >= 0) {
            m_scsp.keyOff(slot);
            m_slotOwner[static_cast<std::size_t>(slot)] = 0;
        }
        slot = -1;
    }
    state.gateLeft = 0;
}

void SoundDriver::startNote(TrackState& state, std::size_t trackIndex, int note)
{
    releaseNote(state);
    if (state.tone < 0 || static_cast<std::size_t>(state.tone) >= m_tones.size()) return;
    const Tone& tone = m_tones[static_cast<std::size_t>(state.tone)];
    if (tone.carrier.length == 0) return; // no tone loaded for this program
    const double frequency = tone.fixedPitch ? tone.carrier.rootFrequency : noteFrequency(note);
    const auto owner = static_cast<int>(trackIndex) + 1;

    const int carrier = allocateSlot();
    m_slotOwner[static_cast<std::size_t>(carrier)] = owner;
    m_slotStarted[static_cast<std::size_t>(carrier)] = m_tick;
    state.slots[0] = carrier;
    int modulator = -1;
    if (tone.modulator) {
        modulator = allocateSlot(carrier);
        m_slotOwner[static_cast<std::size_t>(modulator)] = owner;
        m_slotStarted[static_cast<std::size_t>(modulator)] = m_tick;
        state.slots[1] = modulator;
    }

    // Volume: 3 dB per step below 15 (8 TL steps of 0.375 dB).
    const int volumeLevel = (15 - state.volume) * 8;
    SlotRegisters& regs = m_scsp.slot(carrier);
    configureOperator(regs, tone.carrier, frequency, volumeLevel);
    regs.directLevel = 7;
    regs.effectLevel = tone.effectLevel;
    if (state.pan != 0) {
        // Negative pans left by attenuating the right side; positive pans right.
        regs.pan = static_cast<std::uint8_t>(std::abs(state.pan) | (state.pan > 0 ? 0x10 : 0x00));
    } else {
        regs.pan = tone.pan;
    }
    regs.lfoFrequency = tone.lfoFrequency;
    regs.pitchLfoDepth = tone.pitchLfoDepth;
    regs.ampLfoDepth = tone.ampLfoDepth;

    if (modulator >= 0) {
        SlotRegisters& mod = m_scsp.slot(modulator);
        configureOperator(mod, *tone.modulator, frequency, 0);
        mod.directLevel = 0; // heard only through the carrier
        mod.effectLevel = 0;
        regs.modulationSource = static_cast<std::int8_t>(modulator);
        regs.modulationLevel = tone.modulationLevel;
        m_scsp.keyOn(modulator);
    }
    m_scsp.keyOn(carrier);
}

void SoundDriver::runTrack(std::size_t index)
{
    TrackState& state = m_states[index];
    const Track& track = m_sequence.tracks[index];
    if (state.gateLeft > 0 && --state.gateLeft == 0) releaseNote(state);
    if (state.finished) return;
    if (state.wait > 0 && --state.wait > 0) return;

    while (state.wait == 0) {
        if (state.position >= track.events.size()) {
            if (!track.loopIndex) {
                state.finished = true;
                return;
            }
            state.position = *track.loopIndex;
        }
        const TrackEvent& event = track.events[state.position++];
        switch (event.type) {
        case TrackEvent::Type::Note: {
            startNote(state, index, event.value);
            state.wait = event.ticks;
            state.gateLeft = std::max<std::uint32_t>(1, event.ticks * static_cast<std::uint32_t>(state.gate) / 8);
            break;
        }
        case TrackEvent::Type::Rest: state.wait = event.ticks; break;
        case TrackEvent::Type::Tone: state.tone = event.value; break;
        case TrackEvent::Type::Volume: state.volume = event.value; break;
        case TrackEvent::Type::Gate: state.gate = event.value; break;
        case TrackEvent::Type::Tempo: m_tempo = event.value; break;
        case TrackEvent::Type::Pan: state.pan = event.value; break;
        }
    }
}

void SoundDriver::step()
{
    for (std::size_t i = 0; i < m_states.size(); ++i) runTrack(i);
    ++m_tick;
}

void SoundDriver::render(std::span<std::int16_t> interleaved)
{
    std::size_t frames = interleaved.size() / 2;
    std::size_t offset = 0;
    while (frames > 0) {
        std::size_t chunk = frames;
        if (playing() && !m_paused) {
            if (m_samplesToTick <= 0.0) {
                step();
                m_samplesToTick += static_cast<double>(kSoundSampleRate) * 60.0 / (m_tempo * kTicksPerQuarter);
            }
            chunk = std::min(frames, static_cast<std::size_t>(std::ceil(m_samplesToTick)));
            m_samplesToTick -= static_cast<double>(chunk);
        }
        m_scsp.render(interleaved.subspan(offset * 2, chunk * 2));
        offset += chunk;
        frames -= chunk;
    }
}

SoundDriverStats SoundDriver::stats() const
{
    SoundDriverStats stats;
    for (int i = 0; i < kScspSlots; ++i) {
        if (m_scsp.phase(i) != EnvelopePhase::Off) ++stats.activeSlots;
    }
    stats.steals = m_steals;
    return stats;
}

} // namespace gridfin::saturn
