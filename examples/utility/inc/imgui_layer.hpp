#pragma once
#include "rimera_render_graph.hpp"
#include "glm/glm.hpp"
#include "array"

struct ImGuiContext;
class Window;

namespace RM
{
    class Buffer;
    class Command;
    class Device;
    class Pipeline;
    class Sampler;
    class Texture;
}

class ImGuiLayer
{
public:
    [[nodiscard]] static RM::Result<ImGuiLayer*> Create(RM::Device* device,
                                                        Window* window,
                                                        RM::Format format);
    static void Destroy(const ImGuiLayer* layer);
    ~ImGuiLayer();

    ImGuiLayer(const ImGuiLayer&) = delete;
    ImGuiLayer& operator=(const ImGuiLayer&) = delete;

    void NewFrame(float delta_time);
    void AddPass(RM::RenderGraph* graph, RM::GraphTexture target);
    [[nodiscard]] RM::Result<void> Record(RM::Command* command, glm::uvec2 size);

    [[nodiscard]] bool WantsMouse() const;
    [[nodiscard]] bool WantsKeyboard() const;

    void ShowStatistics(const RM::RenderGraph& graph) const;

private:
    static constexpr uint32_t kFramesInFlight = 3;

    struct Constants
    {
        uint32_t vertex_buffer_index;
        uint32_t sampler_index;
        uint32_t texture_index;
        uint32_t vertex_offset;
        glm::vec2 scale;
        glm::vec2 translate;
    };

    static_assert(sizeof(Constants) == 32);

    struct FrameBuffers
    {
        RM::Buffer* vertices = nullptr;
        RM::Buffer* indices = nullptr;
        void* vertex_data = nullptr;
        void* index_data = nullptr;
        uint32_t vertex_capacity = 0;
        uint32_t index_capacity = 0;
    };

    ImGuiLayer() = default;
    RM::Result<void> Initialize(RM::Device* device, Window* window, RM::Format format);
    RM::Result<void> CreatePipeline(RM::Format format);
    RM::Result<void> CreateFontTexture();
    RM::Result<void> Reserve(FrameBuffers& frame, uint32_t vertex_count, uint32_t index_count);
    RM::Result<bool> Grow(RM::Buffer*& buffer,
                          void*& data,
                          uint32_t& capacity,
                          uint32_t count,
                          uint32_t stride,
                          RM::BufferFlags flags) const;
    RM::Result<void> BindRenderState(RM::Command* command,
                                     const FrameBuffers& frame,
                                     const Constants& constants,
                                     glm::uvec2 size) const;
    void UpdateInput() const;
    void DestroyFrame(FrameBuffers& frame) const;

    RM::Device* m_device = nullptr;
    Window* m_window = nullptr;
    ImGuiContext* m_context = nullptr;
    RM::Pipeline* m_pipeline = nullptr;
    RM::Sampler* m_sampler = nullptr;
    RM::Texture* m_font_texture = nullptr;
    std::array<FrameBuffers, kFramesInFlight> m_frames{};
    uint32_t m_frame_index = 0;
};
