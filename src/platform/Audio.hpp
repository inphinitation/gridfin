#pragma once

#include "game/Music.hpp"

#include <SDL3/SDL.h>

#include <memory>
#include <mutex>

namespace gridfin {

/// Plays the game's Music through an SDL audio stream. SDL pulls samples from its audio thread
/// (or from the Web Audio callback in the browser); the mutex keeps the game thread's commands
/// and the renderer apart. Without an audio device the game runs silently.
class AudioOutput {
public:
    AudioOutput() = default;
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    /// Opens the default playback device. Returns false (and stays silent) if there is none.
    bool start();

    void toggleMusic();

private:
    static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional, int total);

    std::mutex m_mutex;
    std::unique_ptr<Music> m_music;
    SDL_AudioStream* m_stream = nullptr;
};

} // namespace gridfin
