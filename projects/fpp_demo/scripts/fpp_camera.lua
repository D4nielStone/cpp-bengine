-- Local camera pitch; yaw is inherited from the parent player body.
local pitch = 0.0

function setup()
    pitch = transform.localEulerAngles.x
    Input.SetMouseCaptured(true)
end

function update()
    local _, mouse_y = Input.GetMouseDelta()
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * 0.12))
    transform.localEulerAngles = Vector3(pitch, 0.0, 0.0)
end
