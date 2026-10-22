#pragma once
#include "rimera_result.hpp"
#include "glm/glm.hpp"
#include "array"
#include "string"

enum class Key : uint8_t
{
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Space,
    Enter,
    Escape,
    Tab,
    Shift,
    Control,
    Alt,
    Left,
    Right,
    Up,
    Down,
    Backspace,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Count
};

enum class MouseButton : uint8_t
{
    Left,
    Right,
    Middle,
    Count
};

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
    [[nodiscard]] glm::uvec2 GetFramebufferSize() const { return m_framebuffer_size; }
    [[nodiscard]] float GetContentScale() const { return m_content_scale; }
    [[nodiscard]] void* GetNativeHandle() const;

    [[nodiscard]] bool IsKeyDown(Key key) const { return m_keys[static_cast<size_t>(key)]; }
    [[nodiscard]] bool IsKeyPressed(Key key) const
    {
        return m_keys[static_cast<size_t>(key)] && !m_previous_keys[static_cast<size_t>(key)];
    }
    [[nodiscard]] bool IsKeyReleased(Key key) const
    {
        return !m_keys[static_cast<size_t>(key)] && m_previous_keys[static_cast<size_t>(key)];
    }
    [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const
    {
        return m_mouse_buttons[static_cast<size_t>(button)];
    }
    [[nodiscard]] bool IsMouseButtonPressed(MouseButton button) const
    {
        return m_mouse_buttons[static_cast<size_t>(button)] &&
               !m_previous_mouse_buttons[static_cast<size_t>(button)];
    }
    [[nodiscard]] bool IsMouseButtonReleased(MouseButton button) const
    {
        return !m_mouse_buttons[static_cast<size_t>(button)] &&
               m_previous_mouse_buttons[static_cast<size_t>(button)];
    }
    [[nodiscard]] glm::vec2 GetMousePosition() const { return m_mouse_position; }
    [[nodiscard]] glm::vec2 GetMouseDelta() const { return m_mouse_delta; }
    [[nodiscard]] float GetScrollDelta() const { return m_scroll_delta; }
    [[nodiscard]] const std::u32string& GetTextInput() const { return m_text_input; }

private:
    Window() = default;
    RM::Result<void> Initialize();
    void BeginInput();
    void ClearInput();
    void SetKey(Key key, bool down) { m_keys[static_cast<size_t>(key)] = down; }
    void SetMouseButton(MouseButton button, bool down)
    {
        m_mouse_buttons[static_cast<size_t>(button)] = down;
    }
    void AddTextInput(char32_t character);
    bool m_running = false;
    bool m_resized = false;
    glm::uvec2 m_size{1280, 720};
    glm::uvec2 m_framebuffer_size{1280, 720};
    float m_content_scale = 1.f;
    void* m_window = nullptr;
    std::array<bool, static_cast<size_t>(Key::Count)> m_keys{};
    std::array<bool, static_cast<size_t>(Key::Count)> m_previous_keys{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_mouse_buttons{};
    std::array<bool, static_cast<size_t>(MouseButton::Count)> m_previous_mouse_buttons{};
    glm::vec2 m_mouse_position{};
    glm::vec2 m_mouse_delta{};
    float m_scroll_delta = 0.f;
    std::u32string m_text_input;
};
