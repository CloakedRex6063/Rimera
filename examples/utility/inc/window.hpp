#pragma once
#include "glm/glm.hpp"

class Window
{
public:
    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] bool IsRunning() const { return m_running; }
    [[nodiscard]] bool IsResized() const;
    void Tick();

    [[nodiscard]] glm::uvec2 GetSize() const { return m_size; }
    [[nodiscard]] void* GetNativeHandle() const;

private:
    bool m_running = false;
    bool m_resized = false;
    glm::uvec2 m_size{1280, 720};
    void* m_window = nullptr;
};
