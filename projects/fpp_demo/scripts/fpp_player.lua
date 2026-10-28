-- Body movement follows the engine camera's yaw convention (+X at zero yaw).
local yaw = -90.0
local move_speed = 2.5
local body

function setup()
    body = gameObject:GetComponent("Transform")
    yaw = body.localEulerAngles.y
    Input.SetMouseCaptured(true)
    Debug.Log("FPP player body ready.")
end

function update()
    local distance = move_speed * Time.deltaTime
    local forward = Input.GetAxis("Vertical") * distance
    local right = Input.GetAxis("Horizontal") * distance
    local angle = math.rad(yaw)
    local position = body.localPosition
    body.localPosition = Vector3(
        position.x + math.cos(angle) * forward - math.sin(angle) * right,
        position.y,
        position.z + math.sin(angle) * forward + math.cos(angle) * right
    )

    local mouse_x = Input.GetMouseDelta()
    yaw = yaw + mouse_x * 0.12
    body.localEulerAngles = Vector3(0.0, yaw, 0.0)
end
