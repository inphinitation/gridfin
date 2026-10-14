#pragma once

#include "game/Game.hpp"
#include "gfx/GpuContext.hpp"
#include "gfx/SaturnRenderer.hpp"
#include "platform/Input.hpp"
#include "saturn/Machine.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

namespace gridfin {

struct AppOptions {
    int exitAfterFrames = 0;    ///< 0 = run until closed
    std::string screenshotPath; ///< saved on the last frame when exitAfterFrames is set
    bool startInFirstPerson = false;

    [[nodiscard]] static AppOptions parse(int argc, char** argv);
};

/// Ties the window, input, GPU and game together. Driven by SDL's app callbacks,
/// which map onto the browser's frame loop on the web.
class App {
public:
    explicit App(AppOptions options) : m_options(std::move(options)) {}
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    SDL_AppResult init();
    SDL_AppResult handleEvent(const SDL_Event& event);
    SDL_AppResult iterate();

private:
    SDL_AppResult startRenderer();
    void updateTitle(float dt);
    void requestScreenshot(std::string path);
    void toggleFullscreen();
    void presentFrame();

    AppOptions m_options;
    SDL_Window* m_window = nullptr;
    std::unique_ptr<Input> m_input;
    std::unique_ptr<gpu::GpuContext> m_gpu;
    std::unique_ptr<gpu::SaturnRenderer> m_renderer;
    gpu::PresentSettings m_present;
    std::unique_ptr<saturn::Machine> m_machine;
    Game m_game;

    std::uint64_t m_lastTicks = 0;
    std::uint64_t m_frame = 0;
    std::uint64_t m_holdUntil = 0;
    bool m_enforceVdp1Timing = true;
    float m_titleTimer = 0.0f;
    std::uint64_t m_titleDmaBytes = 0;
    int m_titleFrames = 0;
    bool m_quitRequested = false;
};

} // namespace gridfin
