#include "camera.hpp"
#include "window.hpp"
#include "algorithm"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace
{
    const float kMaxPitch = glm::radians(89.f);
    constexpr float kMinDistance = 0.01f;
    const glm::vec3 kWorldUp{0.f, 1.f, 0.f};
}

Camera::Camera(CameraMode mode) : m_mode(mode)
{
}

void Camera::Update(const Window& window, float delta_time)
{
    const auto size = window.GetSize();
    if (size.x && size.y) m_aspect = static_cast<float>(size.x) / static_cast<float>(size.y);

    switch (m_mode)
    {
        case CameraMode::Orbit:
            UpdateOrbit(window);
            break;
        case CameraMode::Fly:
            UpdateFly(window, delta_time);
            break;
    }
}

void Camera::SetMode(CameraMode mode)
{
    if (mode == m_mode) return;
    if (mode == CameraMode::Orbit) m_target = m_position + GetForward() * m_distance;
    m_mode = mode;
}

void Camera::LookAt(const glm::vec3& position, const glm::vec3& target)
{
    const glm::vec3 offset = target - position;
    m_distance = std::max(glm::length(offset), kMinDistance);
    const glm::vec3 forward = offset / m_distance;
    m_position = position;
    m_target = position + forward * m_distance;
    m_yaw = std::atan2(forward.x, -forward.z);
    m_pitch = std::clamp(std::asin(std::clamp(forward.y, -1.f, 1.f)), -kMaxPitch, kMaxPitch);
}

void Camera::SetPerspective(float fov_y, float near_plane, float far_plane)
{
    m_fov_y = fov_y;
    m_near_plane = near_plane;
    m_far_plane = far_plane;
}

glm::vec3 Camera::GetForward() const
{
    return {std::cos(m_pitch) * std::sin(m_yaw),
            std::sin(m_pitch),
            -std::cos(m_pitch) * std::cos(m_yaw)};
}

glm::vec3 Camera::GetRight() const
{
    return glm::normalize(glm::cross(GetForward(), kWorldUp));
}

glm::vec3 Camera::GetUp() const
{
    return glm::cross(GetRight(), GetForward());
}

glm::mat4 Camera::GetView() const
{
    return glm::lookAtRH(m_position, m_position + GetForward(), kWorldUp);
}

glm::mat4 Camera::GetProjection() const
{
    return glm::perspectiveRH_ZO(m_fov_y, m_aspect, m_near_plane, m_far_plane);
}

void Camera::UpdateOrbit(const Window& window)
{
    const glm::vec2 mouse_delta = window.GetMouseDelta();
    if (window.IsMouseButtonDown(MouseButton::Left)) Rotate(mouse_delta);

    if (window.IsMouseButtonDown(MouseButton::Right) ||
        window.IsMouseButtonDown(MouseButton::Middle))
    {
        const float pan_scale = m_distance * m_look_sensitivity * 0.5f;
        m_target += (-GetRight() * mouse_delta.x + GetUp() * mouse_delta.y) * pan_scale;
    }

    if (const float scroll = window.GetScrollDelta(); scroll != 0.f)
        m_distance = std::max(m_distance * std::pow(0.9f, scroll), kMinDistance);

    m_position = m_target - GetForward() * m_distance;
}

void Camera::UpdateFly(const Window& window, float delta_time)
{
    if (window.IsMouseButtonDown(MouseButton::Right)) Rotate(window.GetMouseDelta());

    if (const float scroll = window.GetScrollDelta(); scroll != 0.f)
        m_move_speed = std::max(m_move_speed * std::pow(1.1f, scroll), 0.01f);

    glm::vec3 direction{0.f};
    if (window.IsKeyDown(Key::W)) direction += GetForward();
    if (window.IsKeyDown(Key::S)) direction -= GetForward();
    if (window.IsKeyDown(Key::D)) direction += GetRight();
    if (window.IsKeyDown(Key::A)) direction -= GetRight();
    if (window.IsKeyDown(Key::E) || window.IsKeyDown(Key::Space)) direction += kWorldUp;
    if (window.IsKeyDown(Key::Q) || window.IsKeyDown(Key::Control)) direction -= kWorldUp;
    if (glm::dot(direction, direction) == 0.f) return;

    const float speed = m_move_speed * (window.IsKeyDown(Key::Shift) ? 4.f : 1.f);
    m_position += glm::normalize(direction) * speed * delta_time;
}

void Camera::Rotate(glm::vec2 mouse_delta)
{
    m_yaw += mouse_delta.x * m_look_sensitivity;
    m_pitch = std::clamp(m_pitch - mouse_delta.y * m_look_sensitivity, -kMaxPitch, kMaxPitch);
}
