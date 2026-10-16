#include "data/Config.hpp"
#include "data/Pak.hpp"
#include "data/Vfs.hpp"
#include "data/Wav.hpp"
#include "game/SoundBank.hpp"
#include "saturn/SoundDriver.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>

using namespace gridfin;
using namespace gridfin::data;

namespace {

Bytes sineWav(std::size_t length, int rate)
{
    std::vector<std::int16_t> pcm(length);
    for (std::size_t i = 0; i < length; ++i) {
        pcm[i] = static_cast<std::int16_t>(
            std::lround(20'000.0 * std::sin(2.0 * std::numbers::pi * 441.0 * static_cast<double>(i) / rate)));
    }
    return encodeWav(pcm, 1, rate);
}

bool contains(const std::vector<std::string>& messages, std::string_view text)
{
    return std::ranges::any_of(messages, [&](const std::string& m) { return m.find(text) != std::string::npos; });
}

} // namespace

TEST_CASE("Data paths are normalised and can't escape their source")
{
    CHECK(normalizePath("sound/tones/bass.tone") == "sound/tones/bass.tone");
    CHECK(normalizePath("sound\\\\samples\\\\kick.wav") == std::nullopt); // empty part
    CHECK(normalizePath("sound\\samples\\kick.wav") == "sound/samples/kick.wav");
    CHECK_FALSE(normalizePath("../secret"));
    CHECK_FALSE(normalizePath("/etc/passwd"));
    CHECK_FALSE(normalizePath("a/./b"));
    CHECK_FALSE(normalizePath("C:/game"));
    CHECK_FALSE(normalizePath(""));

    CHECK(resolveRelative("sound/tones/kick.tone", "../samples/kick.wav") == "sound/samples/kick.wav");
    CHECK(resolveRelative("sound/tones/kick.tone", "kick.wav") == "sound/tones/kick.wav");
    CHECK(resolveRelative("sound/tones/kick.tone", "/gridfin.cfg") == "gridfin.cfg");
    CHECK_FALSE(resolveRelative("sound/kick.tone", "../../outside.wav"));
}

TEST_CASE(".pak archives round-trip and later mounts override earlier ones")
{
    MemorySource base("base");
    base.add("gridfin.cfg", "title = Base");
    base.add("sound/a.bin", Bytes{1, 2, 3});
    base.add("sound/b.bin", Bytes{});
    base.add("sound/tones/deep/x.tone", "program = 1");
    base.add("sound-extra.txt", "sorts between sound and sound/...");

    const Bytes image = buildPak(base);
    std::string error;
    auto pak = PakSource::open(image, "base.pak", &error);
    REQUIRE_MESSAGE(pak, error);
    CHECK(pak->list() == base.list());
    CHECK(pak->read("sound/a.bin") == Bytes{1, 2, 3});
    CHECK(pak->read("sound/b.bin") == Bytes{});
    CHECK(pak->read("sound/tones/deep/x.tone") == base.read("sound/tones/deep/x.tone"));
    CHECK_FALSE(pak->read("sound"));            // a folder, not a file
    CHECK_FALSE(pak->read("sound/a.bin/more")); // below a file
    CHECK_FALSE(pak->read("sound/c.bin"));

    // A flipped byte in a file fails its checksum.
    Bytes corrupt = image;
    corrupt[corrupt.size() - 1] ^= 0xffu;
    CHECK_FALSE(PakSource::open(corrupt, "bad.pak", &error));
    CHECK(error.find("corrupt") != std::string::npos);
    CHECK_FALSE(PakSource::open(Bytes{'n', 'o', 'p', 'e'}, "x", &error));

    // Folder entries must be sorted (lookups binary-search them): swap the names of two siblings.
    Bytes unsorted = image;
    const auto get32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(unsorted[at] | unsorted[at + 1] << 8 | unsorted[at + 2] << 16
                                          | unsorted[at + 3] << 24);
    };
    const std::uint32_t nodeTable = get32(12);
    // The root's children are nodes 1..3: gridfin.cfg, sound, sound-extra.txt. Swap 1 and 3's name fields.
    for (std::size_t i = 0; i < 8; ++i) std::swap(unsorted[nodeTable + 16 + i], unsorted[nodeTable + 48 + i]);
    CHECK_FALSE(PakSource::open(unsorted, "unsorted.pak", &error));
    CHECK(error.find("not sorted") != std::string::npos);

    auto mod = std::make_unique<MemorySource>("mod");
    mod->add("gridfin.cfg", "title = Mod");
    mod->add("sound/c.bin", Bytes{9});
    Vfs vfs;
    vfs.mount(std::move(pak));
    vfs.mount(std::move(mod));
    CHECK(vfs.readText("gridfin.cfg") == "title = Mod");
    CHECK(vfs.origin("gridfin.cfg") == "mod");
    CHECK(vfs.origin("sound/a.bin") == "base.pak");
    CHECK(vfs.list("sound/")
          == std::vector<std::string>{"sound/a.bin", "sound/b.bin", "sound/c.bin", "sound/tones/deep/x.tone"});
    CHECK_FALSE(vfs.read("../gridfin.cfg"));
}

TEST_CASE("The base game folder packs into a .pak with the same files")
{
    DirectorySource folder(GRIDFIN_DATA_DIR);
    const auto files = folder.list();
    CHECK(std::ranges::find(files, "gridfin.cfg") != files.end());
    auto pak = PakSource::open(buildPak(folder), "base.pak");
    REQUIRE(pak);
    CHECK(pak->list() == files);
    for (const std::string& path : files) CHECK(pak->read(path) == folder.read(path));
}

TEST_CASE("Config reads typed values and reports mistakes with line numbers")
{
    const Config config("; comment\ntitle = \"Gridfin; the game\"\nvolume = 12\n\n[reverb]\nroom = 0.5\nlevel = 9\n"
                        "typo = 1\nbroken line\n",
                        "test.cfg");
    CHECK(config.string("title") == "Gridfin; the game");
    CHECK(config.integer("volume", 0, 0, 15) == 12);
    CHECK(config.number("reverb.room", 0.0, 0.0, 1.0) == doctest::Approx(0.5));
    CHECK(config.integer("reverb.level", 5, 0, 7) == 5); // out of range: the fallback
    CHECK(config.boolean("missing", true));
    CHECK(contains(config.errors(), "test.cfg:7: reverb.level: 9 is outside 0..7"));
    CHECK(contains(config.errors(), "test.cfg:9: expected 'key = value'"));
    CHECK(contains(config.unusedKeys(), "test.cfg:8: unknown key 'reverb.typo'"));
}

TEST_CASE("WAVE files decode to mono, with smpl loop points")
{
    std::vector<std::int16_t> stereo = {1000, 3000, -2000, -2000, 32767, 32767};
    const auto sound = decodeWav(encodeWav(stereo, 2, 32'000));
    REQUIRE(sound);
    CHECK(sound->sampleRate == 32'000);
    CHECK(sound->sourceChannels == 2);
    REQUIRE(sound->samples.size() == 3);
    CHECK(sound->samples[0] == doctest::Approx(2000.0 / 32768.0));
    CHECK(sound->samples[1] == doctest::Approx(-2000.0 / 32768.0));
    CHECK_FALSE(sound->loopEnd);

    // Append a smpl chunk with root note 57 and a loop over samples 1..2.
    Bytes file = encodeWav(stereo, 2, 32'000);
    const auto put = [&](std::uint32_t v) {
        for (int i = 0; i < 4; ++i) file.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xffu));
    };
    file.insert(file.end(), {'s', 'm', 'p', 'l'});
    put(60);
    for (const std::uint32_t v : {0u, 0u, 0u, 57u, 0u, 0u, 0u, 1u, 0u}) put(v);
    for (const std::uint32_t v : {0u, 0u, 1u, 2u, 0u, 0u}) put(v);
    const std::uint32_t riffSize = static_cast<std::uint32_t>(file.size() - 8);
    for (std::size_t i = 0; i < 4; ++i) file[4 + i] = static_cast<std::uint8_t>((riffSize >> (8 * i)) & 0xffu);
    const auto looped = decodeWav(file);
    REQUIRE(looped);
    CHECK(looped->rootNote == 57);
    CHECK(looped->loopStart == 1u);
    CHECK(looped->loopEnd == 3u);

    std::string error;
    CHECK_FALSE(decodeWav(Bytes{'R', 'I', 'F', 'F'}, &error));
}

TEST_CASE("Note names use MML octave numbering")
{
    CHECK(parseNote("c4") == 60);
    CHECK(parseNote("a4") == 69);
    CHECK(parseNote("a#3") == 58);
    CHECK(parseNote("G+2") == 44);
    CHECK(parseNote("b-4") == 70);
    CHECK(parseNote("72") == 72);
    CHECK_FALSE(parseNote("h4"));
    CHECK_FALSE(parseNote("c"));
    CHECK_FALSE(parseNote("200"));
}

TEST_CASE("Song files split into tracks and report errors by line and column")
{
    const auto song = saturn::compileSongFile("#title Test\n; intro\n#track a\nt90 c d e\n#track b\n; bass\nc1\n");
    REQUIRE_MESSAGE(song, song.error);
    CHECK(song->tracks.size() == 2);
    CHECK(song->tempo == 90);
    CHECK(song->tracks[1].lengthTicks == 192);

    CHECK(saturn::compileSongFile("c d e")->tracks.size() == 1); // no headers: one track

    const auto bad = saturn::compileSongFile("#track a\nc d\n#track b\nc d x\n");
    REQUIRE_FALSE(bad);
    CHECK(bad.error.starts_with("4:5:"));
    CHECK_FALSE(saturn::compileSongFile("#tempo 3\n#track a\nc"));
}

TEST_CASE("Tone files load waves and imported samples into sound RAM")
{
    auto source = std::make_unique<MemorySource>();
    source->add("sound/tones/lead.tone", "program = 1\nwave = 1 0.5\nlevel = 10\nreverb = 3\n[modulator]\nwave = 1\n");
    source->add("sound/tones/drum.tone",
                "program = 2\nsample = ../samples/sine.wav\nformat = pcm8\ndownsample = 2\nfixed-pitch = yes\n");
    source->add("sound/tones/shared.tone", "program = 3\nwave = 1\n"); // same wave as the FM operators
    source->add("sound/samples/sine.wav", sineWav(1000, 44'100));
    Vfs vfs;
    vfs.mount(std::move(source));

    saturn::Scsp scsp;
    saturn::SoundDriver driver(scsp);
    SoundBank bank(scsp, driver);
    std::vector<std::string> errors;
    bank.loadFolder(vfs, "sound/tones/", errors);
    CHECK_MESSAGE(errors.empty(), (errors.empty() ? "" : errors.front()));
    CHECK(bank.stats().tones == 3);
    CHECK(bank.stats().samples == 3); // "1 0.5", "1" (shared three times) and the WAVE file

    const saturn::Tone& lead = driver.tones()[1];
    CHECK(lead.carrier.loop == saturn::LoopMode::Forward);
    CHECK(lead.carrier.length == 64);
    CHECK(lead.carrier.totalLevel == 10);
    REQUIRE(lead.modulator);
    CHECK(lead.modulationLevel == 9);
    CHECK(lead.effectLevel == 3);

    const saturn::Tone& drum = driver.tones()[2];
    CHECK(drum.carrier.format == saturn::SampleFormat::Pcm8);
    CHECK(drum.carrier.length == 500);
    CHECK(drum.carrier.sampleRate == doctest::Approx(22'050.0));
    CHECK(drum.carrier.loop == saturn::LoopMode::Off);
    CHECK(drum.fixedPitch);
    CHECK(driver.tones()[3].carrier.address == lead.modulator->address);
}

TEST_CASE("Tone file mistakes are reported, not loaded")
{
    auto source = std::make_unique<MemorySource>();
    source->add("t/missing.tone", "wave = 1\n");
    source->add("t/typo.tone", "program = 1\nwave = 1\nlevle = 3\n");
    source->add("t/both.tone", "program = 2\nwave = 1\nsample = x.wav\n");
    source->add("t/nofile.tone", "program = 3\nsample = nothere.wav\n");
    source->add("t/long.tone", "program = 4\nsample = long.wav\n");
    source->add("t/dupe.tone", "program = 5\nwave = 1\n");
    source->add("t/dupe2.tone", "program = 5\nwave = 1\n");
    source->add("t/long.wav", sineWav(70'000, 44'100));
    Vfs vfs;
    vfs.mount(std::move(source));

    saturn::Scsp scsp;
    saturn::SoundDriver driver(scsp);
    SoundBank bank(scsp, driver);
    std::vector<std::string> errors;
    bank.loadFolder(vfs, "t/", errors);
    CHECK(contains(errors, "t/missing.tone: program: missing"));
    CHECK(contains(errors, "t/typo.tone:3: unknown key 'levle'"));
    CHECK(contains(errors, "set exactly one of 'sample'"));
    CHECK(contains(errors, "t/nothere.wav not found"));
    CHECK(contains(errors, "the SCSP plays at most 65535 (try downsample = 2)"));
    CHECK(contains(errors, "5 is used by another tone"));
    CHECK(bank.stats().tones == 2); // typo.tone still loads (unknown keys are warnings), dupe.tone loads
}

TEST_CASE("Source folders report changed files for live reload")
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "gridfin-test-reload";
    fs::remove_all(root);
    fs::create_directories(root / "sound" / ".hidden");
    const auto write = [&](const fs::path& path, std::string_view text) {
        std::ofstream(root / path, std::ios::binary) << text;
    };
    write("gridfin.cfg", "title = A");
    write("sound/.hidden/swap", "x");

    DirectorySource source(root);
    CHECK(source.list() == std::vector<std::string>{"gridfin.cfg"});
    CHECK(source.pollChanges().empty());

    write("sound/new.mml", "c d e");
    fs::last_write_time(root / "gridfin.cfg", fs::last_write_time(root / "gridfin.cfg") + std::chrono::seconds(2));
    auto changed = source.pollChanges();
    std::ranges::sort(changed);
    CHECK(changed == std::vector<std::string>{"gridfin.cfg", "sound/new.mml"});
    CHECK(source.pollChanges().empty());

    fs::remove(root / "sound/new.mml");
    CHECK(source.pollChanges() == std::vector<std::string>{"sound/new.mml"});
    fs::remove_all(root);
}
