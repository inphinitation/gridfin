#include "gfx/GpuContext.hpp"

#include "core/Log.hpp"

#include <array>

#if defined(GRIDFIN_WGPU_NATIVE)
#include <webgpu/wgpu.h>
#endif

namespace gridfin::gpu {

GpuContext::~GpuContext()
{
    if (m_frame.texture) wgpuTextureRelease(m_frame.texture);
    m_queue.reset();
    m_device.reset();
    m_adapter.reset();
    m_surface.reset();
    m_instance.reset();
    destroySurfaceResources(m_metalView);
}

void GpuContext::fail(std::string message)
{
    log::error("WebGPU: {}", message);
    m_error = std::move(message);
    m_state = State::Failed;
}

bool GpuContext::start(SDL_Window* window)
{
#if defined(GRIDFIN_WGPU_NATIVE)
    wgpuSetLogLevel(WGPULogLevel_Warn);
    wgpuSetLogCallback(
        [](WGPULogLevel level, WGPUStringView message, void*) {
            if (level <= WGPULogLevel_Error) {
                log::error("wgpu: {}", toString(message));
            } else {
                log::warn("wgpu: {}", toString(message));
            }
        },
        nullptr);
#endif

    m_instance.reset(wgpuCreateInstance(nullptr));
    if (!m_instance) {
        fail("could not create a WebGPU instance (is WebGPU enabled in this browser?)");
        return false;
    }

    m_surface.reset(createSurface(m_instance.get(), window, &m_metalView));
    if (!m_surface) {
        fail("could not create a surface for the window");
        return false;
    }

    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.compatibleSurface = m_surface.get();
    options.powerPreference = WGPUPowerPreference_HighPerformance;

    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = this;
    callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* self,
                           void*) { static_cast<GpuContext*>(self)->onAdapter(status, adapter, message); };

    m_state = State::RequestingAdapter;
    wgpuInstanceRequestAdapter(m_instance.get(), &options, callback);
    return m_state != State::Failed;
}

void GpuContext::poll()
{
    if (m_instance) wgpuInstanceProcessEvents(m_instance.get());
}

void GpuContext::onAdapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message)
{
    if (status != WGPURequestAdapterStatus_Success || !adapter) {
        fail(std::format("no suitable GPU adapter: {}", toString(message)));
        return;
    }
    m_adapter.reset(adapter);

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(adapter, &info) == WGPUStatus_Success) {
        log::info("GPU: {} ({}), backend {}", toString(info.device), toString(info.description),
                  static_cast<int>(info.backendType));
        wgpuAdapterInfoFreeMembers(info);
    }

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.label = view("gridfin device");
    descriptor.defaultQueue.label = view("gridfin queue");
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason,
                                                    WGPUStringView lostMessage, void*, void*) {
        if (reason != WGPUDeviceLostReason_Destroyed && reason != WGPUDeviceLostReason_CallbackCancelled) {
            log::error("WebGPU device lost ({}): {}", static_cast<int>(reason), toString(lostMessage));
        }
    };
    descriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type,
                                                         WGPUStringView errorMessage, void*, void*) {
        log::error("WebGPU error ({}): {}", static_cast<int>(type), toString(errorMessage));
    };

    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = this;
    callback.callback = [](WGPURequestDeviceStatus deviceStatus, WGPUDevice device, WGPUStringView deviceMessage,
                           void* self,
                           void*) { static_cast<GpuContext*>(self)->onDevice(deviceStatus, device, deviceMessage); };

    m_state = State::RequestingDevice;
    wgpuAdapterRequestDevice(adapter, &descriptor, callback);
}

void GpuContext::onDevice(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message)
{
    if (status != WGPURequestDeviceStatus_Success || !device) {
        fail(std::format("could not create a device: {}", toString(message)));
        return;
    }
    m_device.reset(device);
    m_queue.reset(wgpuDeviceGetQueue(device));

    WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
    if (wgpuSurfaceGetCapabilities(m_surface.get(), m_adapter.get(), &capabilities) != WGPUStatus_Success
        || capabilities.formatCount == 0) {
        fail("the surface is not compatible with the adapter");
        return;
    }
    // Prefer a plain UNORM format: the game works in RGB555 and wants no sRGB conversion.
    m_surfaceFormat = capabilities.formats[0];
    for (WGPUTextureFormat preferred : {WGPUTextureFormat_BGRA8Unorm, WGPUTextureFormat_RGBA8Unorm}) {
        bool found = false;
        for (std::size_t i = 0; i < capabilities.formatCount; ++i)
            found = found || capabilities.formats[i] == preferred;
        if (found) {
            m_surfaceFormat = preferred;
            break;
        }
    }
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    m_state = State::Ready;
    configureSurface();
}

bool GpuContext::surfaceIsSrgb() const noexcept
{
    return m_surfaceFormat == WGPUTextureFormat_BGRA8UnormSrgb || m_surfaceFormat == WGPUTextureFormat_RGBA8UnormSrgb;
}

void GpuContext::resize(std::uint32_t width, std::uint32_t height)
{
    if (width == m_width && height == m_height) return;
    m_width = width;
    m_height = height;
    configureSurface();
}

void GpuContext::configureSurface()
{
    if (m_state != State::Ready || m_width == 0 || m_height == 0) return;
    WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = m_device.get();
    config.format = m_surfaceFormat;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = m_width;
    config.height = m_height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    wgpuSurfaceConfigure(m_surface.get(), &config);
}

WGPUTexture GpuContext::acquireFrame()
{
    if (m_state != State::Ready || m_width == 0 || m_height == 0) return nullptr;

    m_frame = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(m_surface.get(), &m_frame);
    switch (m_frame.status) {
    case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
    case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal: return m_frame.texture;
    case WGPUSurfaceGetCurrentTextureStatus_Timeout:
    case WGPUSurfaceGetCurrentTextureStatus_Outdated:
    case WGPUSurfaceGetCurrentTextureStatus_Lost:
        if (m_frame.texture) wgpuTextureRelease(m_frame.texture);
        m_frame.texture = nullptr;
        configureSurface();
        return nullptr;
    default:
        if (m_frame.texture) wgpuTextureRelease(m_frame.texture);
        m_frame.texture = nullptr;
        fail("could not acquire the next swap chain image");
        return nullptr;
    }
}

void GpuContext::present()
{
#if !defined(__EMSCRIPTEN__)
    // In the browser the canvas is presented automatically when control returns to the event loop.
    wgpuSurfacePresent(m_surface.get());
#endif
    if (m_frame.texture) wgpuTextureRelease(m_frame.texture);
    m_frame.texture = nullptr;
}

} // namespace gridfin::gpu
