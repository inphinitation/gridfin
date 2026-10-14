// Creates a WebGPU surface from an SDL3 window on every supported platform.

#include "gfx/GpuContext.hpp"

#include "core/Log.hpp"

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#include <SDL3/SDL_metal.h>
#endif

#include <cstring>

namespace gridfin::gpu {

WGPUSurface createSurface(WGPUInstance instance, SDL_Window* window, void** metalView)
{
    WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
    descriptor.label = view("gridfin surface");
    *metalView = nullptr;

#if defined(__EMSCRIPTEN__)
    (void)window;
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = view("#canvas");
    descriptor.nextInChain = &canvas.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);

#elif defined(_WIN32)
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    source.hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    source.hinstance = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);

#elif defined(__APPLE__)
    SDL_MetalView metal = SDL_Metal_CreateView(window);
    if (!metal) return nullptr;
    *metalView = metal;
    WGPUSurfaceSourceMetalLayer source = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    source.layer = SDL_Metal_GetLayer(metal);
    descriptor.nextInChain = &source.chain;
    return wgpuInstanceCreateSurface(instance, &descriptor);

#else
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && std::strcmp(driver, "wayland") == 0) {
        WGPUSurfaceSourceWaylandSurface source = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
        source.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        source.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
    if (driver && std::strcmp(driver, "x11") == 0) {
        WGPUSurfaceSourceXlibWindow source = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
        source.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        source.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        descriptor.nextInChain = &source.chain;
        return wgpuInstanceCreateSurface(instance, &descriptor);
    }
    log::error("Unsupported SDL video driver '{}'", driver ? driver : "(none)");
    return nullptr;
#endif
}

void destroySurfaceResources(void* metalView)
{
#if defined(__APPLE__)
    if (metalView) SDL_Metal_DestroyView(static_cast<SDL_MetalView>(metalView));
#else
    (void)metalView;
#endif
}

} // namespace gridfin::gpu
