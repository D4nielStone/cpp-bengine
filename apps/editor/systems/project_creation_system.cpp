#include "project_creation_system.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/physics.hpp"
#include "core/ecs.hpp"
#include "project_persistence_system.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace editor::ui {
namespace {

bool write_template_file(
    const std::filesystem::path& path,
    const std::string_view contents,
    std::string& error
) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "Could not create template file '" + path.string() + "'.";
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        error = "Could not write template file '" + path.string() + "'.";
        return false;
    }
    return true;
}

bool write_fpp_scripts(const std::filesystem::path& project_folder, std::string& error) {
    constexpr std::string_view player_script = R"lua(--@property {"name":"move_speed", "type":"number", "default":5}
local yaw = -90.0
local body
local rigidbody

function setup()
    body = gameObject:GetComponent("Transform")
    rigidbody = gameObject:GetComponent("Rigidbody")
    yaw = body.localEulerAngles.y
    Input.SetMouseCaptured(true)
end

function update()
    local mouse_x = Input.GetMouseDelta()
    yaw = yaw - mouse_x * 0.12
    body.localEulerAngles = Vector3(0.0, yaw, 0.0)

    local forward = Input.GetAxis("Vertical")
    local right = Input.GetAxis("Horizontal")
    local length = math.sqrt(forward * forward + right * right)
    if length > 1.0 then
        forward = forward / length
        right = right / length
    end
    forward = forward * properties.move_speed
    right = right * properties.move_speed
    local angle = math.rad(yaw)
    local velocity = rigidbody.velocity
    rigidbody.velocity = Vector3(
        math.cos(angle) * forward + math.sin(angle) * right,
        velocity.y,
        -math.sin(angle) * forward + math.cos(angle) * right
    )
end
)lua";
    constexpr std::string_view camera_script = R"lua(local pitch = 0.0

function setup()
    pitch = transform.localEulerAngles.x
    Input.SetMouseCaptured(true)
end

function update()
    local _, mouse_y = Input.GetMouseDelta()
    pitch = math.max(-89.0, math.min(89.0, pitch - mouse_y * 0.12))
    transform.localEulerAngles = Vector3(pitch, 0.0, 0.0)
end
)lua";
    return write_template_file(
               project_folder / "scripts" / "fpp_player.lua",
               player_script,
               error
           ) &&
        write_template_file(
            project_folder / "scripts" / "fpp_camera.lua",
            camera_script,
            error
        );
}

void add_fpp_scene(const std::shared_ptr<commons::ecs>& registry) {
    auto ground = registry->create();
    registry->entity_names[ground.id] = "Ground";
    registry->get<commons::transform>(ground.id)->set_position({0.f, -0.5f, 0.f});
    registry->add<commons::physics>(ground, true);
    registry->get<commons::physics>(ground.id)->configure_collision(
        commons::physics::shapes::box_shape,
        {40.f, 1.f, 40.f},
        {0.f, 0.f, 0.f},
        true,
        false
    );

    auto player = registry->create();
    registry->entity_names[player.id] = "Player";
    registry->get<commons::transform>(player.id)->set_position({0.f, 0.1f, 10.f});
    registry->get<commons::transform>(player.id)->set_rotation({0.f, 90.f, 0.f});
    registry->add<commons::physics>(
        player,
        false,
        commons::physics::shapes::forma_capsula
    );
    auto body = registry->get<commons::physics>(player.id);
    body->configure_collision(
        commons::physics::shapes::forma_capsula,
        {0.7f, 0.6f, 0.7f},
        {0.f, 0.9f, 0.f},
        true,
        false
    );
    body->set_body_type(commons::physics::body_type::character);
    body->setFriccao(0.f);
    registry->add<commons::code>(player, "scripts/fpp_player.lua");

    auto player_camera = registry->create();
    registry->entity_names[player_camera.id] = "Player Camera";
    registry->add<commons::camera>(player_camera);
    registry->get<commons::transform>(player_camera.id)->set_position(
        {0.f, 0.8f, 0.f}
    );
    registry->get<commons::transform>(player_camera.id)->set_parent(player.id, false);
    registry->add<commons::code>(player_camera, "scripts/fpp_camera.lua");
}

}

std::filesystem::path project_file_path(
    const std::filesystem::path& project_folder
) {
    return project_folder / "project.bengine";
}

bool create_project_files(
    const std::filesystem::path& project_folder,
    std::string& error,
    const project_template selected_template
) {
    error.clear();
    if (project_folder.empty() || project_folder.filename().empty() ||
        project_folder.filename() == "." || project_folder.filename() == "..") {
        error = "Choose a valid project folder name.";
        return false;
    }

    std::error_code filesystem_error;
    const auto absolute_folder = std::filesystem::absolute(
        project_folder,
        filesystem_error
    ).lexically_normal();
    if (filesystem_error) {
        error = "Could not resolve project location: " +
            filesystem_error.message();
        return false;
    }
    if (std::filesystem::exists(absolute_folder, filesystem_error)) {
        if (filesystem_error) {
            error = "Could not inspect project location: " +
                filesystem_error.message();
            return false;
        }
        error = "A file or folder already exists at '" +
            absolute_folder.string() + "'. Choose a different project name.";
        return false;
    }
    if (filesystem_error) {
        error = "Could not inspect project location: " +
            filesystem_error.message();
        return false;
    }

    std::filesystem::create_directories(
        absolute_folder / "assets" / "models",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }
    std::filesystem::create_directories(
        absolute_folder / "assets" / "textures",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }
    std::filesystem::create_directories(
        absolute_folder / "scripts",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }

    if (selected_template == project_template::fpp &&
        !write_fpp_scripts(absolute_folder, error))
        return false;

    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    if (selected_template == project_template::fpp)
        add_fpp_scene(registry);
    project_persistence_system persistence(registry, camera_entity.id);
    return persistence.save(project_file_path(absolute_folder).string(), error);
}

}
