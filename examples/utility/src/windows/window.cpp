#include "window.hpp"
#include "optional"

namespace
{
    constexpr auto WINDOW_CLASS_NAME = L"RimeraWindowClass";

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd, uMsg, wParam, lParam);
        }
    }

    RM::Result<void> RegisterWindowClass()
    {
        static bool registered = false;
        if (registered) return {};

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = WINDOW_CLASS_NAME;

        if (!RegisterClassExW(&wc)) return std::unexpected("Failed to register window class");
        registered = true;
        return {};
    }

    std::optional<Key> ToKey(WPARAM virtual_key)
    {
        if (virtual_key >= 'A' && virtual_key <= 'Z')
            return static_cast<Key>(static_cast<uint32_t>(Key::A) + (virtual_key - 'A'));
        if (virtual_key >= '0' && virtual_key <= '9')
            return static_cast<Key>(static_cast<uint32_t>(Key::Num0) + (virtual_key - '0'));
        switch (virtual_key)
        {
            case VK_SPACE:
                return Key::Space;
            case VK_RETURN:
                return Key::Enter;
            case VK_ESCAPE:
                return Key::Escape;
            case VK_TAB:
                return Key::Tab;
            case VK_SHIFT:
                return Key::Shift;
            case VK_CONTROL:
                return Key::Control;
            case VK_MENU:
                return Key::Alt;
            case VK_LEFT:
                return Key::Left;
            case VK_RIGHT:
                return Key::Right;
            case VK_UP:
                return Key::Up;
            case VK_DOWN:
                return Key::Down;
            default:
                return std::nullopt;
        }
    }

    std::optional<MouseButton> ToMouseButton(UINT message)
    {
        switch (message)
        {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                return MouseButton::Left;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                return MouseButton::Right;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                return MouseButton::Middle;
            default:
                return std::nullopt;
        }
    }

    glm::uvec2 GetWindowClientSize(HWND hwnd)
    {
        if (hwnd == nullptr || !IsWindow(hwnd)) return {0, 0};

        RECT client_rect{};
        if (GetClientRect(hwnd, &client_rect))
        {
            return {
                static_cast<std::uint32_t>(client_rect.right - client_rect.left),
                static_cast<std::uint32_t>(client_rect.bottom - client_rect.top)
            };
        }
        return {0, 0};
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
    if (auto result = RegisterWindowClass(); !result) return std::unexpected(result.error());

    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    RECT window_rect = {
        0,
        0,
        static_cast<LONG>(m_size.x),
        static_cast<LONG>(m_size.y)
    };
    AdjustWindowRect(&window_rect, style, FALSE);

    const int window_width = window_rect.right - window_rect.left;
    const int window_height = window_rect.bottom - window_rect.top;

    HWND hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        L"Rimera",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        window_width,
        window_height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
        );

    if (hwnd == nullptr) return std::unexpected("Failed to create window");

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    const RAWINPUTDEVICE mouse{
        .usUsagePage = 0x01,
        .usUsage = 0x02,
        .dwFlags = 0,
        .hwndTarget = hwnd,
    };
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse)))
        return std::unexpected("Failed to register raw mouse input");

    m_window = hwnd;
    m_running = true;
    m_size = GetWindowClientSize(hwnd);
    return {};
}

Window::~Window()
{
    if (m_window == nullptr) return;

    auto hwnd = static_cast<HWND>(m_window);
    if (IsWindow(hwnd))
    {
        DestroyWindow(hwnd);
    }
    m_window = nullptr;
}

bool Window::IsResized() const
{
    return m_resized;
}

void Window::Tick()
{
    const auto previous_size = m_size;
    auto hwnd = static_cast<HWND>(m_window);

    BeginInput();

    if (hwnd != nullptr && IsWindow(hwnd))
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            switch (msg.message)
            {
                case WM_QUIT:
                    m_running = false;
                    break;
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP:
                    if (auto key = ToKey(msg.wParam))
                        SetKey(*key, msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN);
                    break;
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN:
                    SetMouseButton(*ToMouseButton(msg.message), true);
                    SetCapture(hwnd);
                    break;
                case WM_LBUTTONUP:
                case WM_RBUTTONUP:
                case WM_MBUTTONUP:
                    SetMouseButton(*ToMouseButton(msg.message), false);
                    if (!IsMouseButtonDown(MouseButton::Left) &&
                        !IsMouseButtonDown(MouseButton::Right) &&
                        !IsMouseButtonDown(MouseButton::Middle))
                        ReleaseCapture();
                    break;
                case WM_MOUSEMOVE:
                    m_mouse_position = {static_cast<short>(LOWORD(msg.lParam)),
                                        static_cast<short>(HIWORD(msg.lParam))};
                    break;
                case WM_MOUSEWHEEL:
                    m_scroll_delta += static_cast<float>(GET_WHEEL_DELTA_WPARAM(msg.wParam)) /
                                      static_cast<float>(WHEEL_DELTA);
                    break;
                case WM_INPUT:
                {
                    RAWINPUT raw{};
                    UINT size = sizeof(raw);
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam),
                                        RID_INPUT,
                                        &raw,
                                        &size,
                                        sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
                        raw.header.dwType == RIM_TYPEMOUSE &&
                        !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))
                    {
                        m_mouse_delta += glm::vec2(static_cast<float>(raw.data.mouse.lLastX),
                                                   static_cast<float>(raw.data.mouse.lLastY));
                    }
                    break;
                }
                default:
                    break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!IsWindow(hwnd))
        {
            m_running = false;
        }
        else if (GetForegroundWindow() != hwnd)
        {
            ClearInput();
        }
    }
    else
    {
        m_running = false;
    }

    m_size = GetWindowClientSize(hwnd);
    m_resized = (m_size.x != previous_size.x || m_size.y != previous_size.y);
}

void* Window::GetNativeHandle() const
{
    return m_window;
}