#pragma once
#include "rimera_render_graph.hpp"
#include "glm/glm.hpp"
#include "array"
#include "unordered_map"
#include "vector"

struct ImDrawData;
struct ImGuiContext;
struct ImGuiViewport;
struct ImPlotContext;
struct ImVec2;
class Window;

namespace RM
{
    class Buffer;
    class Command;
    class Device;
    class Pipeline;
    class Queue;
    class Sampler;
    class Swapchain;
    class Texture;
}

struct ImGuiRenderState
{
    RM::Command* command = nullptr;
    RM::RenderGraphContext* context = nullptr;
};

class ImGuiLayer
{
public:
    [[nodiscard]] static RM::Result<ImGuiLayer*> Create(RM::Device* device,
                                                        RM::Queue* queue,
                                                        Window* window,
                                                        RM::Format format);
    static void Destroy(const ImGuiLayer* layer);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void NewFrame(float delta_time);
    void SetDisplayInfo(const RM::DisplayInfo& info) { m_display_info = info; }
    void AddPass(RM::RenderGraph* graph, RM::GraphTexture target);
    void Present();
    void GetAcquiredSwapchains(std::vector<RM::Swapchain*>& swapchains) const;

    [[nodiscard]] uint64_t GetTextureID(const RM::Texture* texture) const;
    [[nodiscard]] uint64_t GetTextureID(RM::GraphTexture texture);

    [[nodiscard]] bool WantsMouse() const;
    [[nodiscard]] bool WantsKeyboard() const;
    [[nodiscard]] bool WantsTextInput() const;

    void ShowStatistics(const RM::RenderGraph& graph);

private:
    static constexpr uint32_t kFramesInFlight = 3;
    static constexpr uint32_t kFrameTimeHistory = 240;
    static constexpr uint64_t kGraphTextureBit = 1ull << 63;

    struct Constants
    {
        uint32_t vertex_buffer_index;
        uint32_t sampler_index;
        uint32_t texture_index;
        uint32_t vertex_offset;
        glm::vec2 scale;
        glm::vec2 translate;
        uint32_t hdr;
        float sdr_white_scale;
    };

    static_assert(sizeof(Constants) == 40);

    struct FrameBuffers
    {
        RM::Buffer* vertices = nullptr;
        RM::Buffer* indices = nullptr;
        void* vertex_data = nullptr;
        void* index_data = nullptr;
        uint32_t vertex_capacity = 0;
        uint32_t index_capacity = 0;
    };

    struct ViewportData
    {
        RM::Swapchain* swapchain = nullptr;
        std::array<FrameBuffers, kFramesInFlight> frames{};
        uint32_t frame_index = 0;
        bool acquired = false;
    };

    ImGuiLayer() = default;
    RM::Result<void> Initialize(RM::Device* device,
                                RM::Queue* queue,
                                Window* window,
                                RM::Format format);
    RM::Result<RM::Pipeline*> GetPipeline(RM::Format format);
    RM::Result<void> RebuildFontAtlas();
    void AddViewportPass(RM::RenderGraph* graph, ImGuiViewport* viewport);
    RM::Result<void> Record(RM::RenderGraphContext& context,
                            const ImDrawData* draw_data,
                            ViewportData& viewport,
                            const RM::DisplayInfo& display_info,
                            RM::Format format);
    RM::Result<void> Reserve(FrameBuffers& frame, uint32_t vertex_count, uint32_t index_count);
    RM::Result<bool> Grow(RM::Buffer*& buffer,
                          void*& data,
                          uint32_t& capacity,
                          uint32_t count,
                          uint32_t stride,
                          RM::BufferFlags flags) const;
    RM::Result<void> BindRenderState(RM::Command* command,
                                     const RM::Pipeline* pipeline,
                                     const FrameBuffers& frame,
                                     const Constants& constants,
                                     glm::uvec2 size) const;
    [[nodiscard]] uint32_t ResolveTexture(const RM::RenderGraphContext& context,
                                          uint64_t texture_id) const;
    void DestroyViewportData(ViewportData& viewport) const;

    static ImGuiLayer* GetCurrent();
    static void CreateViewport(ImGuiViewport* viewport);
    static void DestroyViewport(ImGuiViewport* viewport);
    static void SetViewportSize(ImGuiViewport* viewport, ImVec2 size);
    static bool OpenInShell(ImGuiContext* context, const char* path);

    RM::Device* m_device = nullptr;
    RM::Queue* m_queue = nullptr;
    Window* m_window = nullptr;
    RM::Format m_format = RM::Format::Unknown;
    ImGuiContext* m_context = nullptr;
    ImPlotContext* m_plot_context = nullptr;
    bool m_platform_initialized = false;
    std::unordered_map<RM::Format, RM::Pipeline*> m_pipelines;
    RM::Shader* m_vertex_shader = nullptr;
    RM::Shader* m_fragment_shader = nullptr;
    RM::Sampler* m_sampler = nullptr;
    RM::Texture* m_font_texture = nullptr;
    float m_font_scale = 0.f;
    float m_framebuffer_ratio = 0.f;
    ViewportData m_main_viewport{};
    std::vector<RM::GraphTexture> m_graph_textures;
    ImGuiRenderState m_render_state{};
    RM::DisplayInfo m_display_info{};
    std::array<float, kFrameTimeHistory> m_frame_times{};
    uint32_t m_frame_time_index = 0;
};
