--@property {"name":"cam","type":"entity","default":null}
--@property {"name":"player","type":"entity","default":null}
--@property {"name":"range","type":"number","default":3}
--@property {"name":"hold_x","type":"number","default":0.6}
--@property {"name":"hold_y","type":"number","default":0.65}
--@property {"name":"hold_z","type":"number","default":0.3}

local collected = false
local was_pressed = false

function update()
    if collected then return end

    local pressed = Input.GetKey("E")
    local just_pressed = pressed and not was_pressed
    was_pressed = pressed
    if not just_pressed or not properties.cam or not properties.player then return end

    local player = GameObject.Find(properties.player)
    local cam = GameObject.Find(properties.cam)
    local camera = cam and cam:GetComponent("Camera")
    if not player or not camera then return end

    local hit = camera:traceRay(properties.range, player)
    if not hit or hit.entity.id ~= gameObject.id then return end

    local pose = gameObject.transform
    if not pose or not gameObject:SetParent(player, false) then return end
    local body = gameObject:GetComponent("Rigidbody")
    if body then Destroy(body) end
    pose.localPosition = Vector3(properties.hold_x, properties.hold_y, properties.hold_z)
    pose.localEulerAngles = Vector3()
    collected = true
end
