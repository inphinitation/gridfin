#include "app/App.hpp"

#include "app/Screenshot.hpp"
#include "core/Log.hpp"

#include <algorithm>
#include <cstdlib>
#include <format>
#include <string_view>

#if defined(__EMSCRIPTEN__)
#include <emscripten/em_js.h>

// Hands a file from the in-memory file system to the browser as a download.
// clang-format off
EM_JS_DEPS(gridfin_download, "$UTF8ToString,$FS");
EM_JS(void, downloadFile, (const char* path), {
    const name = UTF8ToString(path);
    const blob = new Blob([FS.readFile(name)], {type: 'application/octet-stream'});
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = name;
    link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
    FS.unlink(name);
});
// clang-format on
#endif

namespace gridfin {

namespace {
constexpr int kDefaultScale = 3;
} // namespace

AppOptions AppOptions::parse(int argc, char** argv)
{
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--frames" && hasValue) {
            options.exitAfterFrames = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshotPath = argv[++i];
        } else if (arg == "--first-person") {
            options.startInFirstPerson = true;
        } else {
            log::warn("Ignoring unknown argument '{}'", arg);
        }
    }
    return options;
}

App::~App()
{
    // GPU objects reference the window's surface, so they go first.
    m_renderer.reset();
    m_gpu.reset();
    m_input.reset();
    if (m_window) SDL_DestroyWindow(m_window);
}

SDL_AppResult App::init()
{
    SDL_SetAppMetadata("Gridfin", "0.1.0", "dev.gridfin.game");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log::error("SDL_Init failed: {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__APPLE__)
    flags |= SDL_WINDOW_METAL;
#elif defined(__EMSCRIPTEN__)
    flags |= SDL_WINDOW_FILL_DOCUMENT; // the canvas follows the browser window
#endif
    // 320x224 shown at 4:3.
    m_window = SDL_CreateWindow("Gridfin", 320 * kDefaultScale, 240 * kDefaultScale, flags);
    if (!m_window) {
        log::error("SDL_CreateWindow failed: {}", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    m_input = std::make_unique<Input>(m_window);
    m_machine = std::make_unique<saturn::Machine>();
    if (!m_game.init(*m_machine)) return SDL_APP_FAILURE;
    if (m_options.startInFirstPerson) {
        InputState toggle;
        toggle.toggleViewPressed = true;
        m_game.update(0.0f, toggle);
    }

    m_gpu = std::make_unique<gpu::GpuContext>();
    if (!m_gpu->start(m_window)) return SDL_APP_FAILURE;
    m_lastTicks = SDL_GetTicksNS();

    log::info("Controls: WASD/left stick move, mouse/right stick look, Space/A jump, Shift/X run,");
    log::info("          wheel/d-pad zoom, V/Y first-third person, Esc release mouse,");
    log::info("          F2 scanlines, F3 scaling, F5 reload shaders, F11 fullscreen, F12 screenshot");
    return SDL_APP_CONTINUE;
}

SDL_AppResult App::startRenderer()
{
    m_renderer = std::make_unique<gpu::SaturnRenderer>();
    if (!m_renderer->init(m_gpu->device(), m_gpu->queue(), m_gpu->surfaceFormat(), m_gpu->surfaceIsSrgb())) {
        log::error("Failed to create the renderer");
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

void App::toggleFullscreen()
{
    const bool fullscreen = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(m_window, !fullscreen);
}

void App::requestScreenshot(std::string path)
{
    if (!m_renderer) return;
    m_renderer->requestCapture(
        [path = std::move(path)](const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height) {
            if (writeBmp(path, rgba, width, height)) {
                log::info("Saved screenshot {}", path);
#if defined(__EMSCRIPTEN__)
                downloadFile(path.c_str());
#endif
            } else {
                log::error("Could not write screenshot {}", path);
            }
        });
}

SDL_AppResult App::handleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        switch (event.key.scancode) {
        case SDL_SCANCODE_F2:
            m_present.scanlines = m_present.scanlines > 0.5f ? 0.0f : m_present.scanlines + 0.3f;
            break;
        case SDL_SCANCODE_F3:
            m_present.scale = static_cast<gpu::ScaleMode>((static_cast<int>(m_present.scale) + 1) % 3);
            break;
        case SDL_SCANCODE_F5:
            if (m_renderer) m_renderer->reloadShaders();
            break;
        case SDL_SCANCODE_F11: toggleFullscreen(); break;
        case SDL_SCANCODE_RETURN:
            if (event.key.mod & SDL_KMOD_ALT) toggleFullscreen();
            break;
        case SDL_SCANCODE_F12: requestScreenshot(std::format("gridfin-{}.bmp", SDL_GetTicks())); break;
        default: break;
        }
    }

    m_input->handleEvent(event);
    return SDL_APP_CONTINUE;
}

void App::updateTitle(float dt)
{
    m_titleTimer += dt;
    ++m_titleFrames;
    if (m_titleTimer < 0.5f) return;

    const saturn::GeometryStats& stats = m_machine->geometry.stats();
    const auto title =
        std::format("Gridfin | {:.0f} fps | VDP1 {}/{} commands, culled {} back {} near {} off{}",
                    static_cast<float>(m_titleFrames) / m_titleTimer, stats.drawn, saturn::Vdp1Vram::kMaxCommands - 1,
                    stats.culledBackface, stats.culledNear, stats.culledOffscreen, stats.overflow ? " | OVERFLOW" : "");
    SDL_SetWindowTitle(m_window, title.c_str());
    m_titleTimer = 0.0f;
    m_titleFrames = 0;
}

SDL_AppResult App::iterate()
{
    m_gpu->poll();
    switch (m_gpu->state()) {
    case gpu::GpuContext::State::Failed:
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Gridfin", m_gpu->error().c_str(), m_window);
        return SDL_APP_FAILURE;
    case gpu::GpuContext::State::Ready:
        if (!m_renderer && startRenderer() != SDL_APP_CONTINUE) return SDL_APP_FAILURE;
        break;
    default: return SDL_APP_CONTINUE; // still waiting for the adapter/device
    }

    if (m_quitRequested) return SDL_APP_SUCCESS;

    const std::uint64_t now = SDL_GetTicksNS();
    const float dt = std::min(static_cast<float>(now - m_lastTicks) * 1e-9f, 0.1f);
    m_lastTicks = now;

    // Fixed timestep in automated runs keeps screenshots reproducible.
    const float stepDt = m_options.exitAfterFrames > 0 ? Game::kFixedStep : dt;
    m_game.update(stepDt, m_input->frame(stepDt));
    m_game.render(*m_machine);

    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    m_gpu->resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));

    ++m_frame;
    const bool lastFrame =
        m_options.exitAfterFrames > 0 && m_frame >= static_cast<std::uint64_t>(m_options.exitAfterFrames);
    if (lastFrame && !m_options.screenshotPath.empty()) requestScreenshot(m_options.screenshotPath);

    if (WGPUTexture target = m_gpu->acquireFrame()) {
        m_renderer->render(*m_machine, target, m_gpu->width(), m_gpu->height(), m_present);
        m_gpu->present();
    }

    if (lastFrame) {
        // Give the screenshot readback a chance to complete before quitting.
        m_renderer->waitForCaptures(m_gpu->instance());
        m_quitRequested = true;
    }

    updateTitle(dt);
    return SDL_APP_CONTINUE;
}

} // namespace gridfin
