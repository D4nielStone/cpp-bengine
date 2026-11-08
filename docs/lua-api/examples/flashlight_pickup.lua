--@property {"name":"cam","type":"entity","default":null}
--@property {"name":"player","type":"entity","default":null}
--@property {"name":"range","type":"number","default":3}
--@property {"name":"hold_x","type":"number","default":0.6}
--@property {"name":"hold_y","type":"number","default":0.65}
--@property {"name":"hold_z","type":"number","default":0.3}
--@property {"name":"prompt_height","type":"number","default":0.35}

local collected = false
local was_pressed = false
local prompt

function setup()
    local hud = gameObject:AddHUD()
    if not hud then return end
    prompt = hud:Add("text", "E", -1)
    prompt.width = 32
    prompt.height = 24
    prompt.visible = false
end

local function update_prompt(camera, camera_pose, pose)
    if not prompt then return end
    prompt.visible = false
    if pose.position:distance(camera_pose.position) > properties.range then return end

    local anchor = pose.position + Vector3(0, properties.prompt_height, 0)
    local pixel = camera:worldToScreen(anchor)
    if not pixel or pixel.z < 0 or pixel.z > 1 or
        pixel.x < 0 or pixel.x >= camera.viewportWidth or
        pixel.y < 0 or pixel.y >= camera.viewportHeight then return end

    prompt.visible = prompt:SetScreenPosition(pixel)
end

function update()
    if collected then return end

    local pressed = Input.GetKey("E")
    local just_pressed = pressed and not was_pressed
    was_pressed = pressed
    local player = properties.player and GameObject.Find(properties.player)
    local cam = properties.cam and GameObject.Find(properties.cam)
    local camera = cam and cam:GetComponent("Camera")
    local camera_pose = cam and cam.transform
    local pose = gameObject.transform
    if not player or not camera or not camera_pose or not pose then
        if prompt then prompt.visible = false end
        return
    end

    update_prompt(camera, camera_pose, pose)
    if not just_pressed then return end

    local hit = camera:traceRay(properties.range, player)
    if not hit or hit.entity.id ~= gameObject.id then return end

    if not gameObject:SetParent(player, false) then return end
    local body = gameObject:GetComponent("Rigidbody")
    if body then Destroy(body) end
    pose.localPosition = Vector3(properties.hold_x, properties.hold_y, properties.hold_z)
    pose.localEulerAngles = Vector3()
    collected = true
    if prompt then prompt.visible = false end
end
