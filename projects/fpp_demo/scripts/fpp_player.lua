-- First-person body movement and yaw control for the FPP demo.
local yaw = -90.0
local move_speed = 2.5

function setup()
    local _, initial_yaw = bubble.transform.get_rotation()
    yaw = initial_yaw or yaw
    bubble.input.set_mouse_captured(true)
    bubble.log("FPP player body ready.")
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

    local mouse_x, _ = bubble.input.mouse_delta()
    yaw = yaw + mouse_x * 0.12
    bubble.transform.set_rotation(0.0, yaw, 0.0)
end
