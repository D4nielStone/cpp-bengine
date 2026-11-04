#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace editor::ui {

enum class lua_script_preset { empty, wasd, fpp_player, fpp_camera, fpp_controller };

struct lua_script_template {
    std::string_view name;
    std::string_view source;
};

inline constexpr std::array lua_script_templates{
    lua_script_template{"Em branco", R"lua(-- Editable per entity in the Inspector; read with properties.speed.
--@property {"name":"speed","type":"number","default":5}

-- Called once when the scene starts.
function setup()
end

-- Called every frame.
function update()
end
)lua"},
    lua_script_template{"Movimentação WASD", R"lua(--@property {"name":"move_speed","type":"number","default":5}

function update()
    local direction = Vector3(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"))
    if direction:length() > 1 then direction = direction:normalized() end
    local movement = direction * properties.move_speed

    local body = gameObject:GetComponent("Rigidbody")
    if body then
        movement.y = body.velocity.y
        body.velocity = movement
    else
        local pose = gameObject.transform
        if pose then pose.position = pose.position + movement * Time.deltaTime end
    end
end
)lua"},
    lua_script_template{"Jogador FPP (WASD e guinada)", R"lua(--@property {"name":"move_speed","type":"number","default":5}
--@property {"name":"mouse_sensitivity","type":"number","default":0.12}

local yaw = -90.0

function setup()
    local pose = gameObject.transform
    if not pose then return end
    yaw = pose.localEulerAngles.y
    Input.SetMouseCaptured(true)
    Debug.Log("FPP player body ready.")
end

function update()
    local pose = gameObject.transform
    local body = gameObject:GetComponent("Rigidbody")
    if not pose or not body then return end

    local mouse_x = Input.GetMouseDelta()
    yaw = yaw - mouse_x * properties.mouse_sensitivity
    pose.localEulerAngles = Vector3(0, yaw, 0)

    local direction = Vector3(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"))
    if direction:length() > 1 then direction = direction:normalized() end
    local right = direction.x * properties.move_speed
    local forward = direction.z * properties.move_speed
    local angle = math.rad(yaw)
    local sine, cosine = math.sin(angle), math.cos(angle)
    body.velocity = Vector3(
        cosine * forward + sine * right,
        body.velocity.y,
        -sine * forward + cosine * right
    )
end
)lua"},
    lua_script_template{"Câmera FPP (filha do jogador)", R"lua(--@property {"name":"mouse_sensitivity","type":"number","default":0.12}

local pitch = 0.0

function setup()
    local pose = gameObject.transform
    if not pose then return end
    pitch = pose.localEulerAngles.x
    Input.SetMouseCaptured(true)
end

function update()
    local pose = gameObject.transform
    if not pose then return end
    local _, mouse_y = Input.GetMouseDelta()
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * properties.mouse_sensitivity))
    pose.localEulerAngles = Vector3(pitch, 0, 0)
end
)lua"},
    lua_script_template{"Controle FPP (jogador e câmera)", R"lua(--@property {"name":"player","type":"entity","default":null}
--@property {"name":"camera","type":"entity","default":null}
--@property {"name":"move_speed","type":"number","default":5}
--@property {"name":"mouse_sensitivity","type":"number","default":0.12}

local enabled = false
local camera_offset

local function targets()
    if not properties.player or not properties.camera then return end
    local player = GameObject.Find(properties.player)
    local camera = GameObject.Find(properties.camera)
    if not player or not camera then return end
    return player, camera
end

function setup()
    local player, camera = targets()
    if not player or player.id == camera.id or not player.transform or not camera.transform
        or not camera:GetComponent("Camera") or player:IsDescendantOf(camera) then
        Debug.Log("Controle FPP: selecione um jogador e uma câmera válida no Inspector.")
        return
    end
    if not camera:IsDescendantOf(player) then
        camera_offset = camera.transform.position - player.transform.position
    end
    enabled = true
    Input.SetMouseCaptured(true)
end

function update()
    if not enabled then return end
    local player, camera = targets()
    if not player then return end
    local player_pose, camera_pose = player.transform, camera.transform
    local view = camera:GetComponent("Camera")
    if not player_pose or not camera_pose or not view then return end

    local mouse_x, mouse_y = Input.GetMouseDelta()
    local angles = camera_pose.localEulerAngles
    local sensitivity = properties.mouse_sensitivity
    angles.y = (angles.y + mouse_x * sensitivity) % 360
    angles.x = math.max(-89, math.min(89, angles.x - mouse_y * sensitivity))
    camera_pose.localEulerAngles = angles

    local forward = view.forward
    forward.y = 0
    forward = forward:normalized()
    local right = forward:cross(Vector3(0, 1, 0))
    local direction = forward * Input.GetAxis("Vertical") + right * Input.GetAxis("Horizontal")
    if direction:length() > 1 then direction = direction:normalized() end
    local movement = direction * properties.move_speed
    local body = player:GetComponent("Rigidbody")
    if body then
        movement.y = body.velocity.y
        body.velocity = movement
    else
        player_pose.position = player_pose.position + movement * Time.deltaTime
    end
    if camera_offset then camera_pose.position = player_pose.position + camera_offset end
end
)lua"}
};

inline constexpr const lua_script_template& script_template(lua_script_preset preset) {
    return lua_script_templates[static_cast<std::size_t>(preset)];
}

}
