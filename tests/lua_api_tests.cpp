#include <gtest/gtest.h>
#include "debugging/script_log.hpp"
#include "api/lua.hpp"
#include "components/code.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/physics.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/sound_listener.hpp"
#include "components/sound_player.hpp"
#include "components/camera.hpp"
#include "core/ecs.hpp"
#include "systems/code_system.hpp"
#include "systems/physics_system.hpp"
#include "project_creation_system.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <cmath>

TEST(LuaApi, UnityStyleTransformAndLegacyApiCoexist) {
    commons::script_log::instance().clear();
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::transform>(entity);
    const auto path = std::filesystem::temp_directory_path() /
        ("bengine-lua-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua");
    { std::ofstream script(path); script << R"(
function setup()
    local own = gameObject:GetComponent("Transform")
    assert(own ~= nil)
    assert(gameObject:GetComponent("Missing") == nil)
    assert(GameObject.Find(gameObject.id).id == gameObject.id)
    assert(GameObject.Find(4294967295) == nil)
    own.position = Vector3(1, 2, 3)
    transform:Translate(Vector3(2, 0, -1))
    transform:Rotate(Vector3(0, 30, 0))
    assert(transform.position.x == 3 and transform.position.z == 2)
    assert(math.abs(transform.eulerAngles.y - 30) < 0.0001)
    assert(Time.deltaTime == 0)
    assert(Input.GetKey("W") == false)
    assert(Input.GetAxis("Horizontal") == 0)
    local x, y, z = bubble.transform.get_position()
    assert(x == 3 and y == 2 and z == 2)
    transform.localEulerAngles = Vector3(0, 90, 0)
    transform:Translate(Vector3(0, 0, 1))
    assert(math.abs(transform.position.x - 4) < 0.0001)
    Debug.Log("compatible")
    setup_complete = true
end
)"; }
    registry->add<commons::code>(entity, path.string());
    auto script = registry->get<commons::code>(entity.id);
    commons::code_system system;
    system.setup(registry);
    ASSERT_TRUE(script->estado_lua["setup_complete"].get_or(false));
    ASSERT_FALSE(commons::script_log::instance().lines().empty());
    EXPECT_NE(commons::script_log::instance().lines().back().find("compatible"), std::string::npos);
    system.update(registry);
    auto time = script->estado_lua["Time"].get<sol::table>();
    EXPECT_FLOAT_EQ(time["deltaTime"].get<float>(), script->estado_lua["bubble"]["delta_time"].get<float>());
    EXPECT_GE(time["time"].get<float>(), time["deltaTime"].get<float>());
    EXPECT_LE(time["deltaTime"].get<float>(), 0.1f);
    std::filesystem::remove(path);
}

TEST(LuaApi, ExposesSoundListenerAndPlayerProperties) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::sound_listener>(entity);
    registry->add<commons::sound_player>(entity);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);

    ASSERT_TRUE(lua.safe_script(R"(
        local listener = gameObject:GetComponent("SoundListener")
        local player = gameObject:GetComponent("SoundPlayer")
        assert(listener.enabled == true)
        assert(player.clip == "")
        assert(player.volume == 1 and player.pitch == 1 and player.radius == 10)
        assert(player.looping == false and player.playOnAwake == false)
        listener.enabled = false
        player.clip = "audio/ambience.ogg"
        player.volume = 0.6
        player.pitch = 1.25
        player.radius = 18
        player.looping = true
        player.playOnAwake = true
    )").valid());
    const auto listener = registry->get<commons::sound_listener>(entity.id);
    const auto player = registry->get<commons::sound_player>(entity.id);
    EXPECT_FALSE(listener->enabled);
    EXPECT_EQ(player->clip, "audio/ambience.ogg");
    EXPECT_FLOAT_EQ(player->volume, 0.6f);
    EXPECT_FLOAT_EQ(player->pitch, 1.25f);
    EXPECT_FLOAT_EQ(player->radius, 18.f);
    EXPECT_TRUE(player->looping);
    EXPECT_TRUE(player->play_on_awake);
}

TEST(LuaApi, GameObjectLookupTracksRegistryLifetime) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::transform>(entity);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUnityApi(lua, registry, entity.id);
    registry->remove(entity.id);
    EXPECT_NO_THROW(lua.script("assert(gameObject.transform == nil); assert(GameObject.Find(gameObject.id) == nil)"));
    registry.reset();
    EXPECT_NO_THROW(lua.script("assert(gameObject:GetComponent('Transform') == nil)"));
}

TEST(LuaApi, TransformSeparatesWorldAndLocalPosition) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto parent = registry->create();
    auto child = registry->create();
    registry->add<commons::transform>(parent);
    registry->add<commons::transform>(child);
    registry->get<commons::transform>(parent.id)->set_position({10.f, 0.f, 0.f});
    auto transform = registry->get<commons::transform>(child.id);
    ASSERT_TRUE(transform->set_parent(parent.id, false));
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, child.id);
    EXPECT_NO_THROW(lua.script(R"(
        transform.localPosition = Vector3(2, 0, 0)
        assert(transform.position.x == 12)
        transform.position = Vector3(15, 0, 0)
        assert(transform.localPosition.x == 5)
    )"));
}

TEST(LuaApi, CameraProjectionRoundTripsWithCurrentPoseAndViewport) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    const auto camera = registry->get<commons::camera>(entity.id);
    camera->viewport({1000, 400});
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);

    EXPECT_NO_THROW(lua.script(R"(
        cam = gameObject:GetComponent("Camera")
        transform.position = Vector3(5, 2, 1)
        local center = cam:worldToScreen(Vector3(9, 2, 1))
        assert(math.abs(center.x - 500) < 0.001 and math.abs(center.y - 200) < 0.001)
        for _, orthographic in ipairs({false, true}) do
            cam.orthographic = orthographic
            local target = Vector3(9, 3, 2)
            local pixel = cam:worldToScreen(target)
            assert(pixel.x > 500 and pixel.y < 200 and pixel.z > 0 and pixel.z < 1)
            assert(cam:screenToWorld(pixel):distance(target) < 0.001)
        end
        cam.orthographic = false
        assert(cam:worldToScreen(Vector3(4, 2, 1)) == nil)
        cam.nearClipPlane = 0
        assert(cam:screenToWorld(Vector3()) == nil)
        cam.nearClipPlane = 0.1
    )"));
    registry->remove<commons::camera>(entity.id);
    EXPECT_NO_THROW(lua.script("assert(cam:worldToScreen(Vector3()) == nil)"));
}

TEST(LuaApi, CameraRayFiltersPlayerAndRemovedColliders) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math);
    auto registry = std::make_shared<commons::ecs>();
    auto view = registry->create();
    auto player = registry->create();
    auto target = registry->create();
    registry->add<commons::camera>(view);
    registry->entity_names[player.id] = "Player";
    registry->entity_names[target.id] = "Target";
    registry->get<commons::transform>(player.id)->set_position({1, 0, 0});
    registry->get<commons::transform>(target.id)->set_position({4, 0, 0});
    registry->add<commons::physics>(player, true);
    registry->add<commons::physics>(target, true);
    auto physics = std::make_shared<commons::physics_system>();
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, view.id, physics);

    EXPECT_NO_THROW(lua.script(R"(
        cam = gameObject:GetComponent("Camera")
        player = GameObject.Find("Player")
        local target = GameObject.Find("Target")
        assert(cam:traceRay().entity.id == player.id)
        assert(cam:traceRay(3, player) == nil)
        local hit = cam:traceRay(5, player)
        assert(hit.entity.id == target.id)
        assert(math.abs(hit.distance - 3.5) < 0.001)
        assert(hit.point:distance(Vector3(3.5, 0, 0)) < 0.001)
        assert(hit.normal:distance(Vector3(-1, 0, 0)) < 0.001)
        assert(cam:traceRay(5, player, 0, 0) == nil)
        assert(cam:traceRay(5, player, 0) == nil)
        assert(cam:traceRay(0/0, player) == nil)
        cam.orthographic = true
        local pixel = cam:worldToScreen(target.transform.position)
        hit = cam:traceRay(5, player, pixel.x, pixel.y)
        assert(hit.entity.id == target.id)
        assert(math.abs(hit.distance - 3.4) < 0.001)
        Destroy(player:GetComponent("Rigidbody"))
        assert(cam:traceRay().entity.id == target.id)
    )"));
    physics.reset();
    EXPECT_NO_THROW(lua.script("assert(cam:traceRay() == nil)"));
}

TEST(LuaApi, FlashlightScriptCollectsAndFollowsPlayer) {
    auto registry = std::make_shared<commons::ecs>();
    auto player = registry->create();
    auto camera = registry->create();
    auto flashlight = registry->create();
    auto blocker = registry->create();
    const auto player_pose = registry->get<commons::transform>(player.id);
    const auto flashlight_pose = registry->get<commons::transform>(flashlight.id);
    ASSERT_TRUE(registry->get<commons::transform>(camera.id)->set_parent(player.id, false));
    flashlight_pose->set_position({2, 0, 0});
    registry->get<commons::transform>(blocker.id)->set_position({1, 0, 0});
    registry->add<commons::camera>(camera);
    registry->add<commons::physics>(flashlight, true);
    registry->add<commons::physics>(blocker, true);
    registry->add<commons::renderer>(flashlight);
    const auto example = std::filesystem::path(BENGINE_SOURCE_DIR) /
        "docs/lua-api/examples/flashlight_pickup.lua";
    registry->add<commons::code>(flashlight, example.string());
    const auto script = registry->get<commons::code>(flashlight.id);
    std::string error;
    ASSERT_TRUE(script->load_properties(error)) << error;
    ASSERT_TRUE(script->set_property("cam", camera.id));
    ASSERT_TRUE(script->set_property("player", player.id));
    ASSERT_TRUE(script->set_property("hold_x", 0.75));

    const auto physics = std::make_shared<commons::physics_system>();
    commons::code_system scripts(physics);
    scripts.setup(registry);
    physics->setup(registry);
    bool pressed = true;
    script->estado_lua["Input"].get<sol::table>().set_function("GetKey",
        [&](const std::string& key) { return key == "E" && pressed; });
    scripts.update(registry);
    EXPECT_EQ(flashlight_pose->parent_id, 0u);
    registry->remove(blocker.id);
    scripts.update(registry);
    EXPECT_EQ(flashlight_pose->parent_id, 0u);
    pressed = false;
    scripts.update(registry);
    pressed = true;
    scripts.update(registry);
    ASSERT_EQ(flashlight_pose->parent_id, player.id);
    EXPECT_FALSE(registry->has<commons::physics>(flashlight.id));
    EXPECT_TRUE(registry->get<commons::renderer>(flashlight.id)->visible);
    EXPECT_EQ(flashlight_pose->get_position(), (bgui::vec3{0.75f, 0.65f, 0.3f}));
    player_pose->set_position({10, 0, 0});
    EXPECT_EQ(flashlight_pose->get_world_position(), (bgui::vec3{10.75f, 0.65f, 0.3f}));
    EXPECT_NO_THROW(script->estado_lua.script(R"(
        local player = GameObject.Find(properties.player)
        assert(not player:SetParent(gameObject))
        assert(not gameObject:SetParent(gameObject))
        assert(not gameObject:SetParent("Player"))
        assert(gameObject:IsDescendantOf(player))
        assert(gameObject:SetParent(nil))
        assert(math.abs(transform.position.x - 10.75) < 0.001)
    )"));
}

TEST(LuaApi, BuiltinFppTemplateScriptsMoveLookAndCollectUsingTheNewApi) {
    const auto project_directory = std::filesystem::temp_directory_path() /
        ("bengine-lua-template-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup {
        std::filesystem::path path;
        ~cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup_project{project_directory};
    std::string error;
    ASSERT_TRUE(editor::ui::create_project_files(project_directory, error,
        editor::ui::project_template::fpp)) << error;
    std::filesystem::copy_file(
        std::filesystem::path(BENGINE_SOURCE_DIR) / "tests/fixtures/lua/pickup.lua",
        project_directory / "scripts/pickup.lua");

    auto registry = std::make_shared<commons::ecs>();
    auto player = registry->create(106);
    registry->entity_names[player.id] = "PlayerCapsule";
    auto camera = registry->create(100);
    registry->add<commons::camera>(camera);
    const auto camera_component = registry->get<commons::camera>(camera.id);
    auto pickup = registry->create(103);
    registry->get<commons::transform>(player.id)->set_rotation({0.f, -90.f, 0.f});
    registry->get<commons::transform>(camera.id)->set_parent(player.id, false);
    auto light_entity = registry->create();
    ASSERT_TRUE(registry->get<commons::transform>(light_entity.id)->set_parent(
        camera.id, false));
    registry->add<commons::spot_light>(light_entity);
    registry->get<commons::spot_light>(light_entity.id)->position = {0.f, 0.f, 0.f};
    registry->add<commons::renderer>(pickup);
    registry->add<commons::physics>(pickup);
    auto load = [&](commons::entity& entity, const std::string& filename) {
        registry->add<commons::code>(entity,
            (project_directory / "scripts" / filename).string());
        auto script = registry->get<commons::code>(entity.id);
        script->setup();
        return script;
    };
    registry->add<commons::physics>(player);
    auto physical = registry->get<commons::physics>(player.id);
    physical->set_body_type(commons::physics::body_type::character);
    physical->setVelocidade({0.f, -3.f, 0.f});
    auto body = load(player, "fpp_player.lua");
    auto setup = sol::protected_function(body->f_setup)();
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();
    auto input = body->estado_lua["Input"].get<sol::table>();
    input.set_function("GetAxis", [](const std::string& axis) { return axis == "Vertical" ? 1.f : 0.f; });
    input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{10.f, 0.f}; });
    body->estado_lua["Time"]["deltaTime"] = 0.1f;
    auto moved = sol::protected_function(body->f_update)();
    ASSERT_TRUE(moved.valid()) << moved.get<sol::error>().what();
    auto player_transform = registry->get<commons::transform>(player.id);
    EXPECT_NEAR(physical->getVelocidade().z, -std::sin(glm::radians(-91.2f)) * 5.f, 0.0001f);
    EXPECT_NEAR(physical->getVelocidade().x, std::cos(glm::radians(-91.2f)) * 5.f, 0.0001f);
    EXPECT_FLOAT_EQ(physical->getVelocidade().y, -3.f);
    EXPECT_NEAR(player_transform->get_rotation().y, -91.2f, 0.0001f);

    input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{0.f, 0.f}; });
    input.set_function("GetAxis", [](const std::string&) { return 1.f; });
    for (const auto delta : {0.001f, 0.1f}) {
        body->estado_lua["Time"]["deltaTime"] = delta;
        ASSERT_NO_THROW(body->update());
        const auto velocity = physical->getVelocidade();
        EXPECT_NEAR(std::hypot(velocity.x, velocity.z), 5.f, 0.0001f);
        EXPECT_FLOAT_EQ(velocity.y, -3.f);
    }

    auto look = load(camera, "fpp_camera.lua");
    setup = sol::protected_function(look->f_setup)();
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();
    auto camera_input = look->estado_lua["Input"].get<sol::table>();
    camera_input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{0.f, 10.f}; });
    auto looked = sol::protected_function(look->f_update)();
    ASSERT_TRUE(looked.valid()) << looked.get<sol::error>().what();
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(camera.id)->get_rotation().x, -1.2f);
    camera_component->getViewMatrix();
    const auto horizontal_length = std::hypot(
        camera_component->forward.x, camera_component->forward.z);
    input.set_function("GetAxis", [](const std::string& axis) {
        return axis == "Vertical" ? 1.f : 0.f;
    });
    ASSERT_NO_THROW(body->update());
    const auto velocity = physical->getVelocidade();
    EXPECT_NEAR(velocity.x, camera_component->forward.x / horizontal_length * 5.f, 0.0001f);
    EXPECT_NEAR(velocity.z, camera_component->forward.z / horizontal_length * 5.f, 0.0001f);
    const auto spotlight_direction =
        registry->get<commons::spot_light>(light_entity.id)->get_world_direction();
    EXPECT_NEAR(spotlight_direction.x, camera_component->forward.x, 0.0001f);
    EXPECT_NEAR(spotlight_direction.y, camera_component->forward.y, 0.0001f);
    EXPECT_NEAR(spotlight_direction.z, camera_component->forward.z, 0.0001f);
    EXPECT_EQ(registry->get<commons::spot_light>(light_entity.id)->get_world_position(),
        registry->get<commons::transform>(camera.id)->get_world_position());
    camera_input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{0.f, 10000.f}; });
    looked = sol::protected_function(look->f_update)();
    ASSERT_TRUE(looked.valid()) << looked.get<sol::error>().what();
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(camera.id)->get_rotation().x, -89.f);

    registry->get<commons::transform>(pickup.id)->set_position(player_transform->get_position());
    auto game = load(pickup, "pickup.lua");
    setup = sol::protected_function(game->f_setup)();
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();
    auto collected = sol::protected_function(game->f_update)();
    ASSERT_TRUE(collected.valid()) << collected.get<sol::error>().what();
    EXPECT_FALSE(registry->get<commons::renderer>(pickup.id)->visible);
    EXPECT_FALSE(registry->has<commons::physics>(pickup.id));
    auto repeated = sol::protected_function(game->f_update)();
    EXPECT_TRUE(repeated.valid());
}

TEST(LuaApi, Vector3OperatorsAndHelpersWorkOnValuesAndTheType) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math);
    auto registry = std::make_shared<commons::ecs>();
    const auto entity = registry->create();
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    const auto result = lua.safe_script(R"lua(
        local a = Vector3(1, 2, 3)
        local b = Vector3(4, 6, 3)
        assert((a + b).y == 8 and (b - a).x == 3)
        assert((-a).z == -3 and (a * 2).y == 4 and (2 * a).z == 6)
        assert((b / 2).x == 2 and not pcall(function() return a / 0 end))
        assert(Vector3.distance(a, b) == 5 and a:distance(b) == 5)
        assert(Vector3.lerp(a, b, 0.5).y == 4 and a:lerp(b, 2).x == 7)
        assert(a:dot(b) == 25 and a:cross(b).z == -2)
        assert(Vector3():normalized():length() == 0)
        assert(math.abs((b - a):normalized():length() - 1) < 0.00001)
        assert(a.x == 1 and a.y == 2 and a.z == 3)
        transform.localPosition = a + b
    )lua", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(registry->get<commons::transform>(entity.id)->get_position(), (bgui::vec3{5, 8, 6}));
}

TEST(LuaApi, TranslateUsesTheEngineRotationOrder) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    auto transform = registry->get<commons::transform>(entity.id);
    transform->set_rotation({30.f, 40.f, 50.f});
    const auto direction = transform->get_world_model_matrix() * glm::vec4{0.f, 0.f, 1.f, 0.f};
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    auto result = lua.safe_script("transform:Translate(Vector3(0, 0, 1))", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const auto position = transform->get_position();
    EXPECT_NEAR(position.x, direction.x, 0.0001f);
    EXPECT_NEAR(position.y, direction.y, 0.0001f);
    EXPECT_NEAR(position.z, direction.z, 0.0001f);
}

TEST(LuaApi, ScriptOutputAppearsInDebugHistory) {
    auto& log = commons::script_log::instance();
    log.clear();
    commons::code script("");
    script.estado_lua.script("print('hello', 42, true, nil)");
    ASSERT_EQ(log.lines().size(), 1u);
    EXPECT_NE(log.lines().front().find("hello\t42\ttrue\tnil"), std::string::npos);
}

TEST(LuaApi, DebugHistoryIsBoundedAndCanBeCleared) {
    auto& log = commons::script_log::instance();
    log.clear();
    for (std::size_t i = 0; i <= log.capacity; ++i) log.write(7, std::to_string(i));
    ASSERT_EQ(log.lines().size(), log.capacity);
    EXPECT_EQ(log.lines().front(), "[lua][7] 1");
    const auto revision = log.revision();
    log.clear();
    EXPECT_TRUE(log.lines().empty());
    EXPECT_GT(log.revision(), revision);
}

TEST(LuaApi, InitializationErrorsAppearInDebugHistory) {
    auto& log = commons::script_log::instance();
    log.clear();
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::code>(entity, "missing-debug-test-script.lua");
    commons::code_system system;
    system.setup(registry);
    ASSERT_EQ(log.lines().size(), 1u);
    EXPECT_NE(log.lines().front().find("Initialization failed:"), std::string::npos);
    log.clear();
}

TEST(LuaApi, RuntimeLogTransportPreservesMultilineAndPartialRecords) {
    const auto path = std::filesystem::temp_directory_path() /
        ("bengine-debug-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct cleanup {
        std::filesystem::path path;
        ~cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
    } cleanup{path};
    auto& log = commons::script_log::instance();
    log.clear();
    const std::string message = "[lua][7] first\nsecond";
    const auto size = static_cast<std::uint32_t>(message.size());
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&size), sizeof(size));
        output.write(message.data(), 4);
    }
    std::uintmax_t offset = 0;
    commons::read_script_log(path, offset);
    EXPECT_EQ(offset, 0u);
    EXPECT_TRUE(log.lines().empty());
    {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        output.write(message.data() + 4, message.size() - 4);
    }
    commons::read_script_log(path, offset);
    ASSERT_EQ(log.lines().size(), 1u);
    EXPECT_EQ(log.lines().front(), message);
    commons::read_script_log(path, offset);
    EXPECT_EQ(log.lines().size(), 1u);
    log.clear();
}

TEST(LuaApi, ComponentPropertiesAreWritable) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::physics>(entity);
    registry->add<commons::directional_light>(entity);
    registry->add<commons::point_light>(entity);
    registry->add<commons::spot_light>(entity);
    registry->add<commons::camera>(entity);
    registry->add<commons::renderer>(entity);
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    auto result = lua.safe_script(R"(
        local body = gameObject:GetComponent("Rigidbody")
        body.mass = 2
        body.friction = 0.7
        body.restitution = 0.3
        gameObject:GetComponent("Renderer").enabled = false
        local cam = gameObject:GetComponent("Camera")
        cam.fieldOfView = 60
        cam.nearClipPlane = 0.2
        cam.farClipPlane = 500
        cam.orthographic = true
        cam.orthographicSize = 12
        cam.backgroundColor = Color(0.1, 0.2, 0.3, 1)
        cam.fogEnabled = true
        cam.fogColor = Color(0.4, 0.5, 0.6, 1)
        cam.fogDensity = 0.025
        body.velocity = Vector3(1, 2, 3)
        local light = gameObject:GetComponent("DirectionalLight")
        light.intensity = 4
        light.color = Vector3(0.1, 0.2, 0.3)
        gameObject:GetComponent("PointLight").intensity = 8
        local spot = gameObject:GetComponent("SpotLight")
        spot.position = Vector3(1, 2, 3)
        spot.direction = Vector3(0, -1, 0)
        spot.color = Vector3(0.5, 0.6, 0.7)
        spot.intensity = 9
        spot.innerCutoff = 10
        spot.outerCutoff = 20
    )", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    auto body = registry->get<commons::physics>(entity.id);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getInvMass(), .5f);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getFriction(), .7f);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getRestitution(), .3f);
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->visible);
    const auto cam = registry->get<commons::camera>(entity.id);
    EXPECT_FLOAT_EQ(cam->config.fov, 60.f);
    EXPECT_FLOAT_EQ(cam->config.corte_curto, .2f);
    EXPECT_FLOAT_EQ(cam->config.corte_longo, 500.f);
    EXPECT_TRUE(cam->config.flag_orth);
    EXPECT_FLOAT_EQ(cam->config.scale, 12.f);
    EXPECT_FLOAT_EQ(cam->config.sky_solid_color.g, .2f);
    EXPECT_TRUE(cam->config.fog_enabled);
    EXPECT_FLOAT_EQ(cam->config.fog_color.b, .6f);
    EXPECT_FLOAT_EQ(cam->config.fog_density, .025f);
    EXPECT_FLOAT_EQ(body->getVelocidade().y, 2.f);
    EXPECT_FLOAT_EQ(registry->get<commons::directional_light>(entity.id)->intensity, 4.f);
    EXPECT_FLOAT_EQ(registry->get<commons::point_light>(entity.id)->intensity, 8.f);
    const auto spot = registry->get<commons::spot_light>(entity.id);
    EXPECT_FLOAT_EQ(spot->position.y, 2.f);
    EXPECT_FLOAT_EQ(spot->color.z, 0.7f);
    EXPECT_FLOAT_EQ(spot->intensity, 9.f);
    EXPECT_FLOAT_EQ(spot->inner_cutoff, 10.f);
    EXPECT_FLOAT_EQ(spot->outer_cutoff, 20.f);
}

TEST(LuaApi, SpotLightDirectionUsesTransformHierarchy) {
    auto registry = std::make_shared<commons::ecs>();
    auto parent = registry->create();
    auto entity = registry->create();
    registry->get<commons::transform>(parent.id)->set_rotation({0.f, 20.f, 0.f});
    registry->get<commons::transform>(entity.id)->set_rotation({0.f, 70.f, 0.f});
    ASSERT_TRUE(registry->get<commons::transform>(entity.id)->set_parent(parent.id, false));
    registry->add<commons::spot_light>(entity);
    const auto light = registry->get<commons::spot_light>(entity.id);

    const auto direction = light->get_world_direction();
    EXPECT_NEAR(direction.x, 0.f, 0.0001f);
    EXPECT_NEAR(direction.y, 0.f, 0.0001f);
    EXPECT_NEAR(direction.z, -1.f, 0.0001f);
    EXPECT_EQ(light->direction, (bgui::vec3{1.f, 0.f, 0.f}));
}

TEST(LuaApi, SpotLightPositionUsesTransformHierarchy) {
    auto registry = std::make_shared<commons::ecs>();
    auto parent = registry->create();
    auto entity = registry->create();
    auto parent_transform = registry->get<commons::transform>(parent.id);
    auto entity_transform = registry->get<commons::transform>(entity.id);
    parent_transform->set_position({3.f, 4.f, 5.f});
    parent_transform->set_rotation({0.f, 90.f, 0.f});
    entity_transform->set_position({0.f, 0.f, 2.f});
    ASSERT_TRUE(entity_transform->set_parent(parent.id, false));
    registry->add<commons::spot_light>(entity);
    const auto light = registry->get<commons::spot_light>(entity.id);
    light->position = {1.f, 0.f, 0.f};

    const auto position = light->get_world_position();
    EXPECT_NEAR(position.x, 5.f, 0.0001f);
    EXPECT_NEAR(position.y, 4.f, 0.0001f);
    EXPECT_NEAR(position.z, 4.f, 0.0001f);
}

TEST(LuaApi, SpotLightChildOfCameraFollowsCameraPitchAndParentYaw) {
    auto registry = std::make_shared<commons::ecs>();
    auto player = registry->create();
    auto camera_entity = registry->create();
    auto light_entity = registry->create();
    registry->get<commons::transform>(player.id)->set_rotation({0.f, 30.f, 0.f});
    registry->add<commons::camera>(camera_entity);
    auto camera_transform = registry->get<commons::transform>(camera_entity.id);
    camera_transform->set_rotation({25.f, 0.f, 0.f});
    ASSERT_TRUE(camera_transform->set_parent(player.id, false));
    ASSERT_TRUE(registry->get<commons::transform>(light_entity.id)->set_parent(
        camera_entity.id, false));
    registry->add<commons::spot_light>(light_entity);

    const auto direction =
        registry->get<commons::spot_light>(light_entity.id)->get_world_direction();
    const float pitch = glm::radians(25.f);
    const float yaw = glm::radians(30.f);
    EXPECT_NEAR(direction.x, std::cos(yaw) * std::cos(pitch), 0.0001f);
    EXPECT_NEAR(direction.y, std::sin(pitch), 0.0001f);
    EXPECT_NEAR(direction.z, -std::sin(yaw) * std::cos(pitch), 0.0001f);
}

TEST(LuaApi, SpotLightExposesWorldDirectionAndCutoffMethods) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->get<commons::transform>(entity.id)->set_rotation({0.f, 90.f, 0.f});
    registry->add<commons::spot_light>(entity);
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);

    const auto result = lua.safe_script(R"(
        local spot = gameObject:GetComponent("SpotLight")
        local direction = spot:GetWorldDirection()
        local position = spot.worldPosition
        assert(direction.x > -0.0001 and direction.x < 0.0001)
        assert(direction.z > -1.0001 and direction.z < -0.9999)
        assert(spot.worldDirection.z > -1.0001 and spot.worldDirection.z < -0.9999)
        assert(position.x > -2.0001 and position.x < -1.9999)
        assert(position.y > spot.position.y - 0.0001 and position.y < spot.position.y + 0.0001)
        assert(position.z > -0.0001 and position.z < 0.0001)
        spot:SetInnerCutoff(8)
        spot:SetOuterCutoff(22)
    )", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const auto light = registry->get<commons::spot_light>(entity.id);
    EXPECT_FLOAT_EQ(light->inner_cutoff, 8.f);
    EXPECT_FLOAT_EQ(light->outer_cutoff, 22.f);
}

TEST(LuaApi, DirectionalLightRotatesConfiguredDirectionInWorldSpace) {
    auto registry = std::make_shared<commons::ecs>();
    auto parent = registry->create();
    auto entity = registry->create();
    auto tr = registry->get<commons::transform>(entity.id);
    registry->get<commons::transform>(parent.id)->set_rotation({0, 90, 0});
    ASSERT_TRUE(tr->set_parent(parent.id, false));
    tr->set_position({10, 20, 30});
    tr->set_scale({2, 3, 4});
    commons::directional_light light({0, 0, -1}, {0, 0, 0}, {1, 1, 1}, 1);
    light.reg = registry;
    light.my_object = entity.id;
    const auto direction = light.get_world_direction();
    EXPECT_NEAR(direction.x, -1.f, .0001f);
    EXPECT_NEAR(direction.y, 0.f, .0001f);
    EXPECT_NEAR(direction.z, 0.f, .0001f);
    EXPECT_FLOAT_EQ(light.direction.z, -1.f);
}

TEST(LuaApi, SyntaxAndSetupErrorsAppearAtGameStart) {
    const auto path = std::filesystem::temp_directory_path() / "bengine-startup-error.lua";
    for (const auto& source : {"function setup( broken", "function setup() error('startup sentinel') end"}) {
        { std::ofstream file(path); file << source; }
        auto& log = commons::script_log::instance();
        log.clear();
        auto registry = std::make_shared<commons::ecs>();
        auto entity = registry->create();
        registry->add<commons::code>(entity, path.string());
        commons::code_system system;
        system.setup(registry);
        ASSERT_FALSE(log.lines().empty());
        EXPECT_NE(log.lines().back().find("Initialization failed:"), std::string::npos);
        EXPECT_NE(log.lines().back().find(path.filename().string()), std::string::npos);
    }
    std::filesystem::remove(path);
    commons::script_log::instance().clear();
}

TEST(LuaApi, UpdateErrorsAreLoggedOnceAndDisableTheFailedScript) {
    const auto path = std::filesystem::temp_directory_path() / "bengine-update-error.lua";
    { std::ofstream file(path); file << "function update() error('update sentinel') end"; }
    auto& log = commons::script_log::instance();
    log.clear();
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::code>(entity, path.string());
    commons::code_system system;
    system.setup(registry);
    system.update(registry);
    ASSERT_EQ(log.lines().size(), 1u);
    EXPECT_NE(log.lines().back().find("Update failed:"), std::string::npos);
    EXPECT_NE(log.lines().back().find("update sentinel"), std::string::npos);
    system.update(registry);
    EXPECT_EQ(log.lines().size(), 1u);
    std::filesystem::remove(path);
    log.clear();
}

TEST(LuaApi, DirectionalLightUsesRotationOnlyAndKeepsLocalMagnitude) {
    auto registry = std::make_shared<commons::ecs>();
    auto parent = registry->create();
    auto child = registry->create();
    const auto parent_transform = registry->get<commons::transform>(parent.id);
    const auto child_transform = registry->get<commons::transform>(child.id);
    parent_transform->set_rotation({20, 30, 40});
    parent_transform->set_scale({2, 3, 4});
    child_transform->set_rotation({-30, 25, 15});
    child_transform->set_scale({0, 0, 0});
    ASSERT_TRUE(child_transform->set_parent(parent.id, false));
    commons::directional_light light({1, -2, 3}, {}, {}, 1);
    light.reg = registry;
    light.my_object = child.id;
    auto rotation = [](bgui::vec3 r) {
        return glm::angleAxis(glm::radians(r.x), glm::vec3{1, 0, 0}) *
            glm::angleAxis(glm::radians(r.y), glm::vec3{0, 1, 0}) *
            glm::angleAxis(glm::radians(r.z), glm::vec3{0, 0, 1});
    };
    const auto expected = rotation(parent_transform->get_rotation()) *
        rotation(child_transform->get_rotation()) * glm::vec3{1, -2, 3};
    const auto actual = light.get_world_direction();
    EXPECT_NEAR(actual.x, expected.x, .0001f);
    EXPECT_NEAR(actual.y, expected.y, .0001f);
    EXPECT_NEAR(actual.z, expected.z, .0001f);
    light.reg.reset();
    EXPECT_EQ(light.get_world_direction(), light.direction);
}


TEST(LuaApi, FindAcceptsObjectNamesAndKeepsNumericLookup) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(42);
    registry->entity_names[entity.id] = "Player";
    registry->entity_names[9999] = "Player"; // Stale metadata is not a live match.
    auto numeric_name = registry->create(50);
    registry->entity_names[numeric_name.id] = "42";
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    auto result = lua.safe_script(R"(
        assert(GameObject.Find('Player').id == 42)
        assert(GameObject.Find(42).id == 42)
        assert(GameObject.Find('42').id == 50)
        assert(GameObject.Find('Missing') == nil)
        assert(GameObject.Find('player') == nil)
        assert(GameObject.Find('') == nil)
        GameObject.Find('Player').transform.localPosition = Vector3(1, 2, 3)
    )", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(42)->get_position().y, 2.f);
    auto duplicate = registry->create(43);
    registry->entity_names[duplicate.id] = "Player";
    EXPECT_NO_THROW(lua.script("assert(GameObject.Find('Player') == nil)"));
    registry->remove(duplicate.id);
    EXPECT_NO_THROW(lua.script("assert(GameObject.Find('Player').id == 42)"));
    registry->remove(entity.id);
    registry->create(42);
    EXPECT_NO_THROW(lua.script("assert(GameObject.Find('Player') == nil)"));
    registry->remove(42);
    registry->remove(50);
    lua["transform"] = sol::nil;
    registry.reset();
    EXPECT_NO_THROW(lua.script("assert(GameObject.Find('42') == nil)"));
}

TEST(LuaApi, ShadowControlsUpdateNativeComponentsAndSerialize) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::renderer>(entity);
    registry->add<commons::directional_light>(entity, bgui::vec3{0,-1,0}, bgui::vec3{0.1f}, bgui::vec3{1}, 1.f);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    ASSERT_NO_THROW(lua.script(R"(
        local r = gameObject:GetComponent('Renderer')
        assert(r.castShadows and r.receiveShadows)
        r.castShadows = false
        r.receiveShadows = false
        local l = gameObject:GetComponent('DirectionalLight')
        l.shadows = false
        l.shadowDistance = 35
        l.shadowBias = 0.002
        l.shadowStrength = 0.7
    )"));
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->cast_shadows);
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->receive_shadows);
    auto light = registry->get<commons::directional_light>(entity.id);
    rapidjson::Document data(rapidjson::kObjectType);
    ASSERT_TRUE(light->serialize(data, data.GetAllocator()));
    commons::directional_light loaded;
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_FALSE(loaded.shadows);
    EXPECT_FLOAT_EQ(loaded.shadow_distance, 35.f);
    EXPECT_FLOAT_EQ(loaded.shadow_bias, 0.002f);
    EXPECT_FLOAT_EQ(loaded.shadow_strength, 0.7f);
    data.RemoveMember("shadows");
    data.RemoveMember("shadow_distance");
    data.RemoveMember("shadow_bias");
    data.RemoveMember("shadow_strength");
    commons::directional_light legacy;
    ASSERT_TRUE(legacy.analyze(data));
    EXPECT_TRUE(legacy.shadows);
    EXPECT_FLOAT_EQ(legacy.shadow_distance, 50.f);
    data.AddMember("shadow_bias", "invalid", data.GetAllocator());
    EXPECT_FALSE(legacy.analyze(data));
}

TEST(LuaApi, SpotShadowControlsUpdateNativeComponentsAndSerialize) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::renderer>(entity);
    registry->add<commons::spot_light>(entity);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    ASSERT_NO_THROW(lua.script(R"(
        local r = gameObject:GetComponent('Renderer')
        assert(r.castShadows and r.receiveShadows)
        r.castShadows = false
        r.receiveShadows = false
        local l = gameObject:GetComponent('SpotLight')
        l.shadows = false
        l.shadowDistance = 35
        l.shadowBias = 0.002
        l.shadowStrength = 0.7
    )"));
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->cast_shadows);
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->receive_shadows);
    auto light = registry->get<commons::spot_light>(entity.id);
    rapidjson::Document data(rapidjson::kObjectType);
    ASSERT_TRUE(light->serialize(data, data.GetAllocator()));
    commons::spot_light loaded;
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_FALSE(loaded.shadows);
    EXPECT_FLOAT_EQ(loaded.shadow_distance, 35.f);
    EXPECT_FLOAT_EQ(loaded.shadow_bias, 0.002f);
    EXPECT_FLOAT_EQ(loaded.shadow_strength, 0.7f);
    data.RemoveMember("shadows");
    data.RemoveMember("shadow_distance");
    data.RemoveMember("shadow_bias");
    data.RemoveMember("shadow_strength");
    commons::spot_light legacy;
    ASSERT_TRUE(legacy.analyze(data));
    EXPECT_TRUE(legacy.shadows);
    EXPECT_FLOAT_EQ(legacy.shadow_distance, 50.f);
    data.AddMember("shadow_bias", "invalid", data.GetAllocator());
    EXPECT_FALSE(legacy.analyze(data));
}

TEST(LuaApi, SetupAndUpdateFailuresLeaveOtherScriptsRunning) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("bengine-lua-isolation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto failed = folder / "failed.lua";
    const auto healthy = folder / "healthy.lua";
    std::ofstream(healthy) << "updates = 0\nfunction setup() ready = true end\nfunction update() updates = updates + 1 end\n";
    for (const auto* source : {"function setup() error('setup sentinel') end",
                              "function update() error('update sentinel') end"}) {
        std::ofstream(failed) << source;
        commons::script_log::instance().clear();
        auto registry = std::make_shared<commons::ecs>();
        auto bad = registry->create();
        auto good = registry->create();
        registry->add<commons::code>(bad, failed.string());
        registry->add<commons::code>(good, healthy.string());
        commons::code_system system;
        EXPECT_NO_THROW(system.setup(registry));
        EXPECT_NO_THROW(system.update(registry));
        EXPECT_NO_THROW(system.update(registry));
        auto script = registry->get<commons::code>(good.id);
        EXPECT_TRUE(script->estado_lua["ready"].get<bool>());
        EXPECT_EQ(script->estado_lua["updates"].get<int>(), 2);
        ASSERT_EQ(commons::script_log::instance().lines().size(), 1u);
        EXPECT_NE(commons::script_log::instance().lines().back().find("sentinel"), std::string::npos);
        registry->entities.clear();
    }
    commons::script_log::instance().clear();
    std::filesystem::remove_all(folder);
}

#include "components/hud.hpp"
TEST(Hud, LuaCreationPersistenceAndLifetime) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    sol::state lua; lua.open_libraries(sol::lib::base);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUpApi(lua, registry, entity.id);
    ASSERT_NO_THROW(lua.script(R"(
        h = gameObject:AddHUD()
        assert(h == gameObject:GetComponent("HUD"))
        row = h:Add("row", "", -1)
        label = h:Add("text", "Vida: 100", 0)
        button = h:Add("button", "Usar", 0)
        world = h:Add("text3d", "Objetivo", -1)
        world.position = Vector3(1, 2, 3)
        world.rotation = Vector3(10, 20, 30)
        world.world_scale = Vector3(2, 3, 4)
        world.depth = 0.125
        assert(h.count == 4); assert(h:Get(1).text == "Vida: 100"); assert(h:Get(-1) == nil)
        assert(not button:ConsumeClick())
    )"));
    auto h=registry->get<commons::hud>(entity.id);
    ASSERT_EQ(h->elements.size(),4u);
    h->elements[2]->clicked=true;
    EXPECT_NO_THROW(lua.script("assert(button:ConsumeClick()); assert(not button:ConsumeClick())"));
    rapidjson::Document doc;
    ASSERT_TRUE(h->serialize(doc,doc.GetAllocator()));
    commons::hud restored;
    ASSERT_TRUE(restored.analyze(doc));
    ASSERT_EQ(restored.elements.size(),4u);
    EXPECT_EQ(restored.elements[1]->parent,0);
    EXPECT_EQ(restored.elements[3]->position.z,3.f);
    EXPECT_EQ(restored.elements[3]->rotation.y, 20.f);
    EXPECT_EQ(restored.elements[3]->world_scale.z, 4.f);
    EXPECT_FLOAT_EQ(restored.elements[3]->depth, .125f);
    auto& old_label = doc["elements"][3];
    old_label.RemoveMember("rotation");
    old_label.RemoveMember("world_scale");
    old_label.RemoveMember("depth");
    ASSERT_TRUE(restored.analyze(doc));
    EXPECT_EQ(restored.elements[3]->world_scale, (bgui::vec3{1, 1, 1}));
    EXPECT_FLOAT_EQ(restored.elements[3]->depth, 0.f);
    doc["elements"][0]["parent"].SetInt(0);
    EXPECT_FALSE(restored.analyze(doc));
    EXPECT_EQ(restored.elements.size(),4u);
    EXPECT_THROW(h->add("window","",-1),std::invalid_argument);
    registry->remove(entity.id);
    EXPECT_NO_THROW(lua.script("assert(gameObject:AddHUD() == nil); assert(gameObject:GetComponent('HUD') == nil); label.text='safe'"));
}

#include "ui/hud_view.hpp"
TEST(Hud, WidgetsReflectDataAndDisappearAfterEntityRemoval) {
    bgui::scoped_interface ui;
    auto registry=std::make_shared<commons::ecs>();
    auto entity=registry->create();
    registry->add<commons::hud>(entity);
    auto h=registry->get<commons::hud>(entity.id);
    auto label=h->add("text","before",-1);
    auto button=h->add("button","Use",-1);
    bgui::element viewport;
    viewport.set_final_rect(20,30,640,480);
    commons::hud_view view(registry,nullptr,viewport);
    view.on_update();
    auto& children=view.get_elements()[bgui::layer::base];
    ASSERT_EQ(children.size(),2u);
    auto* widget=dynamic_cast<bgui::text*>(children[0].get());
    ASSERT_NE(widget,nullptr);
    label->text="after";
    view.on_update();
    EXPECT_EQ(widget->get_buffer(),"after");
    auto* action=dynamic_cast<bgui::button*>(children[1].get());
    ASSERT_NE(action,nullptr);
    action->on_pressed();
    action->on_released();
    bgui::on_update();
    EXPECT_TRUE(button->consume_click());
    registry->remove(entity.id);
    view.on_update();
    EXPECT_TRUE(children.empty());
    EXPECT_EQ(view.processed_width(),0);
}

TEST(Hud, EditorPropertiesPersistAndRejectDuplicateIdsAtomically) {
    commons::hud h;
    auto a=h.add("text","Title");
    auto b=h.add("button","Use");
    EXPECT_NE(a->id,b->id);
    a->id="health"; a->absolute=true; a->position={25,35,0};
    a->width=180; a->height=45; a->padding=4; a->margin=2;
    a->alignment=2; a->color={.1f,.2f,.3f,1}; b->callback="use";
    rapidjson::Document doc;
    ASSERT_TRUE(h.serialize(doc,doc.GetAllocator()));
    commons::hud restored;
    ASSERT_TRUE(restored.analyze(doc));
    EXPECT_EQ(restored.elements[0]->id,"health");
    EXPECT_TRUE(restored.elements[0]->absolute);
    EXPECT_EQ(restored.elements[0]->padding,4);
    EXPECT_EQ(restored.elements[0]->alignment,2);
    EXPECT_EQ(restored.elements[0]->color,a->color);
    EXPECT_EQ(restored.elements[1]->callback,"use");
    doc["elements"][1]["id"].SetString("health",doc.GetAllocator());
    EXPECT_FALSE(restored.analyze(doc));
    EXPECT_EQ(restored.elements[1]->callback,"use");
    for(auto& item:doc["elements"].GetArray())
        for(auto key:{"id","callback","absolute","padding","margin","alignment","color"}) item.RemoveMember(key);
    ASSERT_TRUE(restored.analyze(doc));
    EXPECT_FALSE(restored.elements[0]->absolute);
    EXPECT_NE(restored.elements[0]->id,restored.elements[1]->id);
}

TEST(Hud, AbsoluteWidgetUsesViewportOriginAndScale) {
    bgui::scoped_interface ui;
    auto registry=std::make_shared<commons::ecs>();
    auto entity=registry->create(); registry->add<commons::hud>(entity);
    auto e=registry->get<commons::hud>(entity.id)->add("text","Placed");
    e->absolute=true; e->position={30,40,0}; e->width=160; e->height=40;
    bgui::element viewport; viewport.set_final_rect(20,50,640,480);
    commons::hud_view view(registry,nullptr,viewport); view.on_update();
    auto r=view.element_rect(e); const auto unit=bgui::get_global_scale();
    EXPECT_EQ(r.x,20+int(30*unit)); EXPECT_EQ(r.y,50+int(40*unit));
    EXPECT_EQ(r.z,int(160*unit)); EXPECT_EQ(r.w,int(40*unit));
}

TEST(Hud, ButtonCallbacksRunDuringScriptUpdateAndReportErrors) {
    auto registry=std::make_shared<commons::ecs>();
    auto entity=registry->create(); registry->add<commons::hud>(entity);
    auto e=registry->get<commons::hud>(entity.id)->add("button","Use");
    commons::code script;
    script.reg=registry; script.my_object=entity.id;
    script.estado_lua.open_libraries(sol::lib::base);
    script.estado_lua.script("calls=0; function use(id) calls=calls+1; last=id end; function broken() error('failed') end");
    e->callback="use"; e->clicked=true;
    EXPECT_EQ(script.estado_lua["calls"].get<int>(),0);
    EXPECT_NO_THROW(script.update());
    EXPECT_EQ(script.estado_lua["calls"].get<int>(),1);
    EXPECT_EQ(script.estado_lua["last"].get<std::string>(),e->id);
    EXPECT_FALSE(e->clicked);
    e->callback="broken"; e->clicked=true;
    EXPECT_NO_THROW(script.update());
    e->callback=""; e->clicked=true; script.update();
    EXPECT_TRUE(e->consume_click());
}
