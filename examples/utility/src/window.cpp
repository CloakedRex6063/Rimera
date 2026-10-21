#include "window.hpp"

void Window::BeginInput()
{
    m_previous_keys = m_keys;
    m_previous_mouse_buttons = m_mouse_buttons;
    m_mouse_delta = {};
    m_scroll_delta = 0.f;
}

void Window::ClearInput()
{
    m_keys.fill(false);
    m_mouse_buttons.fill(false);
}
