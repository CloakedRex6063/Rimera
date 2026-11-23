#pragma once
#include "glm/glm.hpp"
#include "window.hpp"

enum class CameraMode
{
    Orbit,
    Fly,
};

struct CameraKeys
{
    Key forward = Key::W;
    Key back = Key::S;
    Key left = Key::A;
    Key right = Key::D;
    Key up = Key::E;
    Key down = Key::Q;
    Key sprint = Key::Shift;
    Key toggle_mode = Key::Tab;
};

class Camera
{
public:
    explicit Camera(CameraMode mode = CameraMode::Fly);

    void Update(const Window& window, float delta_time);

    void SetMode(CameraMode mode);
    void LookAt(const glm::vec3& position, const glm::vec3& target);
    void SetPerspective(float fov_y, float near_plane, float far_plane);
    void SetMoveSpeed(float speed) { m_move_speed = speed; }
    void SetLookSensitivity(float sensitivity) { m_look_sensitivity = sensitivity; }
    void SetKeys(const CameraKeys& keys) { m_keys = keys; }

    [[nodiscard]] CameraMode GetMode() const { return m_mode; }
    [[nodiscard]] const CameraKeys& GetKeys() const { return m_keys; }
    [[nodiscard]] glm::vec3 GetPosition() const { return m_position; }
    [[nodiscard]] glm::vec3 GetTarget() const { return m_target; }
    [[nodiscard]] float GetFovY() const { return m_fov_y; }
    [[nodiscard]] float GetAspect() const { return m_aspect; }
    [[nodiscard]] glm::vec3 GetForward() const;
    [[nodiscard]] glm::vec3 GetRight() const;
    [[nodiscard]] glm::vec3 GetUp() const;
    [[nodiscard]] glm::mat4 GetView() const;
    [[nodiscard]] glm::mat4 GetProjection() const;
    [[nodiscard]] glm::mat4 GetViewProjection() const { return GetProjection() * GetView(); }
    void SetAspect(float aspect) { m_aspect = aspect; }
    void SetReversedZ(bool reversed_z) { m_reversed_z = reversed_z; }

private:
    void UpdateOrbit(const Window& window);
    void UpdateFly(const Window& window, float delta_time);
    void Rotate(glm::vec2 mouse_delta);

    CameraMode m_mode;
    CameraKeys m_keys;
    glm::vec3 m_position{0.f, 0.f, 5.f};
    glm::vec3 m_target{0.f};
    float m_distance = 5.f;
    float m_yaw = 0.f;
    float m_pitch = 0.f;
    float m_fov_y = glm::radians(45.f);
    float m_near_plane = 0.01f;
    float m_far_plane = 1000.f;
    float m_aspect = 16.f / 9.f;
    bool m_reversed_z = false;
    float m_move_speed = 2.f;
    float m_look_sensitivity = 0.0025f;
};
