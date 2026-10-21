#include "window.hpp"

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
