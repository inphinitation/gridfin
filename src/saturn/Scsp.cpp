#include "saturn/Scsp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace gridfin::saturn {

namespace {

constexpr int kMaxAttenuation = 1023;
constexpr float kDbPerStep = 0.09375f; // envelope attenuation step
constexpr int kTotalLevelSteps = 4;    // one TL step (0.375 dB) in envelope steps

/// Gain for an attenuation in envelope steps; anything past 96 dB is silent.
struct GainTable {
    std::array<float, 2048> gain{};
    GainTable()
    {
        for (std::size_t i = 0; i < gain.size(); ++i) {
            gain[i] = i >= kMaxAttenuation ? 0.0f : std::pow(10.0f, -static_cast<float>(i) * kDbPerStep / 20.0f);
        }
    }
};
const GainTable kGain;

float gainFor(int attenuation)
{
    return kGain.gain[static_cast<std::size_t>(std::clamp(attenuation, 0, 2047))];
}

/// Envelope steps per output sample for a 5-bit rate. Decay rate 1 takes about two minutes over the
/// full 96 dB, each further rate step is a factor of sqrt(2) faster; attacks are about 15 times faster.
struct RateTable {
    std::array<float, 32> decay{};
    std::array<float, 32> attack{};
    RateTable()
    {
        for (int rate = 1; rate < 32; ++rate) {
            const float decayMs = 118'200.0f * std::exp2(-static_cast<float>(rate - 1) * 0.5f);
            const float attackMs = 8'100.0f * std::exp2(-static_cast<float>(rate - 1) * 0.5f);
            decay[static_cast<std::size_t>(rate)] = kMaxAttenuation / (decayMs * kSoundSampleRate / 1000.0f);
            attack[static_cast<std::size_t>(rate)] = kMaxAttenuation / (attackMs * kSoundSampleRate / 1000.0f);
        }
    }
};
const RateTable kRates;

/// LFOF: 32 frequencies spaced geometrically from 0.17 Hz to 172 Hz.
struct LfoTable {
    std::array<float, 32> phaseStep{}; ///< per output sample
    LfoTable()
    {
        for (std::size_t i = 0; i < phaseStep.size(); ++i) {
            const float hz = 0.17f * std::exp2(static_cast<float>(i) * (std::log2(172.0f / 0.17f) / 31.0f));
            phaseStep[i] = hz / static_cast<float>(kSoundSampleRate);
        }
    }
};
const LfoTable kLfo;

/// One LFO waveform at a phase in [0, 1), as -1..1.
float lfoValue(LfoWave wave, float phase, float noise)
{
    switch (wave) {
    case LfoWave::Saw: return phase * 2.0f - 1.0f;
    case LfoWave::Square: return phase < 0.5f ? 1.0f : -1.0f;
    case LfoWave::Triangle: return phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
    case LfoWave::Noise: return noise;
    }
    return 0.0f;
}

/// DISDL / EFSDL / MVOL style levels: 0 is off, the top value 0 dB, each step below `stepDb` quieter.
float sendGain(std::uint8_t level, std::uint8_t top, float stepDb)
{
    if (level == 0) return 0.0f;
    return std::pow(10.0f, -static_cast<float>(top - std::min(level, top)) * stepDb / 20.0f);
}

} // namespace

// --- Pitch --------------------------------------------------------------------------------------

Pitch Pitch::fromRatio(double ratio) noexcept
{
    const double clamped = std::clamp(ratio, std::exp2(-8.0), std::exp2(7.0) * (2047.0 / 1024.0));
    int octave = static_cast<int>(std::floor(std::log2(clamped)));
    int fns = static_cast<int>(std::lround((clamped / std::exp2(octave) - 1.0) * 1024.0));
    if (fns >= 1024) {
        fns -= 1024;
        ++octave;
    }
    if (octave > 7) {
        octave = 7;
        fns = 1023;
    }
    if (octave < -8) {
        octave = -8;
        fns = 0;
    }
    return {static_cast<std::int8_t>(octave), static_cast<std::uint16_t>(std::clamp(fns, 0, 1023))};
}

double Pitch::ratio() const noexcept
{
    return std::exp2(static_cast<double>(octave)) * (1.0 + static_cast<double>(fns) / 1024.0);
}

// --- Scsp ---------------------------------------------------------------------------------------

Scsp::Scsp() : m_ram(kSoundRamSize, 0) {}

std::optional<std::uint32_t> Scsp::allocate(std::uint32_t bytes)
{
    const std::uint32_t aligned = (bytes + 1u) & ~1u;
    if (aligned > kSoundRamSize - m_ramTop) return std::nullopt;
    const std::uint32_t address = m_ramTop;
    m_ramTop += aligned;
    return address;
}

void Scsp::write(std::uint32_t address, std::span<const std::uint8_t> data)
{
    if (address > kSoundRamSize || data.size() > kSoundRamSize - address) return;
    std::memcpy(m_ram.data() + address, data.data(), data.size());
}

void Scsp::keyOn(int index)
{
    Slot& slot = m_slots[static_cast<std::size_t>(index)];
    slot.position = 0.0;
    slot.output = 0.0f;
    if (slot.regs.attackRate >= 31) {
        slot.envelope = 0.0f;
        slot.phase = EnvelopePhase::Decay1;
    } else {
        slot.envelope = kMaxAttenuation;
        slot.phase = EnvelopePhase::Attack;
    }
}

void Scsp::keyOff(int index)
{
    Slot& slot = m_slots[static_cast<std::size_t>(index)];
    if (slot.phase != EnvelopePhase::Off) slot.phase = EnvelopePhase::Release;
}

float Scsp::readSample(const SlotRegisters& regs, double position) const
{
    const auto length = static_cast<double>(regs.loopEnd);
    if (length <= 0.0) return 0.0f;
    if (regs.loop == LoopMode::Forward && position >= length) {
        const double loopLength = std::max(1.0, length - static_cast<double>(regs.loopStart));
        position =
            static_cast<double>(regs.loopStart) + std::fmod(position - static_cast<double>(regs.loopStart), loopLength);
    }
    position = std::clamp(position, 0.0, length - 1.0);

    const auto index = static_cast<std::uint32_t>(position);
    std::uint32_t next = index + 1;
    if (next >= regs.loopEnd) next = regs.loop == LoopMode::Forward ? regs.loopStart : index;
    const auto fraction = static_cast<float>(position - static_cast<double>(index));

    const auto fetch = [&](std::uint32_t i) -> float {
        if (regs.format == SampleFormat::Pcm8) {
            const std::uint32_t address = regs.startAddress + i;
            if (address >= kSoundRamSize) return 0.0f;
            return static_cast<float>(static_cast<std::int8_t>(m_ram[address])) / 128.0f;
        }
        const std::uint32_t address = regs.startAddress + i * 2;
        if (address + 1 >= kSoundRamSize) return 0.0f;
        const auto value = static_cast<std::int16_t>(m_ram[address] | (m_ram[address + 1] << 8));
        return static_cast<float>(value) / 32768.0f;
    };
    const float a = fetch(index);
    return a + (fetch(next) - a) * fraction;
}

float Scsp::stepSlot(Slot& slot)
{
    const SlotRegisters& regs = slot.regs;

    // LFO
    float lfoPitch = 0.0f, lfoAmp = 0.0f;
    if (regs.pitchLfoDepth || regs.ampLfoDepth) {
        slot.lfoPhase += kLfo.phaseStep[regs.lfoFrequency & 31u];
        if (slot.lfoPhase >= 1.0f) {
            slot.lfoPhase -= 1.0f;
            m_noise = m_noise * 1'103'515'245u + 12'345u;
            slot.lfoNoise = static_cast<float>((m_noise >> 16) & 0x7fff) / 16383.5f - 1.0f;
        }
        if (regs.pitchLfoDepth) {
            // Depth 1..7: up to ±3.4 cents doubled per step (±218 cents at 7).
            const float cents = 3.4f * std::exp2(static_cast<float>(regs.pitchLfoDepth - 1));
            lfoPitch = lfoValue(regs.pitchLfoWave, slot.lfoPhase, slot.lfoNoise) * cents;
        }
        if (regs.ampLfoDepth) {
            // Depth 1..7: up to 2^(depth+1) envelope steps of extra attenuation (24 dB at 7).
            const float steps = std::exp2(static_cast<float>(regs.ampLfoDepth + 1));
            lfoAmp = (lfoValue(regs.ampLfoWave, slot.lfoPhase, slot.lfoNoise) * 0.5f + 0.5f) * steps;
        }
    }

    // Envelope
    switch (slot.phase) {
    case EnvelopePhase::Attack:
        slot.envelope -= kRates.attack[regs.attackRate & 31u];
        if (slot.envelope <= 0.0f) {
            slot.envelope = 0.0f;
            slot.phase = EnvelopePhase::Decay1;
        }
        break;
    case EnvelopePhase::Decay1: {
        slot.envelope += kRates.decay[regs.decay1Rate & 31u];
        const auto level = static_cast<float>((regs.decayLevel & 31u) << 5);
        if (slot.envelope >= level) {
            slot.envelope = std::max(slot.envelope, level);
            slot.phase = EnvelopePhase::Decay2;
        }
        break;
    }
    case EnvelopePhase::Decay2: slot.envelope += kRates.decay[regs.decay2Rate & 31u]; break;
    case EnvelopePhase::Release: slot.envelope += kRates.decay[regs.releaseRate & 31u]; break;
    case EnvelopePhase::Off: return 0.0f;
    }
    if (slot.envelope >= kMaxAttenuation) {
        slot.envelope = kMaxAttenuation;
        if (slot.phase == EnvelopePhase::Release || slot.phase == EnvelopePhase::Decay2) {
            slot.phase = EnvelopePhase::Off;
            slot.output = 0.0f;
            return 0.0f;
        }
    }

    // Sample, with FM as an offset of the read position.
    double readPosition = slot.position;
    if (regs.modulationSource >= 0 && regs.modulationSource < kScspSlots && regs.modulationLevel >= 5) {
        const Slot& source = m_slots[static_cast<std::size_t>(regs.modulationSource)];
        readPosition += static_cast<double>(source.output) * std::exp2(static_cast<double>(regs.modulationLevel - 5));
        if (readPosition < 0.0 && regs.loop == LoopMode::Forward) {
            const double loopLength = std::max(1.0, static_cast<double>(regs.loopEnd - regs.loopStart));
            readPosition += std::ceil(-readPosition / loopLength) * loopLength;
        }
    }
    const float sample = readSample(regs, readPosition);

    const int attenuation = static_cast<int>(slot.envelope + lfoAmp) + regs.totalLevel * kTotalLevelSteps;
    slot.output = sample * gainFor(attenuation);

    // Advance
    double step = regs.pitch.ratio();
    if (lfoPitch != 0.0f) step *= std::exp2(static_cast<double>(lfoPitch) / 1200.0);
    slot.position += step;
    if (slot.position >= static_cast<double>(regs.loopEnd)) {
        if (regs.loop == LoopMode::Forward && regs.loopEnd > regs.loopStart) {
            const auto loopLength = static_cast<double>(regs.loopEnd - regs.loopStart);
            slot.position = static_cast<double>(regs.loopStart)
                            + std::fmod(slot.position - static_cast<double>(regs.loopStart), loopLength);
        } else {
            slot.phase = EnvelopePhase::Off; // a one-shot sample ended
        }
    }
    return slot.output;
}

// --- DSP ----------------------------------------------------------------------------------------

bool Scsp::enableReverb(std::uint32_t bytes, float roomSize, float damping)
{
    // A Schroeder/Freeverb style program: 4 comb filters and 2 allpasses per channel. The delay line
    // lengths keep Freeverb's ratios, scaled to the sound RAM the program is given.
    constexpr std::array<std::uint32_t, 4> kComb = {1116, 1188, 1277, 1356};
    constexpr std::array<std::uint32_t, 2> kAllpass = {556, 441};
    constexpr std::uint32_t kSpread = 23;
    std::uint32_t total = 0;
    for (std::uint32_t length : kComb) total += 2 * length + kSpread;
    for (std::uint32_t length : kAllpass) total += 2 * length + kSpread;

    const std::uint32_t samples = bytes / 2;
    if (samples < total / 8) return false;
    const auto base = allocate(samples * 2);
    if (!base) return false;
    std::memset(m_ram.data() + *base, 0, samples * 2);

    const double scale = static_cast<double>(samples) / static_cast<double>(total);
    std::uint32_t address = *base;
    const auto place = [&](DelayLine& line, std::uint32_t length) {
        line.length = std::max(1u, static_cast<std::uint32_t>(static_cast<double>(length) * scale));
        line.address = address;
        line.index = 0;
        line.filter = 0.0f;
        address += line.length * 2;
    };
    for (std::size_t i = 0; i < 4; ++i) {
        place(m_combs[i], kComb[i]);
        place(m_combs[i + 4], kComb[i] + kSpread);
    }
    for (std::size_t i = 0; i < 2; ++i) {
        place(m_allpasses[i], kAllpass[i]);
        place(m_allpasses[i + 2], kAllpass[i] + kSpread);
    }
    m_roomSize = std::clamp(roomSize, 0.0f, 0.98f);
    m_damping = std::clamp(damping, 0.0f, 1.0f);
    m_reverb = true;
    return true;
}

std::int16_t Scsp::delayRead(const DelayLine& line) const
{
    const std::uint32_t address = line.address + line.index * 2;
    return static_cast<std::int16_t>(m_ram[address] | (m_ram[address + 1] << 8));
}

void Scsp::delayWrite(DelayLine& line, float value)
{
    // The DSP stores its delay lines as 16-bit words in sound RAM.
    const auto word = static_cast<std::int16_t>(std::clamp(value * 32768.0f, -32768.0f, 32767.0f));
    const std::uint32_t address = line.address + line.index * 2;
    m_ram[address] = static_cast<std::uint8_t>(word & 0xff);
    m_ram[address + 1] = static_cast<std::uint8_t>((word >> 8) & 0xff);
    line.index = (line.index + 1) % line.length;
}

float Scsp::reverbChannel(std::span<DelayLine> combs, std::span<DelayLine> allpasses, float input)
{
    float out = 0.0f;
    for (DelayLine& comb : combs) {
        const float delayed = static_cast<float>(delayRead(comb)) / 32768.0f;
        comb.filter = delayed * (1.0f - m_damping) + comb.filter * m_damping;
        out += delayed;
        delayWrite(comb, input + comb.filter * m_roomSize);
    }
    for (DelayLine& allpass : allpasses) {
        const float delayed = static_cast<float>(delayRead(allpass)) / 32768.0f;
        const float result = delayed - out;
        delayWrite(allpass, out + delayed * 0.5f);
        out = result;
    }
    return out;
}

void Scsp::render(std::span<std::int16_t> interleaved)
{
    // Per-slot mixer gains, fixed for the block.
    struct Mix {
        float left, right, effect;
    };
    std::array<Mix, kScspSlots> mix{};
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const SlotRegisters& regs = m_slots[i].regs;
        const float direct = sendGain(regs.directLevel, 7, 6.0f);
        const float panGain =
            (regs.pan & 0x0fu) == 0x0fu ? 0.0f : std::pow(10.0f, -3.0f * static_cast<float>(regs.pan & 0x0fu) / 20.0f);
        const bool attenuateLeft = (regs.pan & 0x10u) != 0;
        mix[i] = {direct * (attenuateLeft ? panGain : 1.0f), direct * (attenuateLeft ? 1.0f : panGain),
                  sendGain(regs.effectLevel, 7, 6.0f)};
    }
    const float master = sendGain(m_masterVolume, 15, 3.0f);
    const float reverbReturn = sendGain(m_reverbLevel, 7, 6.0f) * 0.25f;

    for (std::size_t frame = 0; frame + 1 < interleaved.size(); frame += 2) {
        float left = 0.0f, right = 0.0f, effect = 0.0f;
        for (std::size_t i = 0; i < m_slots.size(); ++i) {
            if (m_slots[i].phase == EnvelopePhase::Off) continue;
            const float out = stepSlot(m_slots[i]);
            left += out * mix[i].left;
            right += out * mix[i].right;
            effect += out * mix[i].effect;
        }
        if (m_reverb) {
            const std::span<DelayLine> combs = m_combs;
            const std::span<DelayLine> allpasses = m_allpasses;
            left += reverbChannel(combs.first(4), allpasses.first(2), effect * 0.1f) * reverbReturn;
            right += reverbChannel(combs.last(4), allpasses.last(2), effect * 0.1f) * reverbReturn;
        }
        // The output stage saturates at 16 bits.
        interleaved[frame] = static_cast<std::int16_t>(std::clamp(left * master * 32767.0f, -32768.0f, 32767.0f));
        interleaved[frame + 1] = static_cast<std::int16_t>(std::clamp(right * master * 32767.0f, -32768.0f, 32767.0f));
    }
}

} // namespace gridfin::saturn
