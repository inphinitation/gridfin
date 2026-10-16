#pragma once

#include "core/Math.hpp"

namespace gridfin {

enum class InputDevice { KeyboardMouse, Gamepad };

/// Device independent snapshot of the player's intent for one rendered frame.
struct InputState {
    glm::vec2 move{0.0f}; ///< x = strafe right, y = forward; length <= 1
    glm::vec2 look{0.0f}; ///< yaw/pitch change in radians for this frame
    float zoom = 0.0f;    ///< camera zoom steps this frame, positive zooms in
    bool jumpPressed = false;
    bool jumpHeld = false;
    bool sprint = false;
    bool toggleViewPressed = false;
    bool toggleDuskPressed = false;
    InputDevice device = InputDevice::KeyboardMouse;
};

} // namespace gridfin
