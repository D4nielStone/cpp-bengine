-- The camera looks along local +X; parent yaw rotates around world +Y.
local yaw = -90.0
local move_speed = 5
local body
local rigidbody

function setup()
    body = gameObject:GetComponent("Transform")
    rigidbody = gameObject:GetComponent("Rigidbody")
    yaw = body.localEulerAngles.y
    Input.SetMouseCaptured(true)
    Debug.Log("FPP player body ready.")
end

function update()
    local distance = move_speed
    local forward = Input.GetAxis("Vertical") * distance
    local right = Input.GetAxis("Horizontal") * distance
    local angle = math.rad(yaw)
    local velocity = rigidbody.velocity
    -- Preserve gravity and contact response on the vertical axis.
    rigidbody.velocity = Vector3(
        math.cos(angle) * forward + math.sin(angle) * right,
        velocity.y,
        -math.sin(angle) * forward + math.cos(angle) * right
    )

    local mouse_x = Input.GetMouseDelta()
    yaw = yaw - mouse_x * 0.12

    body.localEulerAngles = Vector3(0.0, yaw, 0.0)
end
