#include "game/Music.hpp"
#include "saturn/Scsp.hpp"
#include "saturn/SoundDriver.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <numbers>
#include <vector>

using namespace gridfin;
using namespace gridfin::saturn;

namespace {

/// Loads a 64-sample 16-bit sine cycle and returns an operator that plays it.
Operator sineOperator(Scsp& scsp)
{
    std::vector<std::uint8_t> bytes;
    for (int i = 0; i < 64; ++i) {
        const auto value = static_cast<std::int16_t>(
            std::lround(30'000.0 * std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / 64.0)));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }
    const auto address = scsp.allocate(static_cast<std::uint32_t>(bytes.size()));
    REQUIRE(address);
    scsp.write(*address, bytes);
    Operator op;
    op.address = *address;
    op.length = 64;
    op.loop = LoopMode::Forward;
    op.rootFrequency = kSoundSampleRate / 64.0;
    return op;
}

/// Counts rising zero crossings in the left channel.
int risingCrossings(const std::vector<std::int16_t>& stereo)
{
    int count = 0;
    for (std::size_t i = 2; i < stereo.size(); i += 2) {
        if (stereo[i - 2] < 0 && stereo[i] >= 0) ++count;
    }
    return count;
}

double rms(std::span<const std::int16_t> samples)
{
    double sum = 0.0;
    for (const std::int16_t s : samples) sum += static_cast<double>(s) * s;
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

} // namespace

TEST_CASE("Pitch registers quantise to OCT/FNS steps")
{
    const Pitch unity = Pitch::fromRatio(1.0);
    CHECK(unity.octave == 0);
    CHECK(unity.fns == 0);

    const Pitch fifth = Pitch::fromRatio(1.5);
    CHECK(fifth.octave == 0);
    CHECK(fifth.fns == 512);

    const Pitch low = Pitch::fromRatio(0.3);
    CHECK(low.octave == -2);
    CHECK(low.ratio() == doctest::Approx(0.3).epsilon(0.001));

    // Out of range rates clamp to what the registers can hold.
    CHECK(Pitch::fromRatio(1e-6).octave == -8);
    CHECK(Pitch::fromRatio(1000.0).octave == 7);
    CHECK(Pitch::fromRatio(1000.0).fns == 1023);
}

TEST_CASE("Sound RAM is a 512 KB budget")
{
    Scsp scsp;
    CHECK(scsp.allocate(3) == 0u);
    CHECK(scsp.ramUsed() == 4); // allocations stay 16-bit aligned
    CHECK(scsp.allocate(kSoundRamSize).has_value() == false);
    CHECK(scsp.allocate(kSoundRamSize - 4).has_value());
    CHECK(scsp.allocate(2).has_value() == false);
}

TEST_CASE("A slot plays its sample at the programmed pitch")
{
    Scsp scsp;
    const Operator sine = sineOperator(scsp);
    SlotRegisters& regs = scsp.slot(0);
    regs.startAddress = sine.address;
    regs.loopEnd = 64;
    regs.loop = LoopMode::Forward;
    regs.pitch = Pitch::fromRatio(440.0 * 64.0 / kSoundSampleRate);
    scsp.keyOn(0);

    std::vector<std::int16_t> out(kSoundSampleRate * 2); // one second
    scsp.render(out);
    CHECK(std::abs(risingCrossings(out) - 440) <= 2);
}

TEST_CASE("Envelope runs attack, decay and release, then frees the slot")
{
    Scsp scsp;
    const Operator sine = sineOperator(scsp);
    SlotRegisters& regs = scsp.slot(3);
    regs.startAddress = sine.address;
    regs.loopEnd = 64;
    regs.loop = LoopMode::Forward;
    regs.attackRate = 20;
    regs.decay1Rate = 20;
    regs.decayLevel = 8; // 256 steps = 24 dB
    regs.releaseRate = 26;
    scsp.keyOn(3);
    CHECK(scsp.phase(3) == EnvelopePhase::Attack);

    std::vector<std::int16_t> block(2 * 4410);
    scsp.render(block);
    CHECK(scsp.phase(3) != EnvelopePhase::Attack);
    for (int i = 0; i < 20; ++i) scsp.render(block);
    CHECK(scsp.phase(3) == EnvelopePhase::Decay2);
    CHECK(scsp.attenuation(3) == doctest::Approx(256.0f).epsilon(0.01));
    const double sustained = rms(block);
    CHECK(sustained > 1000.0);

    scsp.keyOff(3);
    CHECK(scsp.phase(3) == EnvelopePhase::Release);
    for (int i = 0; i < 10; ++i) scsp.render(block);
    CHECK(scsp.phase(3) == EnvelopePhase::Off);
    CHECK(rms(block) == 0.0);
}

TEST_CASE("Total level and direct send level attenuate")
{
    const auto level = [](std::uint8_t totalLevel, std::uint8_t directLevel) {
        Scsp scsp;
        const Operator sine = sineOperator(scsp);
        SlotRegisters& regs = scsp.slot(0);
        regs.startAddress = sine.address;
        regs.loopEnd = 64;
        regs.loop = LoopMode::Forward;
        regs.totalLevel = totalLevel;
        regs.directLevel = directLevel;
        scsp.keyOn(0);
        std::vector<std::int16_t> out(2 * 4096);
        scsp.render(out);
        return rms(out);
    };
    const double full = level(0, 7);
    CHECK(level(16, 7) / full == doctest::Approx(std::pow(10.0, -6.0 / 20.0)).epsilon(0.02)); // 16 * 0.375 dB
    CHECK(level(0, 6) / full == doctest::Approx(std::pow(10.0, -6.0 / 20.0)).epsilon(0.02));
    CHECK(level(0, 0) == 0.0);
}

TEST_CASE("MML compiles notes, lengths, octaves and repeats")
{
    const auto track = compileMml("o4 l8 c d+4 e-4. > c < r16 [a b]3 L c1");
    REQUIRE(track);
    const auto& events = track->events;
    REQUIRE(events.size() == 12);
    CHECK(events[0].type == TrackEvent::Type::Note);
    CHECK(events[0].value == 60); // middle C
    CHECK(events[0].ticks == 24);
    CHECK(events[1].value == 63);
    CHECK(events[1].ticks == 48);
    CHECK(events[2].value == 63);
    CHECK(events[2].ticks == 72);
    CHECK(events[3].value == 72);
    CHECK(events[4].type == TrackEvent::Type::Rest);
    CHECK(events[4].ticks == 12);
    CHECK(events[5].value == 69);
    CHECK(events[10].value == 71);
    REQUIRE(track->loopIndex);
    CHECK(*track->loopIndex == 11);
    CHECK(track->lengthTicks == 24 + 48 + 72 + 24 + 12 + 6 * 24 + 192);

    CHECK_FALSE(compileMml("c x"));
    CHECK_FALSE(compileMml("[c d"));
    CHECK_FALSE(compileMml("c5"));     // not a whole number of ticks
    CHECK_FALSE(compileMml("c L v4")); // a loop without notes would never advance
    CHECK(compileMml("v15 ; comment c d e\n c")->events.size() == 2);
}

TEST_CASE("The sequencer keys notes on at the right sample")
{
    Scsp scsp;
    SoundDriver driver(scsp);
    Tone tone;
    tone.carrier = sineOperator(scsp);
    driver.addTone(tone);

    const std::string_view mml = "t125 @0 q8 c4 r4 c4";
    auto song = compileSong(std::span(&mml, 1));
    REQUIRE(song);
    CHECK(song->tempo == 125);
    driver.play(std::move(*song));

    // At 125 bpm a quarter note is 0.48 s = 21168 samples; the second note starts at 42336.
    std::vector<std::int16_t> out(2 * 42'000);
    driver.render(out);
    CHECK(driver.stats().activeSlots == 0); // first note released, rest in progress
    std::vector<std::int16_t> more(2 * 1'000);
    driver.render(more);
    CHECK(driver.stats().activeSlots == 1);
    CHECK(driver.tick() == 2 * kTicksPerQuarter + 2); // 441 samples per tick
}

TEST_CASE("Voice allocation steals the oldest note when all 32 slots are busy")
{
    Scsp scsp;
    SoundDriver driver(scsp);
    const Operator sine = sineOperator(scsp);
    for (int i = 0; i < kScspSlots; ++i) {
        const int slot = driver.allocateSlot();
        SlotRegisters& regs = scsp.slot(slot);
        regs.startAddress = sine.address;
        regs.loopEnd = 64;
        regs.loop = LoopMode::Forward;
        scsp.keyOn(slot);
    }
    CHECK(driver.stats().activeSlots == kScspSlots);
    CHECK(driver.stats().steals == 0);

    // A released slot is taken before a held one.
    scsp.keyOff(17);
    CHECK(driver.allocateSlot() == 17);
    CHECK(driver.stats().steals == 1);
    scsp.keyOn(17);
    // Nothing released: the oldest held note (all started together, so the first) is stolen.
    CHECK(driver.allocateSlot() == 0);
    CHECK(driver.stats().steals == 2);
}

TEST_CASE("The game's music fits sound RAM and renders deterministically")
{
    Music a, b;
    CHECK(a.scsp().ramUsed() <= kSoundRamSize);
    CHECK(a.driver().playing());

    std::vector<std::int16_t> first(2 * kSoundSampleRate * 3), second(first.size());
    a.render(first);
    b.render(second);
    CHECK(first == second);
    CHECK(rms(first) > 500.0);

    // Pausing releases every note; the song continues on resume.
    a.setPlaying(false);
    std::vector<std::int16_t> tail(2 * kSoundSampleRate * 3);
    a.render(tail);
    CHECK(a.driver().stats().activeSlots == 0);
    CHECK(rms(std::span(tail).last(2 * 4410)) < 50.0); // only the reverb tail is left
}
