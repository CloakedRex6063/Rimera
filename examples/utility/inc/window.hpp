#pragma once
#include "rimera_result.hpp"
#include "glm/glm.hpp"

class Window
{
public:
    [[nodiscard]] static RM::Result<Window*> Create();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool IsRunning() const { return m_running; }
    [[nodiscard]] bool IsResized() const;
    void Tick();

    [[nodiscard]] glm::uvec2 GetSize() const { return m_size; }
    [[nodiscard]] void* GetNativeHandle() const;

private:
    Window() = default;
    RM::Result<void> Initialize();
    bool m_running = false;
    bool m_resized = false;
    glm::uvec2 m_size{1280, 720};
    void* m_window = nullptr;
};
