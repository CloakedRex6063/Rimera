#include "imgui_layer.hpp"
#include "window.hpp"
#include "imgui.h"
#include "implot.h"
#include "rimera_buffer.hpp"
#include "rimera_builders.hpp"
#include "rimera_command.hpp"
#include "rimera_device.hpp"
#include "rimera_residency.hpp"
#include "rimera_sampler.hpp"
#include "rimera_texture.hpp"
#include "algorithm"
#include "cmath"
#include "cstddef"
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

        float3 SrgbToLinear(float3 color)
        {
            return select(color <= 0.04045, color / 12.92, pow((color + 0.055) / 1.055, 2.4));
        }

        VSOut VSMain(uint vertex_id : SV_VertexID)
        {
            StructuredBuffer<ImGuiVertex> vertices = ResourceDescriptorHeap[vertex_buffer_index];
            ImGuiVertex vertex = vertices.Load(vertex_id + vertex_offset);
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

    ImGuiKey ToImGuiKey(Key key)
    {
        const auto value = static_cast<int>(key);
        if (key >= Key::A && key <= Key::Z)
            return static_cast<ImGuiKey>(ImGuiKey_A + value - static_cast<int>(Key::A));
        if (key >= Key::Num0 && key <= Key::Num9)
            return static_cast<ImGuiKey>(ImGuiKey_0 + value - static_cast<int>(Key::Num0));
        switch (key)
        {
            case Key::Space:
                return ImGuiKey_Space;
            case Key::Enter:
                return ImGuiKey_Enter;
            case Key::Escape:
                return ImGuiKey_Escape;
            case Key::Tab:
                return ImGuiKey_Tab;
            case Key::Shift:
                return ImGuiKey_LeftShift;
            case Key::Control:
                return ImGuiKey_LeftCtrl;
            case Key::Alt:
                return ImGuiKey_LeftAlt;
            case Key::Left:
                return ImGuiKey_LeftArrow;
            case Key::Right:
                return ImGuiKey_RightArrow;
            case Key::Up:
                return ImGuiKey_UpArrow;
            case Key::Down:
                return ImGuiKey_DownArrow;
            case Key::Backspace:
                return ImGuiKey_Backspace;
            case Key::Delete:
                return ImGuiKey_Delete;
            case Key::Home:
                return ImGuiKey_Home;
            case Key::End:
                return ImGuiKey_End;
            case Key::PageUp:
                return ImGuiKey_PageUp;
            case Key::PageDown:
                return ImGuiKey_PageDown;
            default:
                return ImGuiKey_None;
        }
    }

    ImGuiKey ToImGuiModifier(Key key)
    {
        switch (key)
        {
            case Key::Shift:
                return ImGuiMod_Shift;
            case Key::Control:
                return ImGuiMod_Ctrl;
            case Key::Alt:
                return ImGuiMod_Alt;
            default:
                return ImGuiKey_None;
        }
    }
}

RM::Result<ImGuiLayer*> ImGuiLayer::Create(RM::Device* device, Window* window, RM::Format format)
{
    auto* layer = new (std::nothrow) ImGuiLayer();
    if (!layer) return std::unexpected("Failed to allocate the ImGui layer");
    if (auto result = layer->Initialize(device, window, format); !result)
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
    for (auto& frame : m_frames) DestroyFrame(frame);
    if (m_font_texture)
    {
        m_device->GetCoreResidencySet()->RemoveAllocation(m_font_texture);
        m_device->DestroyTexture(m_font_texture);
    }
    if (m_device) m_device->GetCoreResidencySet()->Commit();
    if (m_sampler) m_device->DestroySampler(m_sampler);
    if (m_pipeline) m_device->DestroyPipeline(m_pipeline);
    if (m_plot_context) ImPlot::DestroyContext(m_plot_context);
    if (m_context) ImGui::DestroyContext(m_context);
}

RM::Result<void> ImGuiLayer::Initialize(RM::Device* device, Window* window, RM::Format format)
{
    if (!device || !window) return std::unexpected("Invalid ImGui layer creation parameters");
    m_device = device;
    m_window = window;

    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    if (!m_context) return std::unexpected("Failed to create the ImGui context");
    ImGui::SetCurrentContext(m_context);
    ImGui::StyleColorsDark();
    m_plot_context = ImPlot::CreateContext();
    if (!m_plot_context) return std::unexpected("Failed to create the ImPlot context");

    auto& io = ImGui::GetIO();
    io.BackendRendererName = "Rimera";
    io.BackendPlatformName = "Rimera Window";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

    if (auto result = CreatePipeline(format); !result) return result;
    if (auto result = CreateFontTexture(); !result) return result;

    auto sampler = m_device->CreateSampler({
        .wrap_u = RM::Wrap::ClampToEdge,
        .wrap_v = RM::Wrap::ClampToEdge,
        .wrap_w = RM::Wrap::ClampToEdge,
    });
    if (!sampler) return std::unexpected(sampler.error());
    m_sampler = *sampler;
    return {};
}

RM::Result<void> ImGuiLayer::CreatePipeline(RM::Format format)
{
    auto vertex_shader = RM::ShaderBuilder(kShader).EntryPoint("VSMain").Build();
    auto fragment_shader = RM::ShaderBuilder(kShader).EntryPoint("PSMain").Build();
    auto pipeline_info = RM::GraphicsPipelineBuilder(vertex_shader, fragment_shader)
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
    m_pipeline = *pipeline;
    return {};
}

RM::Result<void> ImGuiLayer::CreateFontTexture()
{
    auto& io = ImGui::GetIO();
    const float scale = std::max(1.f, m_window->GetContentScale());
    ImFontConfig font_config;
    font_config.SizePixels = 13.f * scale;
    io.Fonts->AddFontDefault(&font_config);
    io.FontGlobalScale = 1.f / scale;

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
    m_font_texture = *texture;
    m_device->GetCoreResidencySet()->AddAllocation(m_font_texture);
    m_device->GetCoreResidencySet()->Commit();
    const auto index = m_font_texture->GetResourceIndex(RM::TextureViewType::ShaderRead);
    io.Fonts->SetTexID(static_cast<ImTextureID>(index));
    return {};
}

void ImGuiLayer::NewFrame(float delta_time)
{
    ImGui::SetCurrentContext(m_context);
    auto& io = ImGui::GetIO();
    const auto size = m_window->GetSize();
    io.DisplaySize = ImVec2(static_cast<float>(size.x), static_cast<float>(size.y));
    io.DeltaTime = delta_time > 0.f ? delta_time : 1.f / 60.f;
    UpdateInput();
    ImGui::NewFrame();
}

void ImGuiLayer::UpdateInput() const
{
    auto& io = ImGui::GetIO();
    const auto mouse = m_window->GetMousePosition();
    io.AddMousePosEvent(mouse.x, mouse.y);

    for (uint32_t i = 0; i < static_cast<uint32_t>(MouseButton::Count); ++i)
    {
        const auto button = static_cast<MouseButton>(i);
        if (m_window->IsMouseButtonPressed(button))
            io.AddMouseButtonEvent(static_cast<int>(i), true);
        else if (m_window->IsMouseButtonReleased(button))
            io.AddMouseButtonEvent(static_cast<int>(i), false);
    }

    if (const float scroll = m_window->GetScrollDelta(); scroll != 0.f)
        io.AddMouseWheelEvent(0.f, scroll);

    for (uint32_t i = 0; i < static_cast<uint32_t>(Key::Count); ++i)
    {
        const auto key = static_cast<Key>(i);
        const bool pressed = m_window->IsKeyPressed(key);
        if (!pressed && !m_window->IsKeyReleased(key)) continue;
        if (const auto modifier = ToImGuiModifier(key); modifier != ImGuiKey_None)
            io.AddKeyEvent(modifier, pressed);
        if (const auto imgui_key = ToImGuiKey(key); imgui_key != ImGuiKey_None)
            io.AddKeyEvent(imgui_key, pressed);
    }

    for (const char32_t character : m_window->GetTextInput())
        io.AddInputCharacter(static_cast<unsigned int>(character));
}

void ImGuiLayer::AddPass(RM::RenderGraph* graph, RM::GraphTexture target)
{
    ImGui::SetCurrentContext(m_context);
    ImGui::Render();
    graph->AddRenderPass("ImGui")
         .Color(target)
         .Execute([this](RM::RenderGraphContext& context) -> RM::Result<void>
                  {
                      const auto area = context.GetRenderArea();
                      return Record(context.GetCommand(), {area.x, area.y});
                  });
}

RM::Result<void> ImGuiLayer::Record(RM::Command* command, glm::uvec2 size)
{
    ImGui::SetCurrentContext(m_context);
    const ImDrawData* draw_data = ImGui::GetDrawData();
    if (!command || !draw_data || draw_data->TotalVtxCount <= 0 || !size.x || !size.y) return {};
    const ImVec2 position = draw_data->DisplayPos;
    const ImVec2 display = draw_data->DisplaySize;
    if (display.x <= 0.f || display.y <= 0.f) return {};

    auto& frame = m_frames[m_frame_index];
    m_frame_index = (m_frame_index + 1) % kFramesInFlight;
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
        .hdr = m_display_info.hdr ? 1u : 0u,
        .sdr_white_scale = m_display_info.sdr_white_scale,
    };
    if (auto result = BindRenderState(command, frame, constants, size); !result) return result;

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
                if (auto result = BindRenderState(command, frame, constants, size); !result)
                    return result;
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

            const std::array<uint32_t, 2> draw_constants{static_cast<uint32_t>(draw.GetTexID()),
                                                         global_vertex + draw.VtxOffset};
            if (draw_constants != bound)
            {
                if (auto result = command->BindRootConstants(draw_constants.data(),
                                                             sizeof(draw_constants),
                                                             offsetof(Constants, texture_index));
                    !result)
                    return result;
                bound = draw_constants;
            }
            if (auto result = command->DrawIndexed(draw.ElemCount,
                                                   1,
                                                   global_index + draw.IdxOffset,
                                                   0,
                                                   0);
                !result)
                return result;
        }
        global_vertex += static_cast<uint32_t>(list->VtxBuffer.Size);
        global_index += static_cast<uint32_t>(list->IdxBuffer.Size);
    }
    return {};
}

RM::Result<void> ImGuiLayer::BindRenderState(RM::Command* command,
                                             const FrameBuffers& frame,
                                             const Constants& constants,
                                             glm::uvec2 size) const
{
    if (auto result = command->BindShader(m_pipeline); !result) return result;
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
        residency->RemoveAllocation(buffer);
        m_device->DestroyBuffer(buffer);
        buffer = nullptr;
        data = nullptr;
        capacity = 0;
    }
    auto result = m_device->CreateBuffer({
        .size = new_capacity * stride,
        .data = nullptr,
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

void ImGuiLayer::DestroyFrame(FrameBuffers& frame) const
{
    auto* residency = m_device ? m_device->GetCoreResidencySet() : nullptr;
    for (auto* buffer : {frame.vertices, frame.indices})
    {
        if (!buffer) continue;
        residency->RemoveAllocation(buffer);
        m_device->DestroyBuffer(buffer);
    }
    frame = {};
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

    ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.8f);
    if (!ImGui::Begin("Statistics", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const float frame_time = io.Framerate > 0.f ? 1000.f / io.Framerate : 0.f;
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, frame_time);

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
                         static_cast<int>(kFrameTimeHistory),
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
