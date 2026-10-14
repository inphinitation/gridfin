#include "platform/Input.hpp"

#include "core/Log.hpp"

#include <cmath>

namespace gridfin {

namespace {

[[nodiscard]] float axis(SDL_Gamepad* pad, SDL_GamepadAxis which)
{
    return static_cast<float>(SDL_GetGamepadAxis(pad, which)) / 32767.0f;
}

/// Radial deadzone with the remaining range rescaled to 0..1.
[[nodiscard]] glm::vec2 applyDeadzone(glm::vec2 stick, float deadzone)
{
    const float length = glm::length(stick);
    if (length <= deadzone) return glm::vec2(0.0f);
    const float scaled = std::min((length - deadzone) / (1.0f - deadzone), 1.0f);
    return stick / length * scaled;
}

} // namespace

Input::~Input()
{
    if (m_gamepad) SDL_CloseGamepad(m_gamepad);
}

void Input::openGamepad(SDL_JoystickID id)
{
    if (m_gamepad) return;
    m_gamepad = SDL_OpenGamepad(id);
    if (m_gamepad) log::info("Gamepad connected: {}", SDL_GetGamepadName(m_gamepad));
}

void Input::setMouseCaptured(bool captured)
{
    m_mouseCaptured = captured;
    SDL_SetWindowRelativeMouseMode(m_window, captured);
}

void Input::handleEvent(const SDL_Event& event)
{
    switch (event.type) {
    case SDL_EVENT_GAMEPAD_ADDED: openGamepad(event.gdevice.which); break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (m_gamepad && SDL_GetGamepadID(m_gamepad) == event.gdevice.which) {
            SDL_CloseGamepad(m_gamepad);
            m_gamepad = nullptr;
            log::info("Gamepad disconnected");
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        if (m_mouseCaptured) {
            m_mouseDelta += glm::vec2(event.motion.xrel, event.motion.yrel);
            m_device = InputDevice::KeyboardMouse;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!m_mouseCaptured) setMouseCaptured(true);
        m_device = InputDevice::KeyboardMouse;
        break;
    case SDL_EVENT_MOUSE_WHEEL: m_wheel += event.wheel.y; break;
    case SDL_EVENT_KEY_DOWN:
        m_device = InputDevice::KeyboardMouse;
        if (event.key.repeat) break;
        if (event.key.scancode == SDL_SCANCODE_SPACE) m_jumpPressed = true;
        if (event.key.scancode == SDL_SCANCODE_V) m_togglePressed = true;
        if (event.key.scancode == SDL_SCANCODE_ESCAPE && m_mouseCaptured) setMouseCaptured(false);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        m_device = InputDevice::Gamepad;
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: m_jumpPressed = true; break;
        case SDL_GAMEPAD_BUTTON_NORTH:
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: m_togglePressed = true; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: ++m_dpadZoom; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: --m_dpadZoom; break;
        default: break;
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (m_mouseCaptured) setMouseCaptured(false);
        break;
    default: break;
    }
}

InputState Input::frame(float dt)
{
    InputState state;
    const bool* keys = SDL_GetKeyboardState(nullptr);

    glm::vec2 move{0.0f};
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) move.y += 1.0f;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) move.y -= 1.0f;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move.x += 1.0f;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]) move.x -= 1.0f;
    if (glm::length(move) > 1.0f) move = glm::normalize(move);

    const float invert = settings.invertY ? -1.0f : 1.0f;
    glm::vec2 look = glm::vec2(m_mouseDelta.x, -m_mouseDelta.y * invert) * settings.mouseSensitivity;
    bool sprint = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
    bool jumpHeld = keys[SDL_SCANCODE_SPACE];
    float zoom = m_wheel;

    if (m_gamepad) {
        const glm::vec2 leftStick =
            applyDeadzone({axis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTX), -axis(m_gamepad, SDL_GAMEPAD_AXIS_LEFTY)},
                          settings.stickDeadzone);
        const glm::vec2 rightStick =
            applyDeadzone({axis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTX), -axis(m_gamepad, SDL_GAMEPAD_AXIS_RIGHTY)},
                          settings.stickDeadzone);
        if (glm::length(leftStick) > 0.0f || glm::length(rightStick) > 0.0f) m_device = InputDevice::Gamepad;

        move += leftStick;
        if (glm::length(move) > 1.0f) move = glm::normalize(move);
        // Square the response for finer aiming near the centre.
        const glm::vec2 curved = rightStick * glm::length(rightStick);
        look += glm::vec2(curved.x, curved.y * invert) * settings.stickLookSpeed * dt;

        sprint = sprint || SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)
                 || SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_WEST)
                 || axis(m_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;
        jumpHeld = jumpHeld || SDL_GetGamepadButton(m_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        zoom += static_cast<float>(m_dpadZoom);
    }

    state.move = move;
    state.look = look;
    state.zoom = zoom;
    state.sprint = sprint;
    state.jumpHeld = jumpHeld;
    state.jumpPressed = m_jumpPressed;
    state.toggleViewPressed = m_togglePressed;
    state.device = m_device;

    m_mouseDelta = glm::vec2(0.0f);
    m_wheel = 0.0f;
    m_dpadZoom = 0;
    m_jumpPressed = false;
    m_togglePressed = false;
    return state;
}

} // namespace gridfin
