#include "window.hpp"

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

    void RegisterWindowClass()
    {
        static bool registered = false;
        if (registered) return;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = WINDOW_CLASS_NAME;

        RegisterClassExW(&wc);
        registered = true;
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

Window::Window()
{
    RegisterWindowClass();

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

    if (hwnd == nullptr) return;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    m_window = hwnd;
    m_running = true;
    m_size = GetWindowClientSize(hwnd);
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

    if (hwnd != nullptr && IsWindow(hwnd))
    {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                m_running = false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!IsWindow(hwnd))
        {
            m_running = false;
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