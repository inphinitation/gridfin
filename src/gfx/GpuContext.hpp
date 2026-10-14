#pragma once

#include "gfx/WebGpu.hpp"

#include <cstdint>
#include <string>

struct SDL_Window;

namespace gridfin::gpu {

/// Owns the WebGPU instance, surface, adapter and device.
///
/// Adapter and device creation are asynchronous in WebGPU. Instead of blocking
/// (which the browser can't do without Asyncify/JSPI), initialisation is a small
/// state machine advanced by poll() from the main loop.
class GpuContext {
public:
    enum class State { Idle, RequestingAdapter, RequestingDevice, Ready, Failed };

    GpuContext() = default;
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;
    ~GpuContext();

    /// Creates the instance and window surface and starts requesting an adapter.
    [[nodiscard]] bool start(SDL_Window* window);

    /// Processes pending WebGPU events. Call every frame.
    void poll();

    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] const std::string& error() const noexcept { return m_error; }

    [[nodiscard]] WGPUInstance instance() const noexcept { return m_instance.get(); }
    [[nodiscard]] WGPUDevice device() const noexcept { return m_device.get(); }
    [[nodiscard]] WGPUQueue queue() const noexcept { return m_queue.get(); }
    [[nodiscard]] WGPUTextureFormat surfaceFormat() const noexcept { return m_surfaceFormat; }
    [[nodiscard]] bool surfaceIsSrgb() const noexcept;

    /// (Re)configures the swap chain; call after the window's pixel size changes.
    void resize(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] std::uint32_t width() const noexcept { return m_width; }
    [[nodiscard]] std::uint32_t height() const noexcept { return m_height; }

    /// Returns the texture to render this frame into, or null if the frame should be skipped.
    [[nodiscard]] WGPUTexture acquireFrame();
    void present();

private:
    void fail(std::string message);
    void onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message);
    void onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message);
    void configureSurface();

    State m_state = State::Idle;
    std::string m_error;
    Instance m_instance;
    Surface m_surface;
    Adapter m_adapter;
    Device m_device;
    Queue m_queue;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;
    WGPUSurfaceTexture m_frame{};
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    void* m_metalView = nullptr;
};

/// Creates a WebGPU surface for an SDL window (implemented per platform).
[[nodiscard]] WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window, void** metalView);
void destroySurfaceResources(void* metalView);

} // namespace gridfin::gpu
