-- FPP player controller for the Bubble Engine demo project.
-- This script is intentionally lightweight and designed to be executed by the Lua code component.

local yaw = 0.0
local pitch = 0.0
local move_speed = 2.5

function setup()
    bubble.log("FPP player ready.")
end

function update()
    if bubble.input == nil then
        return
    end

    if bubble.input.is_key_down("W") then
        bubble.transform.move_forward(move_speed)
    end
    if bubble.input.is_key_down("S") then
        bubble.transform.move_forward(-move_speed)
    end
    if bubble.input.is_key_down("A") then
        bubble.transform.move_right(-move_speed)
    end
    if bubble.input.is_key_down("D") then
        bubble.transform.move_right(move_speed)
    end

    if bubble.input.has_mouse_delta() then
        local dx, dy = bubble.input.mouse_delta()
        yaw = yaw + dx * 0.006
        pitch = pitch + dy * 0.004
        bubble.transform.set_rotation({ 0.0, yaw * 57.2958, 0.0 })
    end
end
