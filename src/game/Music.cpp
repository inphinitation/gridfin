#include "game/Music.hpp"

#include "data/Config.hpp"

#include <format>

namespace gridfin {

Music::Music(const data::Vfs& vfs, std::string_view manifestPath)
{
    // The 68000's program, work area and sequence data sit at the bottom of sound RAM.
    (void)m_scsp.allocate(kDriverBytes);

    const auto manifestText = vfs.readText(manifestPath);
    if (!manifestText) {
        m_errors.push_back(std::format("{}: not found", manifestPath));
        return;
    }
    const data::Config manifest(*manifestText, std::string(manifestPath));

    m_bank.loadFolder(vfs, manifest.string("music.tones", "sound/tones/"), m_errors);

    // DSP: a reverb whose delay lines take the next stretch of sound RAM.
    const auto reverbBytes = static_cast<std::uint32_t>(manifest.integer("reverb.bytes", 32 * 1024, 0, 256 * 1024));
    const auto room = static_cast<float>(manifest.number("reverb.room", 0.8, 0.0, 0.98));
    const auto damping = static_cast<float>(manifest.number("reverb.damping", 0.3, 0.0, 1.0));
    m_scsp.setReverbLevel(static_cast<std::uint8_t>(manifest.integer("reverb.level", 5, 0, 7)));
    if (reverbBytes > 0 && !m_scsp.enableReverb(reverbBytes, room, damping)) {
        manifest.error("reverb.bytes", std::format("{} bytes don't fit in sound RAM, or are too few", reverbBytes));
    }

    if (const auto songPath = manifest.string("music.song")) {
        if (const auto text = vfs.readText(*songPath)) {
            auto song = saturn::compileSongFile(*text);
            if (song) {
                m_driver.play(std::move(*song));
            } else {
                m_errors.push_back(std::format("{}:{}", *songPath, song.error));
            }
        } else {
            manifest.error("music.song", std::format("{} not found", *songPath));
        }
    }
    (void)manifest.string("title"); // read by the app
    for (const std::string& error : manifest.errors()) m_errors.push_back(error);
}

} // namespace gridfin
