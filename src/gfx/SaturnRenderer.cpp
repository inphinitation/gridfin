#include "gfx/SaturnRenderer.hpp"

#include "Shaders.hpp"
#include "core/Log.hpp"

#if defined(GRIDFIN_WGPU_NATIVE)
#include <webgpu/wgpu.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace gridfin::gpu {

using namespace saturn;

namespace {

/// Mirrors `Frame` in shaders/vdp.wgsl.
struct FrameUniforms {
    std::array<float, 4> screen;
    std::array<float, 4> focal;
    std::array<float, 4> camPos;
    std::array<float, 4> camRight;
    std::array<float, 4> camUp;
    std::array<float, 4> camForward;
    std::array<float, 4> ground;
    std::array<float, 4> sky;
    std::array<std::uint32_t, 4> vdp2;
    std::array<std::uint32_t, 4> sizes;
};
static_assert(sizeof(FrameUniforms) == 160);

/// Mirrors `Present` in shaders/present.wgsl.
struct PresentUniforms {
    std::array<float, 4> viewport;
    std::array<float, 4> params;
};

constexpr WGPUTextureFormat kScreenFormat = WGPUTextureFormat_RGBA8Unorm;

[[nodiscard]] std::array<float, 4> vec4(glm::vec3 v)
{
    return {v.x, v.y, v.z, 0.0f};
}

[[nodiscard]] std::string shaderSource(std::string_view name, std::string_view embedded)
{
#if defined(GRIDFIN_SHADER_DIR)
    std::ifstream file(std::string(GRIDFIN_SHADER_DIR) + "/" + std::string(name) + ".wgsl", std::ios::binary);
    if (file) {
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }
    log::warn("Shader '{}' not found in {}, using the embedded copy", name, GRIDFIN_SHADER_DIR);
#else
    (void)name;
#endif
    return std::string(embedded);
}

[[nodiscard]] ShaderModule createShader(WGPUDevice device, std::string_view label, const std::string& code)
{
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = view(code);
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain;
    descriptor.label = view(label);
    return ShaderModule(wgpuDeviceCreateShaderModule(device, &descriptor));
}

[[nodiscard]] Buffer createBuffer(WGPUDevice device, std::string_view label, std::uint64_t size, WGPUBufferUsage usage)
{
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.label = view(label);
    descriptor.size = size;
    descriptor.usage = usage | WGPUBufferUsage_CopyDst;
    return Buffer(wgpuDeviceCreateBuffer(device, &descriptor));
}

[[nodiscard]] RenderPipeline createPipeline(WGPUDevice device, std::string_view label, WGPUPipelineLayout layout,
                                            WGPUShaderModule module, std::string_view vertexEntry,
                                            std::string_view fragmentEntry, WGPUTextureFormat format,
                                            const WGPUBlendState* blend)
{
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    target.blend = blend;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = view(fragmentEntry);
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.label = view(label);
    descriptor.layout = layout;
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = view(vertexEntry);
    descriptor.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    descriptor.primitive.cullMode = WGPUCullMode_None; // culling happens on the "SH-2"
    descriptor.fragment = &fragment;
    return RenderPipeline(wgpuDeviceCreateRenderPipeline(device, &descriptor));
}

[[nodiscard]] std::array<float, 4> computeViewport(ScaleMode mode, float width, float height)
{
    float w = width;
    float h = height;
    switch (mode) {
    case ScaleMode::Fit4x3:
        if (width * 3.0f > height * 4.0f) {
            w = std::floor(height * 4.0f / 3.0f);
        } else {
            h = std::floor(width * 3.0f / 4.0f);
        }
        break;
    case ScaleMode::Integer: {
        const float scale = std::max(1.0f, std::floor(std::min(width / static_cast<float>(kScreenWidth),
                                                               height / static_cast<float>(kScreenHeight))));
        w = static_cast<float>(kScreenWidth) * scale;
        h = static_cast<float>(kScreenHeight) * scale;
        break;
    }
    case ScaleMode::Stretch: break;
    }
    return {std::floor((width - w) * 0.5f), std::floor((height - h) * 0.5f), w, h};
}

} // namespace

struct SaturnRenderer::Pipelines {
    RenderPipeline vdp2;
    RenderPipeline vdp1;
    RenderPipeline present;
};

struct SaturnRenderer::PendingReload {
    SaturnRenderer* renderer;
    std::unique_ptr<Pipelines> pipelines;
};

struct SaturnRenderer::Capture {
    Buffer buffer;
    CaptureCallback callback;
    bool done = false;
};

constexpr std::uint32_t kCaptureRowBytes = kScreenWidth * 4; // already a multiple of 256

SaturnRenderer::SaturnRenderer() = default;
SaturnRenderer::~SaturnRenderer() = default;

bool SaturnRenderer::canReloadShaders() noexcept
{
#if defined(GRIDFIN_SHADER_DIR)
    return true;
#else
    return false;
#endif
}

bool SaturnRenderer::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat, bool surfaceIsSrgb)
{
    m_device = device;
    m_queue = queue;
    m_surfaceFormat = surfaceFormat;
    m_surfaceIsSrgb = surfaceIsSrgb;

    // Emulated video memory.
    m_vdp1Vram = createBuffer(device, "VDP1 VRAM", kVdp1VramSize, WGPUBufferUsage_Storage);
    m_vdp2Vram = createBuffer(device, "VDP2 VRAM", kVdp2VramSize, WGPUBufferUsage_Storage);
    m_cram = createBuffer(device, "VDP2 CRAM", kCramEntries * 2, WGPUBufferUsage_Storage);
    m_frameUniforms = createBuffer(device, "frame uniforms", sizeof(FrameUniforms), WGPUBufferUsage_Uniform);
    m_presentUniforms = createBuffer(device, "present uniforms", sizeof(PresentUniforms), WGPUBufferUsage_Uniform);

    // The 320x224 VDP framebuffer.
    WGPUTextureDescriptor screen = WGPU_TEXTURE_DESCRIPTOR_INIT;
    screen.label = view("VDP framebuffer");
    screen.size = {static_cast<std::uint32_t>(kScreenWidth), static_cast<std::uint32_t>(kScreenHeight), 1};
    screen.format = kScreenFormat;
    screen.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
    m_screen.reset(wgpuDeviceCreateTexture(device, &screen));
    m_screenView.reset(wgpuTextureCreateView(m_screen.get(), nullptr));

    // Bind group layouts.
    {
        std::array<WGPUBindGroupLayoutEntry, 4> entries{};
        for (std::uint32_t i = 0; i < entries.size(); ++i) {
            entries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entries[i].binding = i;
            entries[i].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            entries[i].buffer.type = i == 0 ? WGPUBufferBindingType_Uniform : WGPUBufferBindingType_ReadOnlyStorage;
        }
        WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        descriptor.label = view("VDP layout");
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        m_sceneLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &descriptor));

        std::array<WGPUBindGroupEntry, 4> bindings{};
        const std::array<std::pair<WGPUBuffer, std::uint64_t>, 4> buffers{{
            {m_frameUniforms.get(), sizeof(FrameUniforms)},
            {m_vdp1Vram.get(), kVdp1VramSize},
            {m_vdp2Vram.get(), kVdp2VramSize},
            {m_cram.get(), kCramEntries * 2},
        }};
        for (std::uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            bindings[i].binding = i;
            bindings[i].buffer = buffers[i].first;
            bindings[i].size = buffers[i].second;
        }
        WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group.label = view("VDP bind group");
        group.layout = m_sceneLayout.get();
        group.entryCount = bindings.size();
        group.entries = bindings.data();
        m_sceneBindGroup.reset(wgpuDeviceCreateBindGroup(device, &group));
    }
    {
        std::array<WGPUBindGroupLayoutEntry, 2> entries{WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].buffer.type = WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor descriptor = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        descriptor.label = view("present layout");
        descriptor.entryCount = entries.size();
        descriptor.entries = entries.data();
        m_presentLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &descriptor));

        std::array<WGPUBindGroupEntry, 2> bindings{WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        bindings[0].binding = 0;
        bindings[0].textureView = m_screenView.get();
        bindings[1].binding = 1;
        bindings[1].buffer = m_presentUniforms.get();
        bindings[1].size = sizeof(PresentUniforms);
        WGPUBindGroupDescriptor group = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        group.label = view("present bind group");
        group.layout = m_presentLayout.get();
        group.entryCount = bindings.size();
        group.entries = bindings.data();
        m_presentBindGroup.reset(wgpuDeviceCreateBindGroup(device, &group));
    }

    m_pipelines = createPipelines();
    return m_pipelines && m_pipelines->vdp1 && m_pipelines->vdp2 && m_pipelines->present;
}

std::unique_ptr<SaturnRenderer::Pipelines> SaturnRenderer::createPipelines() const
{
    const ShaderModule vdp = createShader(m_device, "vdp.wgsl", shaderSource("vdp", shaders::vdp));
    const ShaderModule present = createShader(m_device, "present.wgsl", shaderSource("present", shaders::present));
    if (!vdp || !present) return nullptr;

    const auto makeLayout = [&](WGPUBindGroupLayout group) {
        WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        descriptor.bindGroupLayoutCount = 1;
        descriptor.bindGroupLayouts = &group;
        return PipelineLayout(wgpuDeviceCreatePipelineLayout(m_device, &descriptor));
    };
    const PipelineLayout sceneLayout = makeLayout(m_sceneLayout.get());
    const PipelineLayout presentLayout = makeLayout(m_presentLayout.get());

    // Half-transparency and shadow are expressed through alpha; opaque pixels write alpha 1.
    WGPUBlendState blend{};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
    blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};

    auto pipelines = std::make_unique<Pipelines>();
    pipelines->vdp2 = createPipeline(m_device, "VDP2", sceneLayout.get(), vdp.get(), "fullscreenVertex", "vdp2Fragment",
                                     kScreenFormat, nullptr);
    pipelines->vdp1 = createPipeline(m_device, "VDP1", sceneLayout.get(), vdp.get(), "vdp1Vertex", "vdp1Fragment",
                                     kScreenFormat, &blend);
    pipelines->present = createPipeline(m_device, "present", presentLayout.get(), present.get(), "presentVertex",
                                        "presentFragment", m_surfaceFormat, nullptr);
    return pipelines;
}

void SaturnRenderer::reloadShaders()
{
    if (!canReloadShaders()) return;
    wgpuDevicePushErrorScope(m_device, WGPUErrorFilter_Validation);
    auto* pending = new PendingReload{this, createPipelines()};

    WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowSpontaneous;
    callback.userdata1 = pending;
    callback.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView message, void* data,
                           void*) {
        std::unique_ptr<PendingReload> reload(static_cast<PendingReload*>(data));
        if (status != WGPUPopErrorScopeStatus_Success) return;
        if (type != WGPUErrorType_NoError || !reload->pipelines) {
            log::error("Shader reload failed, keeping the previous shaders:\n{}", toString(message));
            return;
        }
        reload->renderer->m_pipelines = std::move(reload->pipelines);
        log::info("Shaders reloaded");
    };
    wgpuDevicePopErrorScope(m_device, callback);
}

void SaturnRenderer::uploadMemory(Machine& machine)
{
    // VDP1: the command and gouraud tables change every frame; textures only where the cache streamed them in.
    Vdp1Vram& vdp1 = machine.vdp1;
    const auto vdp1Bytes = vdp1.bytes();
    const std::uint32_t count = vdp1.commandCount();
    wgpuQueueWriteBuffer(m_queue, m_vdp1Vram.get(), Vdp1Vram::kCommandTableAddress,
                         vdp1Bytes.data() + Vdp1Vram::kCommandTableAddress, (count + 1) * 32u);
    if (count > 0) {
        wgpuQueueWriteBuffer(m_queue, m_vdp1Vram.get(), Vdp1Vram::kGouraudTableAddress,
                             vdp1Bytes.data() + Vdp1Vram::kGouraudTableAddress, count * 8u);
    }
    for (auto [begin, end] : vdp1.takeDirtyRanges()) {
        begin &= ~3u; // buffer writes must be 4-byte aligned
        end = std::min((end + 3u) & ~3u, kVdp1VramSize);
        wgpuQueueWriteBuffer(m_queue, m_vdp1Vram.get(), begin, vdp1Bytes.data() + begin, end - begin);
    }

    // VDP2: bitmaps change at load time, the line colour table every frame.
    Vdp2Vram& vdp2 = machine.vdp2;
    const Vdp2Registers& registers = machine.vdp2Registers;
    for (std::uint32_t line = 0; line < registers.lineColors.size(); ++line) {
        vdp2.write16(registers.lineColorAddress + line * 2, registers.lineColors[line]);
    }
    const auto vdp2Bytes = vdp2.bytes();
    if (vdp2.vramDirty) {
        wgpuQueueWriteBuffer(m_queue, m_vdp2Vram.get(), 0, vdp2Bytes.data(), vdp2Bytes.size());
        vdp2.vramDirty = false;
    } else {
        wgpuQueueWriteBuffer(m_queue, m_vdp2Vram.get(), registers.lineColorAddress,
                             vdp2Bytes.data() + registers.lineColorAddress, kScreenHeight * 2);
    }
    if (vdp2.cramDirty) {
        const auto cram = vdp2.cramWords();
        wgpuQueueWriteBuffer(m_queue, m_cram.get(), 0, cram.data(), cram.size_bytes());
        vdp2.cramDirty = false;
    }
}

void SaturnRenderer::writeFrameUniforms(const Machine& machine)
{
    const View& view = machine.view;
    const Vdp2Registers& registers = machine.vdp2Registers;
    const auto size = [](std::uint16_t width, std::uint16_t height) {
        return static_cast<std::uint32_t>(width) | (static_cast<std::uint32_t>(height) << 16);
    };

    FrameUniforms frame{};
    frame.screen = {static_cast<float>(kScreenWidth), static_cast<float>(kScreenHeight), kScreenWidth * 0.5f,
                    kScreenHeight * 0.5f};
    frame.focal = {view.focalX, view.focalY, 0.0f, 0.0f};
    frame.camPos = vec4(view.position);
    frame.camRight = vec4(view.right);
    frame.camUp = vec4(view.up);
    frame.camForward = vec4(view.forward);
    frame.ground = {registers.groundHeight, registers.groundTexelsPerUnit, registers.fadeStart, registers.fadeEnd};
    frame.sky = {registers.cloudScrollX, view.horizonRow(), 0.0f, 0.0f};
    frame.vdp2 = {registers.ground.address, registers.clouds.address, registers.lineColorAddress,
                  (registers.groundEnabled ? 1u : 0u) | (registers.cloudsEnabled ? 2u : 0u)};
    frame.sizes = {size(registers.ground.width, registers.ground.height),
                   size(registers.clouds.width, registers.clouds.height),
                   size(registers.ground.bank, registers.clouds.bank), registers.hazeColor};
    wgpuQueueWriteBuffer(m_queue, m_frameUniforms.get(), 0, &frame, sizeof(frame));
}

void SaturnRenderer::render(Machine& machine, WGPUTexture target, std::uint32_t targetWidth, std::uint32_t targetHeight,
                            const PresentSettings& settings)
{
    std::erase_if(m_captures, [](const auto& c) { return c->done; });
    uploadMemory(machine);
    writeFrameUniforms(machine);

    PresentUniforms present{};
    present.viewport =
        computeViewport(settings.scale, static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    present.params = {settings.scanlines, m_surfaceIsSrgb ? 1.0f : 0.0f, 0.0f, 0.0f};
    wgpuQueueWriteBuffer(m_queue, m_presentUniforms.get(), 0, &present, sizeof(present));

    CommandEncoder encoder(wgpuDeviceCreateCommandEncoder(m_device, nullptr));

    {
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = m_screenView.get();
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.label = view("VDP pass");
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder.get(), &descriptor));
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, m_sceneBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(pass.get(), m_pipelines->vdp2.get());
        wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
        if (const std::uint32_t count = machine.vdp1.commandCount(); count > 0) {
            wgpuRenderPassEncoderSetPipeline(pass.get(), m_pipelines->vdp1.get());
            wgpuRenderPassEncoderDraw(pass.get(), count * 6, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass.get());
    }

    {
        TextureView targetView(wgpuTextureCreateView(target, nullptr));
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = targetView.get();
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDescriptor descriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        descriptor.label = view("present pass");
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        RenderPassEncoder pass(wgpuCommandEncoderBeginRenderPass(encoder.get(), &descriptor));
        wgpuRenderPassEncoderSetPipeline(pass.get(), m_pipelines->present.get());
        wgpuRenderPassEncoderSetBindGroup(pass.get(), 0, m_presentBindGroup.get(), 0, nullptr);
        wgpuRenderPassEncoderDraw(pass.get(), 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass.get());
    }

    Capture* capture = nullptr;
    if (m_captureRequest) {
        auto pending = std::make_unique<Capture>();
        pending->callback = std::move(m_captureRequest);
        m_captureRequest = nullptr;
        pending->buffer = createBuffer(m_device, "capture", kCaptureRowBytes * kScreenHeight, WGPUBufferUsage_MapRead);

        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = m_screen.get();
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = pending->buffer.get();
        destination.layout.bytesPerRow = kCaptureRowBytes;
        destination.layout.rowsPerImage = kScreenHeight;
        const WGPUExtent3D extent{static_cast<std::uint32_t>(kScreenWidth), static_cast<std::uint32_t>(kScreenHeight),
                                  1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder.get(), &source, &destination, &extent);
        capture = pending.get();
        m_captures.push_back(std::move(pending));
    }

    CommandBuffer commands(wgpuCommandEncoderFinish(encoder.get(), nullptr));
    const WGPUCommandBuffer buffer = commands.get();
    wgpuQueueSubmit(m_queue, 1, &buffer);

    if (capture) {
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.userdata1 = capture;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView message, void* data, void*) {
            auto* done = static_cast<Capture*>(data);
            done->done = true;
            if (status != WGPUMapAsyncStatus_Success) {
                log::error("Framebuffer readback failed: {}", toString(message));
                return;
            }
            const auto size = static_cast<std::size_t>(kCaptureRowBytes) * kScreenHeight;
            const auto* bytes =
                static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(done->buffer.get(), 0, size));
            const std::vector<std::uint8_t> rgba(bytes, bytes + size);
            wgpuBufferUnmap(done->buffer.get());
            done->callback(rgba, kScreenWidth, kScreenHeight);
        };
        wgpuBufferMapAsync(capture->buffer.get(), WGPUMapMode_Read, 0, kCaptureRowBytes * kScreenHeight, callback);
    }
}

void SaturnRenderer::requestCapture(CaptureCallback callback)
{
    m_captureRequest = std::move(callback);
}

void SaturnRenderer::waitForCaptures(WGPUInstance instance)
{
#if defined(__EMSCRIPTEN__)
    (void)instance; // the browser completes readbacks asynchronously
#else
    const auto pending = [&] {
        return std::any_of(m_captures.begin(), m_captures.end(), [](const auto& c) { return !c->done; });
    };
    for (int attempt = 0; attempt < 500 && pending(); ++attempt) {
#if defined(GRIDFIN_WGPU_NATIVE)
        wgpuDevicePoll(m_device, true, nullptr);
#endif
        wgpuInstanceProcessEvents(instance);
    }
    std::erase_if(m_captures, [](const auto& c) { return c->done; });
#endif
}

} // namespace gridfin::gpu
