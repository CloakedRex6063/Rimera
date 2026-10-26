#include "window.hpp"
#include "optional"

#ifdef _WIN32
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#elifdef __APPLE__
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include "GLFW/glfw3.h"
#ifdef __linux__
extern "C"
{
unsigned long glfwGetX11Window(GLFWwindow* window);
void* glfwGetX11Display(void);
}
#else
#include "GLFW/glfw3native.h"
#endif

#ifdef _WIN32
#include "shellapi.h"
#elifdef __linux__
#include "spawn.h"
extern char** environ;
#elifdef __APPLE__
#include "CoreServices/CoreServices.h"
#endif

namespace
{
    uint32_t g_window_count = 0;

    std::optional<Key> ToKey(int key)
    {
        if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
            return static_cast<Key>(static_cast<int>(Key::A) + key - GLFW_KEY_A);
        if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
            return static_cast<Key>(static_cast<int>(Key::Num0) + key - GLFW_KEY_0);
        switch (key)
        {
            case GLFW_KEY_SPACE:
                return Key::Space;
            case GLFW_KEY_ENTER:
                return Key::Enter;
            case GLFW_KEY_ESCAPE:
                return Key::Escape;
            case GLFW_KEY_TAB:
                return Key::Tab;
            case GLFW_KEY_LEFT_SHIFT:
            case GLFW_KEY_RIGHT_SHIFT:
                return Key::Shift;
            case GLFW_KEY_LEFT_CONTROL:
            case GLFW_KEY_RIGHT_CONTROL:
                return Key::Control;
            case GLFW_KEY_LEFT_ALT:
            case GLFW_KEY_RIGHT_ALT:
                return Key::Alt;
            case GLFW_KEY_LEFT:
                return Key::Left;
            case GLFW_KEY_RIGHT:
                return Key::Right;
            case GLFW_KEY_UP:
                return Key::Up;
            case GLFW_KEY_DOWN:
                return Key::Down;
            case GLFW_KEY_BACKSPACE:
                return Key::Backspace;
            case GLFW_KEY_DELETE:
                return Key::Delete;
            case GLFW_KEY_HOME:
                return Key::Home;
            case GLFW_KEY_END:
                return Key::End;
            case GLFW_KEY_PAGE_UP:
                return Key::PageUp;
            case GLFW_KEY_PAGE_DOWN:
                return Key::PageDown;
            default:
                return std::nullopt;
        }
    }

    bool IsSideDown(GLFWwindow* window, int left, int right)
    {
        return glfwGetKey(window, left) == GLFW_PRESS || glfwGetKey(window, right) == GLFW_PRESS;
    }

    Window* FromHandle(GLFWwindow* handle)
    {
        return static_cast<Window*>(glfwGetWindowUserPointer(handle));
    }
}

RM::Result<Window*> Window::Create()
{
    auto* window = new (std::nothrow) Window();
    if (!window) return std::unexpected("Window allocation failed");
    auto result = window->Initialize();
    if (!result)
    {
        auto error = std::move(result.error());
        delete window;
        return std::unexpected(std::move(error));
    }
    return window;
}

RM::Result<void> Window::Initialize()
{
    if (!g_window_count && !glfwInit()) return std::unexpected("Failed to initialize GLFW");
    ++g_window_count;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    m_window = glfwCreateWindow(static_cast<int>(m_size.x),
                                static_cast<int>(m_size.y),
                                "Rimera",
                                nullptr,
                                nullptr);
    if (!m_window) return std::unexpected("Failed to create window");

    glfwSetWindowUserPointer(m_window, this);
    glfwSetKeyCallback(m_window, OnKey);
    glfwSetMouseButtonCallback(m_window, OnMouseButton);
    glfwSetCursorPosCallback(m_window, OnCursorPosition);
    glfwSetScrollCallback(m_window, OnScroll);
    glfwSetCharCallback(m_window, OnCharacter);
    glfwSetWindowFocusCallback(m_window, OnFocus);

    m_running = true;
    UpdateSizes();
    return {};
}

Window::~Window()
{
    if (m_window) glfwDestroyWindow(m_window);
    if (g_window_count && !--g_window_count) glfwTerminate();
}

void Window::Tick()
{
    const auto previous_size = m_size;
    const auto previous_framebuffer_size = m_framebuffer_size;
    BeginInput();
    glfwPollEvents();
    m_running = !glfwWindowShouldClose(m_window);
    UpdateSizes();
    m_resized = m_size != previous_size || m_framebuffer_size != previous_framebuffer_size;
}

bool Window::IsResized() const
{
    return m_resized;
}

NativeWindow Window::GetNativeWindow(GLFWwindow* handle)
{
#ifdef _WIN32
    return {.window = glfwGetWin32Window(handle)};
#elifdef __APPLE__
    return {.window = glfwGetCocoaWindow(handle)};
#elifdef __linux__
    return {.window = reinterpret_cast<void*>(static_cast<uintptr_t>(glfwGetX11Window(handle))),
            .display = glfwGetX11Display()};
#else
    return {};
#endif
}

void Window::OpenInShell(std::string_view path)
{
#ifdef _WIN32
    const std::string narrow(path);
    const int length = MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, nullptr, 0);
    if (length <= 0) return;
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), -1, wide.data(), length);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
#elifdef __linux__
    const std::string narrow(path);
    char* const arguments[] = {const_cast<char*>("xdg-open"), const_cast<char*>(narrow.c_str()), nullptr};
    pid_t process = 0;
    posix_spawnp(&process, "xdg-open", nullptr, nullptr, arguments, environ);
#elifdef __APPLE__
    const CFURLRef url = CFURLCreateWithBytes(nullptr,
                                              reinterpret_cast<const UInt8*>(path.data()),
                                              static_cast<CFIndex>(path.size()),
                                              kCFStringEncodingUTF8,
                                              nullptr);
    if (!url) return;
    LSOpenCFURLRef(url, nullptr);
    CFRelease(url);
#endif
}

void Window::UpdateSizes()
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(m_window, &width, &height);
    m_size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    glfwGetFramebufferSize(m_window, &width, &height);
    m_framebuffer_size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    float scale = 1.f;
    glfwGetWindowContentScale(m_window, &scale, nullptr);
    m_content_scale = scale;
}

void Window::OnKey(GLFWwindow* handle, int key, int, int action, int)
{
    if (action == GLFW_REPEAT) return;
    auto* window = FromHandle(handle);
    const auto mapped = ToKey(key);
    if (!window || !mapped) return;
    switch (*mapped)
    {
        case Key::Shift:
            window->SetKey(Key::Shift,
                           IsSideDown(handle, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT));
            break;
        case Key::Control:
            window->SetKey(Key::Control,
                           IsSideDown(handle, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL));
            break;
        case Key::Alt:
            window->SetKey(Key::Alt, IsSideDown(handle, GLFW_KEY_LEFT_ALT, GLFW_KEY_RIGHT_ALT));
            break;
        default:
            window->SetKey(*mapped, action == GLFW_PRESS);
            break;
    }
}

void Window::OnMouseButton(GLFWwindow* handle, int button, int action, int)
{
    auto* window = FromHandle(handle);
    if (!window || button < 0 || button >= static_cast<int>(MouseButton::Count)) return;
    window->SetMouseButton(static_cast<MouseButton>(button), action == GLFW_PRESS);
}

void Window::OnCursorPosition(GLFWwindow* handle, double x, double y)
{
    auto* window = FromHandle(handle);
    if (!window) return;
    const glm::vec2 position{static_cast<float>(x), static_cast<float>(y)};
    if (window->m_has_mouse_position) window->m_mouse_delta += position - window->m_mouse_position;
    window->m_mouse_position = position;
    window->m_has_mouse_position = true;
}

void Window::OnScroll(GLFWwindow* handle, double, double y)
{
    if (auto* window = FromHandle(handle)) window->m_scroll_delta += static_cast<float>(y);
}

void Window::OnCharacter(GLFWwindow* handle, unsigned int character)
{
    if (auto* window = FromHandle(handle)) window->AddTextInput(character);
}

void Window::OnFocus(GLFWwindow* handle, int focused)
{
    if (auto* window = FromHandle(handle); window && !focused) window->ClearInput();
}

void Window::BeginInput()
{
    m_previous_keys = m_keys;
    m_previous_mouse_buttons = m_mouse_buttons;
    m_mouse_delta = {};
    m_scroll_delta = 0.f;
    m_text_input.clear();
}

void Window::ClearInput()
{
    m_keys.fill(false);
    m_mouse_buttons.fill(false);
}

void Window::AddTextInput(char32_t character)
{
    if (character < 0x20 || character == 0x7F) return;
    if (character >= 0xF700 && character <= 0xF8FF) return;
    m_text_input.push_back(character);
}
