#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace editor::ui {

enum class lua_script_preset { blank, fpp_controller };

struct lua_script_template {
    std::string_view name;
    std::string_view source;
};

inline constexpr std::array lua_script_templates{
    lua_script_template{"Arquivo em branco", R"lua(--@property {"name":"my_property","type":"number","default":0}
-- Compartilhe valores com Globals.Set("pontos", 0) e Globals.Get("pontos").
-- camera:SetUniform("intensity", "float", 1) configura filtros GLSL.
-- camera.postProcessPasses = {"shaders/ps1.frag"} adiciona passes após o filtro base.
-- SoundPlayer oferece Play/Pause/Resume/Stop e isPlaying (somente leitura).
function setup() end

function update() end
)lua"},
    lua_script_template{"Controle FPP (jogador e câmera)", R"lua(--@property {"name":"player","type":"entity","default":null}
--@property {"name":"camera","type":"entity","default":null}
--@property {"name":"move_speed","type":"number","default":5}
--@property {"name":"mouse_sensitivity","type":"number","default":0.12}

-- Globals.Set/Get compartilham valores entre scripts da cena.
-- A câmera aceita postProcessShader, postProcessPasses e SetUniform(nome, tipo, valor).
-- SoundPlayer:Play/Pause/Resume/Stop controlam áudio 3D no componente de som.
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
