#include "game/Music.hpp"

#include "core/Log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <vector>

namespace gridfin {

namespace {

using saturn::LoopMode;
using saturn::Operator;
using saturn::SampleFormat;
using saturn::Tone;

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kDrumRate = 22'050.0; // one-shots at half rate in 8 bits, to save sound RAM

/// A looping single-cycle waveform from harmonic amplitudes, as 16-bit PCM.
std::vector<std::int16_t> singleCycle(std::size_t length, std::span<const double> harmonics)
{
    std::vector<double> wave(length);
    double peak = 0.0;
    for (std::size_t i = 0; i < length; ++i) {
        const double phase = kTau * static_cast<double>(i) / static_cast<double>(length);
        for (std::size_t h = 0; h < harmonics.size(); ++h)
            wave[i] += harmonics[h] * std::sin(phase * static_cast<double>(h + 1));
        peak = std::max(peak, std::abs(wave[i]));
    }
    std::vector<std::int16_t> pcm(length);
    for (std::size_t i = 0; i < length; ++i) pcm[i] = static_cast<std::int16_t>(std::lround(wave[i] / peak * 30'000.0));
    return pcm;
}

/// A deterministic white noise source, -1..1.
struct Noise {
    std::uint32_t state = 0x9e3779b9u;
    double next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 2147483647.5 - 1.0;
    }
};

/// A one-shot drum, generated at kDrumRate, as signed 8-bit PCM.
template <typename Generator>
std::vector<std::int8_t> drum(double seconds, Generator&& generator)
{
    std::vector<std::int8_t> pcm(static_cast<std::size_t>(seconds * kDrumRate));
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double value = generator(static_cast<double>(i) / kDrumRate);
        pcm[i] = static_cast<std::int8_t>(std::clamp(std::lround(value * 127.0), -127L, 127L));
    }
    return pcm;
}

template <typename T>
std::span<const std::uint8_t> bytes(const std::vector<T>& data)
{
    return {reinterpret_cast<const std::uint8_t*>(data.data()), data.size() * sizeof(T)};
}

/// Copies a sample into sound RAM and describes it as an operator.
template <typename T>
Operator upload(saturn::Scsp& scsp, const std::vector<T>& data, bool loop)
{
    Operator op;
    const auto address = scsp.allocate(static_cast<std::uint32_t>(data.size() * sizeof(T)));
    if (!address) {
        log::error("Sound RAM is full");
        return op;
    }
    // Sound RAM holds 16-bit samples little-endian here (the Saturn is big-endian; see docs/SATURN.md).
    scsp.write(*address, bytes(data));
    op.address = *address;
    op.format = sizeof(T) == 1 ? SampleFormat::Pcm8 : SampleFormat::Pcm16;
    op.length = static_cast<std::uint32_t>(data.size());
    op.loop = loop ? LoopMode::Forward : LoopMode::Off;
    if (loop) op.rootFrequency = saturn::kSoundSampleRate / static_cast<double>(data.size()); // one cycle
    return op;
}

// The song: "Gridfin Fields", A minor at 104 bpm. Four bars of intro, then an eight-bar loop.
// Progression: Am F C G | Am F C E.
constexpr std::array<std::string_view, 9> kSong = {
    // Lead: FM electric piano.
    "t104 @1 v13 q7 o4 l4 r1 r1 r1 r1 L"
    " e4. a8 b4 >c4< | a4 g8 f8 e4 c4 | e4. g8 >c4< b4 | a2 g4 r4 |"
    " a8 >c8 e4 d8 c8 <b4 | a4 >c4< a4 f4 | g4. e8 g4 >c4< | b4 a4 g+4 e4",
    // Bass: octave eighths.
    "@0 v14 q6 o2 l8 [a>a<]4 [f>f<]4 >[c>c<]4< [g>g<]4 L"
    " [a>a<]4 [f>f<]4 >[c>c<]4< [g>g<]4 | [a>a<]4 [f>f<]4 >[c>c<]4< [e>e<]4",
    // Pads: three voices of whole-note chords, spread across the stereo field.
    "@2 v11 q8 p-6 o3 l1 a a g g L a a g g a a g g+",
    "@2 v11 q8 o4 l1 c c c <b> L c c c <b> c c c <b>",
    "@2 v11 q8 p6 o4 l1 e f e d L e f e d e f e e",
    // Drums: kick, snare, hats.
    "@3 v15 q8 l8 r1 r1 [crrrcrrr]2 L [crrrcrrr]7 crrrcrcc",
    "@4 v13 q8 l8 r1 r1 r1 r2 r8 ccc L [rrcrrrcr]7 rrcrrccc",
    "@5 v11 q4 p4 l8 r1 r1 [v11c v7c]8 L [v11c v7c]32",
    // A sparse arpeggio that answers the lead in the loop, two octaves up.
    "@1 v12 q3 p-9 o5 l8 r1 r1 r1 r1 L"
    " r2 r8 e a >c< | r1 | r2 r8 g >c e< | r1 | r2 r8 e a >c< | r1 | r2 r8 g >c e< | r2 r8 b >e g+<",
};

} // namespace

Music::Music()
{
    // The 68000's program, work area and sequence data sit at the bottom of sound RAM.
    (void)m_scsp.allocate(kDriverBytes);

    // Samples ---------------------------------------------------------------------------------
    constexpr std::array<double, 10> kBassHarmonics = {1.0, 0.5, 0.33, 0.25, 0.2, 0.12, 0.08, 0.05, 0.03, 0.02};
    constexpr std::array<double, 12> kPadHarmonics = {1.0,  0.45, 0.4,  0.2,  0.25, 0.1,
                                                      0.12, 0.05, 0.06, 0.03, 0.03, 0.02};
    constexpr std::array<double, 1> kSine = {1.0};
    const Operator bass = upload(m_scsp, singleCycle(64, kBassHarmonics), true);
    const Operator pad = upload(m_scsp, singleCycle(128, kPadHarmonics), true);
    const Operator sine = upload(m_scsp, singleCycle(64, kSine), true);

    Noise noise;
    const auto kick = drum(0.35, [](double t) {
        // A sine sweeping from about 155 Hz down to 45 Hz; the phase is the integral of the frequency.
        const double phase = kTau * (45.0 * t + 110.0 / 28.0 * (1.0 - std::exp(-28.0 * t)));
        return std::sin(phase) * std::exp(-7.0 * t) + (t < 0.003 ? 0.4 : 0.0);
    });
    const auto snare = drum(0.22, [&](double t) {
        return 0.9 * noise.next() * std::exp(-16.0 * t) + 0.5 * std::sin(kTau * 185.0 * t) * std::exp(-28.0 * t);
    });
    double previous = 0.0;
    const auto hat = drum(0.07, [&](double t) {
        const double value = noise.next();
        const double highPassed = value - previous; // a first difference keeps the hiss
        previous = value;
        return 0.5 * highPassed * std::exp(-55.0 * t);
    });

    const auto drumOperator = [&](const std::vector<std::int8_t>& data) {
        Operator op = upload(m_scsp, data, false);
        op.sampleRate = kDrumRate;
        op.releaseRate = 14; // let the sample ring out after the gate
        return op;
    };

    // Tones -----------------------------------------------------------------------------------
    Tone bassTone;
    bassTone.carrier = bass;
    bassTone.carrier.decay1Rate = 11;
    bassTone.carrier.releaseRate = 22;
    bassTone.carrier.totalLevel = 26;
    m_driver.addTone(bassTone); // @0

    // FM electric piano: a sine carrier modulated by a sine at the same pitch. The modulator decays
    // faster than the carrier, so the attack is bright and the tail mellow.
    Tone piano;
    piano.carrier = sine;
    piano.carrier.decay1Rate = 9;
    piano.carrier.releaseRate = 18;
    piano.carrier.totalLevel = 30;
    piano.modulator = sine;
    piano.modulator->decay1Rate = 15;
    piano.modulator->decayLevel = 12;
    piano.modulator->decay2Rate = 9;
    piano.modulator->totalLevel = 0;
    piano.modulationLevel = 10;
    piano.effectLevel = 5;
    piano.lfoFrequency = 17;
    piano.pitchLfoDepth = 1;
    m_driver.addTone(piano); // @1

    Tone padTone;
    padTone.carrier = pad;
    padTone.carrier.attackRate = 10;
    padTone.carrier.releaseRate = 15;
    padTone.carrier.totalLevel = 28;
    padTone.effectLevel = 6;
    padTone.lfoFrequency = 14;
    padTone.pitchLfoDepth = 2;
    m_driver.addTone(padTone); // @2

    Tone kickTone;
    kickTone.carrier = drumOperator(kick);
    kickTone.carrier.totalLevel = 24;
    kickTone.fixedPitch = true;
    m_driver.addTone(kickTone); // @3

    Tone snareTone;
    snareTone.carrier = drumOperator(snare);
    snareTone.carrier.totalLevel = 4;
    snareTone.effectLevel = 4;
    snareTone.fixedPitch = true;
    m_driver.addTone(snareTone); // @4

    Tone hatTone;
    hatTone.carrier = drumOperator(hat);
    hatTone.carrier.totalLevel = 0;
    hatTone.fixedPitch = true;
    m_driver.addTone(hatTone); // @5

    // DSP: a reverb whose delay lines take the next stretch of sound RAM.
    if (!m_scsp.enableReverb(kReverbBytes, 0.82f, 0.35f)) log::warn("No sound RAM left for the reverb");
    m_scsp.setReverbLevel(6);

    auto song = saturn::compileSong(kSong);
    if (!song) {
        log::error("Could not compile the song: {}", song.error);
        return;
    }
    m_driver.play(std::move(*song));
}

} // namespace gridfin
