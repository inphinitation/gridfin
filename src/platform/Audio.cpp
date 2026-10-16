#include "platform/Audio.hpp"

#include "core/Log.hpp"

#include <array>

namespace gridfin {

AudioOutput::~AudioOutput()
{
    // Destroying the stream stops the callback before the music goes away.
    if (m_stream) SDL_DestroyAudioStream(m_stream);
}

namespace {
std::unique_ptr<Music> loadMusic(const data::Vfs& data)
{
    auto music = std::make_unique<Music>(data);
    for (const std::string& error : music->errors()) log::warn("{}", error);
    log::info("Sound RAM: {} KB of {} KB ({} tones, {} samples in {} KB)", music->scsp().ramUsed() / 1024,
              saturn::kSoundRamSize / 1024, music->bank().tones, music->bank().samples,
              music->bank().sampleBytes / 1024);
    return music;
}
} // namespace

bool AudioOutput::start(const data::Vfs& data)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("No audio: {}", SDL_GetError());
        return false;
    }
    m_music = loadMusic(data);
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, saturn::kSoundSampleRate};
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioOutput::feed, this);
    if (!m_stream) {
        log::warn("No audio device: {}", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(m_stream);
    return true;
}

void AudioOutput::reload(const data::Vfs& data)
{
    if (!m_music) return;
    auto music = loadMusic(data); // outside the lock: the audio thread keeps playing meanwhile
    const std::scoped_lock lock(m_mutex);
    music->setPlaying(m_music->playing());
    m_music = std::move(music);
}

void AudioOutput::toggleMusic()
{
    if (!m_music) return;
    const std::scoped_lock lock(m_mutex);
    m_music->setPlaying(!m_music->playing());
    log::info("Music {}", m_music->playing() ? "on" : "off");
}

void SDLCALL AudioOutput::feed(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/)
{
    auto* self = static_cast<AudioOutput*>(userdata);
    std::array<std::int16_t, 1024> buffer{};
    while (additional > 0) {
        const int bytes = std::min(additional, static_cast<int>(sizeof(buffer)));
        const auto frames = static_cast<std::size_t>(bytes) / (2 * sizeof(std::int16_t));
        if (frames == 0) break;
        {
            const std::scoped_lock lock(self->m_mutex);
            self->m_music->render(std::span(buffer).first(frames * 2));
        }
        SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(frames * 2 * sizeof(std::int16_t)));
        additional -= static_cast<int>(frames * 2 * sizeof(std::int16_t));
    }
}

} // namespace gridfin
