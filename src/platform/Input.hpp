#pragma once

#include "game/InputState.hpp"

#include <SDL3/SDL.h>

namespace gridfin {

struct InputSettings {
    float mouseSensitivity = 0.0025f; ///< radians per mouse count
    float stickLookSpeed = 3.2f;      ///< radians per second at full deflection
    float stickDeadzone = 0.18f;
    bool invertY = false;
};

/// Turns SDL keyboard, mouse and gamepad events into an InputState.
///
/// Keyboard bindings use scancodes, so WASD stays in place on any layout.
class Input {
public:
    explicit Input(SDL_Window* window) : m_window(window) {}
    ~Input();

    void handleEvent(const SDL_Event& event);

    /// Builds the state for this frame and resets per-frame accumulators.
    [[nodiscard]] InputState frame(float dt);

    void setMouseCaptured(bool captured);
    [[nodiscard]] bool mouseCaptured() const noexcept { return m_mouseCaptured; }

    InputSettings settings;

private:
    void openGamepad(SDL_JoystickID id);

    SDL_Window* m_window;
    SDL_Gamepad* m_gamepad = nullptr;
    bool m_mouseCaptured = false;
    glm::vec2 m_mouseDelta{0.0f};
    float m_wheel = 0.0f;
    bool m_jumpPressed = false;
    bool m_togglePressed = false;
    bool m_duskPressed = false;
    int m_dpadZoom = 0;
    InputDevice m_device = InputDevice::KeyboardMouse;
};

} // namespace gridfin
