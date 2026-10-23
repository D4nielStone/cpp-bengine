-- FPP player controller for the Bubble Engine demo project.
-- This script is intentionally lightweight and designed to be executed by the Lua code component.

local yaw = -90.0
local pitch = 0.0
local move_speed = 2.5

function setup()
    pitch, yaw = bubble.transform.get_rotation()
    bubble.input.set_mouse_captured(true)
    bubble.log("FPP player ready.")
end

function update()
    local delta_time = bubble.delta_time or 0.0

    if bubble.input.is_key_down("W") then
        bubble.transform.move_forward(move_speed * delta_time)
    end
    if bubble.input.is_key_down("S") then
        bubble.transform.move_forward(-move_speed * delta_time)
    end
    if bubble.input.is_key_down("A") then
        bubble.transform.move_right(-move_speed * delta_time)
    end
    if bubble.input.is_key_down("D") then
        bubble.transform.move_right(move_speed * delta_time)
    end

    local mouse_x, mouse_y = bubble.input.mouse_delta()
    yaw = yaw + mouse_x * 0.12
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * 0.12))
    bubble.transform.set_rotation(pitch, yaw, 0.0)
end
