#include "imgui_layer.hpp"
#include "window.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "implot.h"
#include "GLFW/glfw3.h"
#include "rimera_buffer.hpp"
#include "rimera_builders.hpp"
#include "rimera_compiler.hpp"
#include "rimera_command.hpp"
#include "rimera_device.hpp"
#include "rimera_residency.hpp"
#include "rimera_sampler.hpp"
#include "rimera_swapchain.hpp"
#include "rimera_texture.hpp"
#include "algorithm"
#include "cmath"
#include "cstddef"
#include "cstdio"
#include "cstring"

namespace
{
    constexpr auto kShader = R"(
        struct ImGuiVertex
        {
            float2 position;
            float2 uv;
            uint color;
        };

        cbuffer Constants : register(b3)
        {
            uint vertex_buffer_index;
            uint sampler_index;
            uint texture_index;
            uint vertex_offset;
            float2 scale;
            float2 translate;
            uint hdr;
            float sdr_white_scale;
        };

        struct VSOut
        {
            float4 position : SV_Position;
            float2 uv : TEXCOORD0;
            float4 color : COLOR0;
        };

        template <typename T>
        T LoadElement(ByteAddressBuffer buffer, uint index)
        {
            return buffer.Load<T>(index * sizeof(T));
        }

        float3 SrgbToLinear(float3 color)
        {
            return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
        }

        VSOut VSMain(uint vertex_id : SV_VertexID)
        {
            ByteAddressBuffer vertices = ResourceDescriptorHeap[vertex_buffer_index];
            ImGuiVertex vertex = LoadElement<ImGuiVertex>(vertices, vertex_id + vertex_offset);
            VSOut output;
            output.position = float4(vertex.position * scale + translate, 0.0, 1.0);
            output.uv = vertex.uv;
            output.color = float4(vertex.color & 0xffu,
                                  (vertex.color >> 8) & 0xffu,
                                  (vertex.color >> 16) & 0xffu,
                                  vertex.color >> 24) / 255.0;
            if (hdr) output.color.rgb = SrgbToLinear(output.color.rgb) * sdr_white_scale;
            return output;
        }

        float4 PSMain(VSOut input) : SV_Target
        {
            Texture2D<float4> atlas = ResourceDescriptorHeap[texture_index];
            SamplerState atlas_sampler = SamplerDescriptorHeap[sampler_index];
            return input.color * atlas.Sample(atlas_sampler, input.uv);
        }
    )";

    static_assert(sizeof(ImDrawVert) == 20);
    static_assert(sizeof(ImDrawIdx) == 2);

    constexpr RM::ResourceState kPresentState{.layout = RM::BarrierLayout::Present};
}

RM::Result<ImGuiLayer*> ImGuiLayer::Create(RM::Device* device,
                                           RM::Queue* queue,
                                           Window* window,
                                           RM::Format format)
{
    auto* layer = new(std::nothrow) ImGuiLayer();
    if (!layer) return std::unexpected("Failed to allocate the ImGui layer");
    if (auto result = layer->Initialize(device, queue, window, format); !result)
    {
        delete layer;
        return std::unexpected(result.error());
    }
    return layer;
}

void ImGuiLayer::Destroy(const ImGuiLayer* layer)
{
    delete layer;
}

ImGuiLayer::~ImGuiLayer()
{
    if (m_context)
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::DestroyPlatformWindows();
        if (m_platform_initialized) ImGui_ImplGlfw_Shutdown();
    }
    DestroyViewportData(m_main_viewport);
    if (m_font_texture) m_device->DestroyImmediate(m_font_texture);
    if (m_sampler) m_device->DestroyImmediate(m_sampler);
    for (const auto& [format, pipeline] : m_pipelines) m_device->DestroyImmediate(pipeline);
    if (m_vertex_shader) RM::DestroyShader(m_vertex_shader);
    if (m_fragment_shader) RM::DestroyShader(m_fragment_shader);
    if (m_plot_context) ImPlot::DestroyContext(m_plot_context);
    if (m_context)
    {
        ImGui::GetIO().BackendRendererUserData = nullptr;
        ImGui::DestroyContext(m_context);
    }
}

RM::Result<void> ImGuiLayer::Initialize(RM::Device* device,
                                        RM::Queue* queue,
                                        Window* window,
                                        RM::Format format)
{
    if (!device || !queue || !window)
        return std::unexpected("Invalid ImGui layer creation parameters");
    m_device = device;
    m_queue = queue;
    m_window = window;
    m_format = format;

    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    if (!m_context) return std::unexpected("Failed to create the ImGui context");
    ImGui::SetCurrentContext(m_context);
    m_plot_context = ImPlot::CreateContext();
    if (!m_plot_context) return std::unexpected("Failed to create the ImPlot context");

    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad |
        ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.BackendRendererName = "Rimera";
    io.BackendRendererUserData = this;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset |
        ImGuiBackendFlags_RendererHasViewports;

    if (!ImGui_ImplGlfw_InitForOther(m_window->GetGlfwWindow(), true))
        return std::unexpected("Failed to initialize the ImGui GLFW backend");
    m_platform_initialized = true;

    auto& platform_io = ImGui::GetPlatformIO();
    platform_io.Platform_OpenInShellFn = OpenInShell;
    platform_io.Renderer_CreateWindow = CreateViewport;
    platform_io.Renderer_DestroyWindow = DestroyViewport;
    platform_io.Renderer_SetWindowSize = SetViewportSize;

    if (auto pipeline = GetPipeline(format); !pipeline) return std::unexpected(pipeline.error());
    if (auto result = RebuildFontAtlas(); !result) return result;

    auto sampler = m_device->CreateSampler({
        .wrap_u = RM::Wrap::ClampToEdge,
        .wrap_v = RM::Wrap::ClampToEdge,
        .wrap_w = RM::Wrap::ClampToEdge,
    });
    if (!sampler) return std::unexpected(sampler.error());
    m_sampler = *sampler;
    return {};
}

RM::Result<RM::Pipeline*> ImGuiLayer::GetPipeline(RM::Format format)
{
    if (const auto it = m_pipelines.find(format); it != m_pipelines.end()) return it->second;
    if (!m_vertex_shader || !m_fragment_shader)
    {
        auto compiler = RM::CreateCompiler();
        if (!compiler) return std::unexpected(compiler.error());
        auto vertex = (*compiler)->Compile(RM::ShaderBuilder(kShader).EntryPoint("VSMain").Build(),
                                           RM::ShaderType::Vertex);
        auto fragment = (*compiler)->Compile(RM::ShaderBuilder(kShader).EntryPoint("PSMain").Build(),
                                             RM::ShaderType::Fragment);
        RM::DestroyCompiler(*compiler);
        if (!vertex || !fragment)
        {
            auto error = !vertex ? vertex.error() : fragment.error();
            if (vertex) RM::DestroyShader(*vertex);
            if (fragment) RM::DestroyShader(*fragment);
            return std::unexpected(std::move(error));
        }
        m_vertex_shader = *vertex;
        m_fragment_shader = *fragment;
    }
    auto pipeline_info = RM::GraphicsPipelineBuilder(m_vertex_shader, m_fragment_shader)
                         .AddRenderTarget(format)
                         .AddBlendInfo(RM::BlendInfoBuilder()
                                       .Color(RM::BlendFactor::SrcAlpha,
                                              RM::BlendFactor::InvSrcAlpha)
                                       .Alpha(RM::BlendFactor::One, RM::BlendFactor::InvSrcAlpha)
                                       .Build())
                         .DisableDepth()
                         .DisableStencil()
                         .Cull(RM::CullMode::None)
                         .Fill(RM::FillMode::Solid)
                         .Primitive(RM::PolygonMode::Triangle)
                         .Build();
    auto pipeline = m_device->CreatePipeline(pipeline_info);
    if (!pipeline) return std::unexpected(pipeline.error());
    m_pipelines.emplace(format, *pipeline);
    return *pipeline;
}

RM::Result<void> ImGuiLayer::RebuildFontAtlas()
{
    auto& io = ImGui::GetIO();
    const float scale = std::max(1.f, m_window->GetContentScale());
    const auto size = m_window->GetSize();
    const auto framebuffer = m_window->GetFramebufferSize();
    const float ratio = framebuffer.x ? static_cast<float>(size.x) / framebuffer.x : 1.f;
    if (scale != m_font_scale || ratio != m_framebuffer_ratio)
    {
        io.Fonts->Clear();
        ImFontConfig font_config;
        font_config.SizePixels = 13.f * scale;
        io.Fonts->AddFontDefault(&font_config);
        io.FontGlobalScale = ratio;

        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        style.ScaleAllSizes(scale * ratio);
        ImGui::GetStyle() = style;

        m_font_scale = scale;
        m_framebuffer_ratio = ratio;
    }

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels || width <= 0 || height <= 0)
        return std::unexpected("Failed to build the ImGui font atlas");

    auto texture = m_device->CreateTexture(
        RM::TextureBuilder(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
        .Format(RM::Format::RGBA8_UNorm)
        .Usage(RM::TextureUsage::ShaderRead)
        .InitialData(pixels)
        .Build());
    if (!texture) return std::unexpected(texture.error());
    m_device->Retire(m_font_texture);
    m_font_texture = *texture;
    m_device->GetCoreResidencySet()->AddAllocation(m_font_texture);
    m_device->GetCoreResidencySet()->Commit();
    io.Fonts->SetTexID(GetTextureID(m_font_texture));
    return {};
}

void ImGuiLayer::NewFrame(float delta_time)
{
    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plot_context);
    auto& io = ImGui::GetIO();
    const auto size = m_window->GetSize();
    const auto framebuffer = m_window->GetFramebufferSize();
    const float ratio = framebuffer.x ? static_cast<float>(size.x) / framebuffer.x : 1.f;
    if (m_window->GetContentScale() != m_font_scale || ratio != m_framebuffer_ratio ||
        !io.Fonts->IsBuilt())
    {
        if (auto result = RebuildFontAtlas(); !result)
            std::fprintf(stderr, "[ImGui] %s\n", result.error().c_str());
    }
    ImGui_ImplGlfw_NewFrame();
    if (delta_time > 0.f) io.DeltaTime = delta_time;
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0,
                                 ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);
}

uint64_t ImGuiLayer::GetTextureID(const RM::Texture* texture) const
{
    return texture->GetResourceIndex(RM::TextureViewType::ShaderRead);
}

uint64_t ImGuiLayer::GetTextureID(RM::GraphTexture texture)
{
    if (!std::ranges::contains(m_graph_textures, texture.index, &RM::GraphTexture::index))
        m_graph_textures.push_back(texture);
    return kGraphTextureBit | texture.index;
}

uint32_t ImGuiLayer::ResolveTexture(const RM::RenderGraphContext& context,
                                    uint64_t texture_id) const
{
    if (!(texture_id & kGraphTextureBit)) return static_cast<uint32_t>(texture_id);
    const auto* texture =
        context.GetTexture({.index = static_cast<uint32_t>(texture_id & ~kGraphTextureBit)});
    return texture ? texture->GetResourceIndex(RM::TextureViewType::ShaderRead) : 0;
}

void ImGuiLayer::AddPass(RM::RenderGraph* graph, RM::GraphTexture target)
{
    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    auto pass = graph->AddRenderPass("ImGui");
    pass.Color(target, RM::LoadOp::Clear);
    for (const auto texture : m_graph_textures)
        pass.Read(texture, RM::BarrierStage::PixelShading);
    pass.Execute([this, target](RM::RenderGraphContext& context) -> RM::Result<void>
    {
        const auto* texture = context.GetTexture(target);
        return Record(context,
                      ImGui::GetMainViewport()->DrawData,
                      m_main_viewport,
                      m_display_info,
                      texture ? texture->GetFormat() : m_format);
    });

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        for (auto* viewport : ImGui::GetPlatformIO().Viewports)
            if (viewport != ImGui::GetMainViewport()) AddViewportPass(graph, viewport);
    }
    m_graph_textures.clear();
}

void ImGuiLayer::AddViewportPass(RM::RenderGraph* graph, ImGuiViewport* viewport)
{
    auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
    if (!data || !data->swapchain || (viewport->Flags & ImGuiViewportFlags_IsMinimized)) return;
    if (auto result = data->swapchain->Acquire(); !result)
    {
        std::fprintf(stderr, "[ImGui] %s\n", result.error().c_str());
        return;
    }
    auto* back_buffer = data->swapchain->GetCurrentBackBuffer();
    if (!back_buffer) return;
    data->acquired = true;

    auto pass = graph->AddRenderPass("ImGui Viewport");
    pass.Color(graph->ImportTexture(back_buffer, kPresentState, kPresentState),
               viewport->Flags & ImGuiViewportFlags_NoRendererClear
                   ? RM::LoadOp::DontCare
                   : RM::LoadOp::Clear,
               RM::StoreOp::Store,
               {0.f, 0.f, 0.f, 1.f});
    for (const auto texture : m_graph_textures)
        pass.Read(texture, RM::BarrierStage::PixelShading);
    pass.Execute([this, viewport, data, format = back_buffer->GetFormat()](
        RM::RenderGraphContext& context) -> RM::Result<void>
        {
            return Record(context,
                          viewport->DrawData,
                          *data,
                          data->swapchain->GetDisplayInfo(),
                          format);
        });
}

void ImGuiLayer::GetAcquiredSwapchains(std::vector<RM::Swapchain*>& swapchains) const
{
    ImGui::SetCurrentContext(m_context);
    for (auto* viewport : ImGui::GetPlatformIO().Viewports)
    {
        const auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
        if (data && data->acquired) swapchains.push_back(data->swapchain);
    }
}

void ImGuiLayer::Present()
{
    ImGui::SetCurrentContext(m_context);
    for (auto* viewport : ImGui::GetPlatformIO().Viewports)
    {
        auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
        if (!data || !data->acquired) continue;
        data->acquired = false;
        if (auto result = data->swapchain->Present(); !result)
            std::fprintf(stderr, "[ImGui] %s\n", result.error().c_str());
    }
}

RM::Result<void> ImGuiLayer::Record(RM::RenderGraphContext& context,
                                    const ImDrawData* draw_data,
                                    ViewportData& viewport,
                                    const RM::DisplayInfo& display_info,
                                    RM::Format format)
{
    auto* command = context.GetCommand();
    const auto area = context.GetRenderArea();
    const glm::uvec2 size{area.x, area.y};
    if (!command || !draw_data || draw_data->TotalVtxCount <= 0 || !size.x || !size.y) return {};
    const ImVec2 position = draw_data->DisplayPos;
    const ImVec2 display = draw_data->DisplaySize;
    if (display.x <= 0.f || display.y <= 0.f) return {};

    auto pipeline = GetPipeline(format);
    if (!pipeline) return std::unexpected(pipeline.error());

    auto& frame = viewport.frames[viewport.frame_index];
    viewport.frame_index = (viewport.frame_index + 1) % kFramesInFlight;
    if (auto result = Reserve(frame,
                              static_cast<uint32_t>(draw_data->TotalVtxCount),
                              static_cast<uint32_t>(draw_data->TotalIdxCount));
        !result)
        return result;

    auto* vertices = static_cast<ImDrawVert*>(frame.vertex_data);
    auto* indices = static_cast<ImDrawIdx*>(frame.index_data);
    for (const ImDrawList* list : draw_data->CmdLists)
    {
        std::memcpy(vertices, list->VtxBuffer.Data, list->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(indices, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vertices += list->VtxBuffer.Size;
        indices += list->IdxBuffer.Size;
    }

    const glm::vec2 framebuffer_scale{static_cast<float>(size.x) / display.x,
                                      static_cast<float>(size.y) / display.y};
    const Constants constants{
        .vertex_buffer_index = frame.vertices->GetResourceIndex(),
        .sampler_index = m_sampler->GetSamplerIndex(),
        .texture_index = 0,
        .vertex_offset = 0,
        .scale = {2.f / display.x, -2.f / display.y},
        .translate = {-1.f - position.x * 2.f / display.x, 1.f + position.y * 2.f / display.y},
        .hdr = display_info.hdr ? 1u : 0u,
        .sdr_white_scale = display_info.sdr_white_scale,
    };
    if (auto result = BindRenderState(command, *pipeline, frame, constants, size); !result)
        return result;

    auto& platform_io = ImGui::GetPlatformIO();
    m_render_state = {.command = command, .context = &context};
    platform_io.Renderer_RenderState = &m_render_state;

    RM::Result<void> result{};
    uint32_t global_vertex = 0;
    uint32_t global_index = 0;
    std::array<uint32_t, 2> bound{constants.texture_index, constants.vertex_offset};
    for (const ImDrawList* list : draw_data->CmdLists)
    {
        for (const ImDrawCmd& draw : list->CmdBuffer)
        {
            if (draw.UserCallback)
            {
                if (draw.UserCallback != ImDrawCallback_ResetRenderState)
                {
                    draw.UserCallback(list, &draw);
                    bound = {UINT32_MAX, UINT32_MAX};
                    continue;
                }
                result = BindRenderState(command, *pipeline, frame, constants, size);
                if (!result) break;
                bound = {constants.texture_index, constants.vertex_offset};
                continue;
            }

            const glm::vec2 clip_min{
                std::max((draw.ClipRect.x - position.x) * framebuffer_scale.x, 0.f),
                std::max((draw.ClipRect.y - position.y) * framebuffer_scale.y, 0.f)};
            const glm::vec2 clip_max{
                std::min((draw.ClipRect.z - position.x) * framebuffer_scale.x,
                         static_cast<float>(size.x)),
                std::min((draw.ClipRect.w - position.y) * framebuffer_scale.y,
                         static_cast<float>(size.y))};
            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

            command->SetScissor({
                .offset = {.x = static_cast<uint32_t>(clip_min.x),
                           .y = static_cast<uint32_t>(clip_min.y)},
                .size = {.x = static_cast<uint32_t>(clip_max.x - clip_min.x),
                         .y = static_cast<uint32_t>(clip_max.y - clip_min.y)},
            });

            const std::array<uint32_t, 2> draw_constants{ResolveTexture(context, draw.GetTexID()),
                                                         global_vertex + draw.VtxOffset};
            if (draw_constants != bound)
            {
                result = command->BindRootConstants(draw_constants.data(),
                                                    sizeof(draw_constants),
                                                    offsetof(Constants, texture_index));
                if (!result) break;
                bound = draw_constants;
            }
            result = command->DrawIndexed(draw.ElemCount, 1, global_index + draw.IdxOffset, 0, 0);
            if (!result) break;
        }
        if (!result) break;
        global_vertex += static_cast<uint32_t>(list->VtxBuffer.Size);
        global_index += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
    platform_io.Renderer_RenderState = nullptr;
    return result;
}

RM::Result<void> ImGuiLayer::BindRenderState(RM::Command* command,
                                             const RM::Pipeline* pipeline,
                                             const FrameBuffers& frame,
                                             const Constants& constants,
                                             glm::uvec2 size) const
{
    if (auto result = command->BindShader(pipeline); !result) return result;
    command->SetPrimitiveTopology(RM::PrimitiveTopology::TriangleList);
    command->SetViewport({
        .offset = {},
        .size = {.x = size.x, .y = size.y},
        .depth_range = {.x = 0.f, .y = 1.f},
    });
    if (auto result = command->BindIndexBuffer(frame.indices, RM::Format::R16_UInt); !result)
        return result;
    return command->BindRootConstants(&constants, sizeof(constants));
}

RM::Result<void> ImGuiLayer::Reserve(FrameBuffers& frame,
                                     uint32_t vertex_count,
                                     uint32_t index_count)
{
    auto vertices = Grow(frame.vertices,
                         frame.vertex_data,
                         frame.vertex_capacity,
                         vertex_count,
                         sizeof(ImDrawVert),
                         RM::BufferFlags::eShaderResource);
    if (!vertices) return std::unexpected(vertices.error());
    auto indices = Grow(frame.indices,
                        frame.index_data,
                        frame.index_capacity,
                        index_count,
                        sizeof(ImDrawIdx),
                        RM::BufferFlags::eNone);
    if (!indices) return std::unexpected(indices.error());
    if (*vertices || *indices) m_device->GetCoreResidencySet()->Commit();
    return {};
}

RM::Result<bool> ImGuiLayer::Grow(RM::Buffer*& buffer,
                                  void*& data,
                                  uint32_t& capacity,
                                  uint32_t count,
                                  uint32_t stride,
                                  RM::BufferFlags flags) const
{
    if (buffer && count <= capacity) return false;
    auto* residency = m_device->GetCoreResidencySet();
    const uint32_t new_capacity = std::max({count + count / 2, capacity * 2, 1024u});
    if (buffer)
    {
        m_device->Retire(buffer);
        buffer = nullptr;
        data = nullptr;
        capacity = 0;
    }
    auto result = m_device->CreateBuffer({
        .size = new_capacity * stride,
        .data = nullptr,
        .memory_type = RM::MemoryType::HostVisible,
        .flags = flags,
    });
    if (!result) return std::unexpected(result.error());
    buffer = *result;
    auto mapped = buffer->Map();
    if (!mapped) return std::unexpected(mapped.error());
    data = *mapped;
    capacity = new_capacity;
    residency->AddAllocation(buffer);
    return true;
}

void ImGuiLayer::DestroyViewportData(ViewportData& viewport) const
{
    for (auto& frame : viewport.frames)
    {
        m_device->Retire(frame.vertices);
        m_device->Retire(frame.indices);
        frame = {};
    }
    if (viewport.swapchain) m_device->DestroyImmediate(viewport.swapchain);
    viewport.swapchain = nullptr;
    viewport.acquired = false;
}

ImGuiLayer* ImGuiLayer::GetCurrent()
{
    return ImGui::GetCurrentContext()
               ? static_cast<ImGuiLayer*>(ImGui::GetIO().BackendRendererUserData)
               : nullptr;
}

void ImGuiLayer::CreateViewport(ImGuiViewport* viewport)
{
    auto* layer = GetCurrent();
    if (!layer) return;
    auto* data = new ViewportData();
    viewport->RendererUserData = data;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(viewport->PlatformHandle), &width, &height);
    auto swapchain = layer->m_device->CreateSwapchain(layer->m_queue,
                                                      {
                                                          .window = viewport->PlatformHandleRaw,
                                                          .width = static_cast<uint32_t>(
                                                              std::max(width, 1)),
                                                          .height = static_cast<uint32_t>(
                                                              std::max(height, 1)),
                                                      });
    if (!swapchain)
    {
        std::fprintf(stderr, "[ImGui] %s\n", swapchain.error().c_str());
        return;
    }
    data->swapchain = *swapchain;
}

void ImGuiLayer::DestroyViewport(ImGuiViewport* viewport)
{
    auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
    viewport->RendererUserData = nullptr;
    if (!data) return;
    if (auto* layer = GetCurrent()) layer->DestroyViewportData(*data);
    delete data;
}

void ImGuiLayer::SetViewportSize(ImGuiViewport* viewport, ImVec2)
{
    auto* data = static_cast<ViewportData*>(viewport->RendererUserData);
    if (!data || !data->swapchain) return;
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(viewport->PlatformHandle), &width, &height);
    if (auto result = data->swapchain->Resize(static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height));
        !result)
        std::fprintf(stderr, "[ImGui] %s\n", result.error().c_str());
}

bool ImGuiLayer::OpenInShell(ImGuiContext*, const char* path)
{
    Window::OpenInShell(path);
    return true;
}

bool ImGuiLayer::WantsMouse() const
{
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantsKeyboard() const
{
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiLayer::WantsTextInput() const
{
    ImGui::SetCurrentContext(m_context);
    return ImGui::GetIO().WantTextInput;
}

namespace
{
    constexpr double kMegabyte = 1024.0 * 1024.0;
    constexpr float kPlotWidth = 480.f;
    constexpr ImPlotFlags kPlotFlags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText |
                                       ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;

    void DrawPlotRect(double x0, double y0, double x1, double y1, ImU32 color)
    {
        const ImVec2 a = ImPlot::PlotToPixels(x0, y0);
        const ImVec2 b = ImPlot::PlotToPixels(x1, y1);
        ImPlot::GetPlotDrawList()->AddRectFilled(ImVec2(std::min(a.x, b.x), std::min(a.y, b.y)),
                                                 ImVec2(std::max(a.x, b.x), std::max(a.y, b.y)),
                                                 color,
                                                 2.f);
    }

    void ShowResourceTooltip(const RM::RenderGraphResourceInfo& resource)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(resource.name.c_str());
        ImGui::Text("%s, %.2f MB at offset %.2f MB",
                    resource.texture ? "Texture" : "Buffer",
                    static_cast<double>(resource.size) / kMegabyte,
                    static_cast<double>(resource.offset) / kMegabyte);
        ImGui::Text("Steps %u to %u", resource.first_step, resource.last_step);
        ImGui::EndTooltip();
    }

    ImU32 ResourceColor(size_t index)
    {
        return ImGui::GetColorU32(ImPlot::GetColormapColor(static_cast<int>(index)));
    }
}

void ImGuiLayer::ShowStatistics(const RM::RenderGraph& graph)
{
    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plot_context);
    const auto& io = ImGui::GetIO();
    const auto& statistics = graph.GetStatistics();
    const auto& timeline = graph.GetTimeline();

    m_frame_times[m_frame_time_index] = io.DeltaTime * 1000.f;
    m_frame_time_index = (m_frame_time_index + 1) % kFrameTimeHistory;

    const ImVec2 origin = ImGui::GetMainViewport()->Pos;
    ImGui::SetNextWindowPos(ImVec2(origin.x + 10.f, origin.y + 10.f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const float frame_time = io.Framerate > 0.f ? 1000.f / io.Framerate : 0.f;
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, frame_time);

    const char* backend = "Unknown";
    switch (m_device->GetBackendType())
    {
        case RM::BackendType::D3D12:
            backend = "D3D12";
            break;
        case RM::BackendType::Metal:
            backend = "Metal";
            break;
        case RM::BackendType::Vulkan:
            backend = "Vulkan";
            break;
    }
    ImGui::Text("Backend: %s", backend);

    if (ImGui::CollapsingHeader("Frame time", ImGuiTreeNodeFlags_DefaultOpen) &&
        ImPlot::BeginPlot("##FrameTime", ImVec2(kPlotWidth, 120.f), kPlotFlags))
    {
        const float highest = *std::ranges::max_element(m_frame_times);
        ImPlot::SetupAxes(nullptr,
                          "ms",
                          ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_Lock,
                          ImPlotAxisFlags_Lock);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, kFrameTimeHistory - 1, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1,
                                0.0,
                                std::max(1.0, static_cast<double>(highest) * 1.2),
                                ImPlotCond_Always);
        ImPlotSpec spec;
        spec.Offset = static_cast<int>(m_frame_time_index);
        ImPlot::PlotLine("Frame time",
                         m_frame_times.data(),
                         kFrameTimeHistory,
                         1.0,
                         0.0,
                         spec);
        ImPlot::EndPlot();
    }

    if (ImGui::CollapsingHeader("Render graph", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Text("Passes: %u executed, %u culled, %u declared",
                    statistics.executed_passes,
                    statistics.culled_passes,
                    statistics.declared_passes);
        ImGui::Text("Parallel batches: %u", statistics.parallel_batches);
        ImGui::Text("Barriers: %u", statistics.barriers);
        ImGui::Text("Transient: %u textures, %u buffers",
                    statistics.transient_textures,
                    statistics.transient_buffers);
        ImGui::Text("Aliased %.2f MB, unaliased %.2f MB, heap %.2f MB",
                    static_cast<double>(statistics.transient_memory) / kMegabyte,
                    static_cast<double>(statistics.transient_memory_unaliased) / kMegabyte,
                    static_cast<double>(statistics.heap_size) / kMegabyte);
        if (statistics.transient_memory_unaliased)
            ImGui::Text("Saved by aliasing: %.1f%%",
                        100.0 * (1.0 - static_cast<double>(statistics.transient_memory) /
                                 static_cast<double>(
                                     statistics.transient_memory_unaliased)));
    }

    std::vector<std::string> step_names(timeline.step_count);
    std::string culled;
    for (const auto& pass : timeline.passes)
    {
        if (pass.step == UINT32_MAX)
        {
            culled += culled.empty() ? pass.name : ", " + pass.name;
            continue;
        }
        auto& name = step_names[pass.step];
        name += name.empty() ? pass.name : " + " + pass.name;
    }
    std::vector<double> step_ticks;
    std::vector<const char*> step_labels;
    for (uint32_t step = 0; step < timeline.step_count; ++step)
    {
        step_ticks.push_back(step + 0.5);
        step_labels.push_back(step_names[step].c_str());
    }
    std::vector<double> resource_ticks;
    std::vector<const char*> resource_labels;
    for (size_t i = 0; i < timeline.resources.size(); ++i)
    {
        resource_ticks.push_back(static_cast<double>(i));
        resource_labels.push_back(timeline.resources[i].name.c_str());
    }

    if (ImGui::CollapsingHeader("Pass timeline", ImGuiTreeNodeFlags_DefaultOpen) &&
        timeline.step_count)
    {
        const float height = 70.f + 24.f * static_cast<float>(timeline.resources.size());
        if (ImPlot::BeginPlot("##Lifetimes", ImVec2(kPlotWidth, height), kPlotFlags))
        {
            ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxis(ImAxis_Y1,
                              nullptr,
                              ImPlotAxisFlags_Lock | ImPlotAxisFlags_Invert |
                              ImPlotAxisFlags_NoGridLines);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, timeline.step_count, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1,
                                    -0.5,
                                    std::max<double>(1.0, timeline.resources.size()) - 0.5,
                                    ImPlotCond_Always);
            ImPlot::SetupAxisTicks(ImAxis_X1,
                                   step_ticks.data(),
                                   static_cast<int>(step_ticks.size()),
                                   step_labels.data());
            if (!resource_ticks.empty())
                ImPlot::SetupAxisTicks(ImAxis_Y1,
                                       resource_ticks.data(),
                                       static_cast<int>(resource_ticks.size()),
                                       resource_labels.data());

            ImPlot::PushPlotClipRect();
            for (size_t i = 0; i < timeline.resources.size(); ++i)
            {
                const auto& resource = timeline.resources[i];
                DrawPlotRect(resource.first_step,
                             static_cast<double>(i) - 0.35,
                             resource.last_step + 1.0,
                             static_cast<double>(i) + 0.35,
                             ResourceColor(i));
            }
            ImPlot::PopPlotClipRect();

            if (ImPlot::IsPlotHovered())
            {
                const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                const double row = std::round(mouse.y);
                if (row >= 0.0 && row < static_cast<double>(timeline.resources.size()))
                {
                    const auto& resource = timeline.resources[static_cast<size_t>(row)];
                    if (mouse.x >= resource.first_step && mouse.x <= resource.last_step + 1.0)
                        ShowResourceTooltip(resource);
                }
            }
            ImPlot::EndPlot();
        }
        if (!culled.empty()) ImGui::TextWrapped("Culled: %s", culled.c_str());
    }

    if (ImGui::CollapsingHeader("Transient memory layout") && !timeline.resources.empty())
    {
        const double heap = static_cast<double>(std::max(statistics.heap_size,
                                                         statistics.transient_memory)) /
                            kMegabyte;
        if (ImPlot::BeginPlot("##Memory", ImVec2(kPlotWidth, 240.f), kPlotFlags))
        {
            ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_Lock);
            ImPlot::SetupAxis(ImAxis_Y1, "MB", ImPlotAxisFlags_Lock | ImPlotAxisFlags_Invert);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, timeline.step_count, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, std::max(heap, 1.0), ImPlotCond_Always);
            ImPlot::SetupAxisTicks(ImAxis_X1,
                                   step_ticks.data(),
                                   static_cast<int>(step_ticks.size()),
                                   step_labels.data());

            ImPlot::PushPlotClipRect();
            for (size_t i = 0; i < timeline.resources.size(); ++i)
            {
                const auto& resource = timeline.resources[i];
                DrawPlotRect(resource.first_step,
                             static_cast<double>(resource.offset) / kMegabyte,
                             resource.last_step + 1.0,
                             static_cast<double>(resource.offset + resource.size) / kMegabyte,
                             ResourceColor(i));
            }
            ImPlot::PopPlotClipRect();

            if (ImPlot::IsPlotHovered())
            {
                const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
                for (const auto& resource : timeline.resources)
                {
                    const double begin = static_cast<double>(resource.offset) / kMegabyte;
                    const double end =
                        static_cast<double>(resource.offset + resource.size) / kMegabyte;
                    if (mouse.x < resource.first_step || mouse.x > resource.last_step + 1.0 ||
                        mouse.y < begin || mouse.y > end)
                        continue;
                    ShowResourceTooltip(resource);
                    break;
                }
            }
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}