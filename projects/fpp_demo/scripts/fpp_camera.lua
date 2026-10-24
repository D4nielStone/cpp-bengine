-- Local camera pitch; yaw is inherited from the parent player body.
local pitch = 0.0

function setup()
    local initial_pitch = bubble.transform.get_rotation()
    pitch = initial_pitch or 0.0
    bubble.input.set_mouse_captured(true)
end

function update()
    local _, mouse_y = bubble.input.mouse_delta()
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * 0.12))
    bubble.transform.set_rotation(pitch, 0.0, 0.0)
end
