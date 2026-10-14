#pragma once

#include "gfx/WebGpu.hpp"
#include "saturn/Machine.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gridfin::gpu {

enum class ScaleMode { Fit4x3, Integer, Stretch };

/// How the picture is put on the window.
enum class DisplayFilter {
    Sharp,           ///< crisp square dots
    Scanlines,       ///< dots with light scanline gaps
    ScanlinesStrong, ///< dots with dark scanline gaps
    Crt,             ///< electron beam, phosphor mask and glow: dots blur together like on a CRT
};

struct PresentSettings {
    ScaleMode scale = ScaleMode::Fit4x3;
    DisplayFilter filter = DisplayFilter::Sharp;
    bool ntsc = false; ///< NTSC composite video signal (colour bleed, fringing, dot crawl)
};

enum class CaptureSource {
    Framebuffer, ///< the 320x224 VDP output
    Display,     ///< what the window shows, with scaling and filters
};

/// Draws a frame of the emulated Saturn with WebGPU.
///
/// Pass 1 renders the VDP2 background and the VDP1 command list into a
/// 320x224 framebuffer. Optionally pass 2 runs it through an NTSC composite
/// encoder/decoder. The last pass scales it onto the window (sharp, scanlines or CRT).
class SaturnRenderer {
public:
    SaturnRenderer();
    ~SaturnRenderer();
    SaturnRenderer(const SaturnRenderer&) = delete;
    SaturnRenderer& operator=(const SaturnRenderer&) = delete;

    [[nodiscard]] bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat, bool surfaceIsSrgb);

    void render(saturn::Machine& machine, WGPUTexture target, std::uint32_t targetWidth, std::uint32_t targetHeight,
                const PresentSettings& settings);

    using CaptureCallback =
        std::function<void(const std::vector<std::uint8_t>& rgba, std::uint32_t width, std::uint32_t height)>;
    /// Reads back the next rendered frame.
    void requestCapture(CaptureCallback callback, CaptureSource source = CaptureSource::Framebuffer);
    /// Blocks until pending captures completed (native only).
    void waitForCaptures(WGPUInstance instance);

    /// Recompiles the shaders from disk (development builds); keeps the old ones on error.
    void reloadShaders();
    [[nodiscard]] static bool canReloadShaders() noexcept;

private:
    struct Pipelines;
    struct PendingReload;
    struct Capture;

    [[nodiscard]] std::unique_ptr<Pipelines> createPipelines() const;
    void uploadMemory(saturn::Machine& machine);
    void writeFrameUniforms(const saturn::Machine& machine);
    void encodePresent(WGPUCommandEncoder encoder, WGPUTextureView target, bool ntsc) const;
    void encodeCapture(WGPUCommandEncoder encoder, WGPUTexture target, std::uint32_t width, std::uint32_t height,
                       bool ntsc);

    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_Undefined;
    bool m_surfaceIsSrgb = false;

    Buffer m_vdp1Vram;
    Buffer m_vdp2Vram;
    Buffer m_cram;
    Buffer m_frameUniforms;
    Buffer m_presentUniforms;
    Buffer m_ntscUniforms;
    Texture m_screen;
    TextureView m_screenView;
    Texture m_ntsc;
    TextureView m_ntscView;

    BindGroupLayout m_sceneLayout;
    BindGroupLayout m_presentLayout;
    BindGroup m_sceneBindGroup;
    BindGroup m_presentBindGroup;
    BindGroup m_presentNtscBindGroup;
    BindGroup m_ntscBindGroup;
    std::uint32_t m_frameCounter = 0;
    std::unique_ptr<Pipelines> m_pipelines;

    CaptureCallback m_captureRequest;
    CaptureSource m_captureSource = CaptureSource::Framebuffer;
    std::vector<std::unique_ptr<Capture>> m_captures;
};

} // namespace gridfin::gpu
