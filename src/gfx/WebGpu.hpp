#pragma once

#include <webgpu/webgpu.h>

#include <string_view>
#include <utility>

namespace gridfin::gpu {

[[nodiscard]] inline WGPUStringView view(std::string_view text) noexcept
{
    return {text.data(), text.size()};
}

[[nodiscard]] inline std::string_view toString(WGPUStringView text) noexcept
{
    if (!text.data) return {};
    if (text.length == WGPU_STRLEN) return text.data;
    return {text.data, text.length};
}

/// Move-only owner of a webgpu.h object.
template <typename T, void (*Release)(T)>
class Handle {
public:
    Handle() noexcept = default;
    explicit Handle(T handle) noexcept : m_handle(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept
    {
        if (this != &other) reset(std::exchange(other.m_handle, nullptr));
        return *this;
    }
    ~Handle() { reset(); }

    void reset(T handle = nullptr) noexcept
    {
        if (m_handle) Release(m_handle);
        m_handle = handle;
    }

    [[nodiscard]] T get() const noexcept { return m_handle; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_handle != nullptr; }

private:
    T m_handle = nullptr;
};

using Instance = Handle<WGPUInstance, wgpuInstanceRelease>;
using Adapter = Handle<WGPUAdapter, wgpuAdapterRelease>;
using Device = Handle<WGPUDevice, wgpuDeviceRelease>;
using Queue = Handle<WGPUQueue, wgpuQueueRelease>;
using Surface = Handle<WGPUSurface, wgpuSurfaceRelease>;
using Buffer = Handle<WGPUBuffer, wgpuBufferRelease>;
using Texture = Handle<WGPUTexture, wgpuTextureRelease>;
using TextureView = Handle<WGPUTextureView, wgpuTextureViewRelease>;
using ShaderModule = Handle<WGPUShaderModule, wgpuShaderModuleRelease>;
using BindGroupLayout = Handle<WGPUBindGroupLayout, wgpuBindGroupLayoutRelease>;
using BindGroup = Handle<WGPUBindGroup, wgpuBindGroupRelease>;
using PipelineLayout = Handle<WGPUPipelineLayout, wgpuPipelineLayoutRelease>;
using RenderPipeline = Handle<WGPURenderPipeline, wgpuRenderPipelineRelease>;
using CommandEncoder = Handle<WGPUCommandEncoder, wgpuCommandEncoderRelease>;
using CommandBuffer = Handle<WGPUCommandBuffer, wgpuCommandBufferRelease>;
using RenderPassEncoder = Handle<WGPURenderPassEncoder, wgpuRenderPassEncoderRelease>;

} // namespace gridfin::gpu
