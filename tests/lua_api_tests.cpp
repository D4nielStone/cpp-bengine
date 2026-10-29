#include <gtest/gtest.h>
#include "debugging/script_log.hpp"
#include "api/lua.hpp"
#include "components/code.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/physics.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/camera.hpp"
#include "core/ecs.hpp"
#include "systems/code_system.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>

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
    commons::api::setUnityApi(lua, registry, child.id);
    EXPECT_NO_THROW(lua.script(R"(
        transform.localPosition = Vector3(2, 0, 0)
        assert(transform.position.x == 12)
        transform.position = Vector3(15, 0, 0)
        assert(transform.localPosition.x == 5)
    )"));
}

TEST(LuaApi, FppScriptsMoveLookAndCollectUsingTheNewApi) {
    auto registry = std::make_shared<commons::ecs>();
    auto player = registry->create(106);
    auto camera = registry->create(100);
    auto pickup = registry->create(103);
    registry->get<commons::transform>(player.id)->set_rotation({0.f, -90.f, 0.f});
    registry->get<commons::transform>(camera.id)->set_parent(player.id, false);
    registry->add<commons::renderer>(pickup);
    registry->add<commons::physics>(pickup);
    auto load = [&](commons::entity& entity, const std::string& filename) {
        registry->add<commons::code>(entity,
            std::string(BENGINE_SOURCE_DIR) + "/projects/fpp_demo/scripts/" + filename);
        auto script = registry->get<commons::code>(entity.id);
        script->setup();
        return script;
    };
    auto body = load(player, "fpp_player.lua");
    auto setup = body->f_setup();
    ASSERT_TRUE(setup.valid()) << sol::error(setup).what();
    auto input = body->estado_lua["Input"].get<sol::table>();
    input.set_function("GetAxis", [](const std::string& axis) { return axis == "Vertical" ? 1.f : 0.f; });
    input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{10.f, 0.f}; });
    body->estado_lua["Time"]["deltaTime"] = 0.1f;
    auto moved = body->f_update();
    ASSERT_TRUE(moved.valid()) << sol::error(moved).what();
    auto player_transform = registry->get<commons::transform>(player.id);
    EXPECT_NEAR(player_transform->get_position().z, 0.25f, 0.0001f);
    EXPECT_NEAR(player_transform->get_rotation().y, -91.2f, 0.0001f);

    auto look = load(camera, "fpp_camera.lua");
    setup = look->f_setup();
    ASSERT_TRUE(setup.valid()) << sol::error(setup).what();
    auto camera_input = look->estado_lua["Input"].get<sol::table>();
    camera_input.set_function("GetMouseDelta", []() { return std::tuple<float, float>{0.f, 10000.f}; });
    auto looked = look->f_update();
    ASSERT_TRUE(looked.valid()) << sol::error(looked).what();
    EXPECT_FLOAT_EQ(registry->get<commons::transform>(camera.id)->get_rotation().x, -89.f);

    registry->get<commons::transform>(pickup.id)->set_position(player_transform->get_position());
    auto game = load(pickup, "fpp_game.lua");
    setup = game->f_setup();
    ASSERT_TRUE(setup.valid()) << sol::error(setup).what();
    auto collected = game->f_update();
    ASSERT_TRUE(collected.valid()) << sol::error(collected).what();
    EXPECT_FALSE(registry->get<commons::renderer>(pickup.id)->visible);
    EXPECT_FALSE(registry->has<commons::physics>(pickup.id));
    auto repeated = game->f_update();
    EXPECT_TRUE(repeated.valid());
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
    commons::api::setUnityApi(lua, registry, entity.id);
    auto result = lua.safe_script("transform:Translate(Vector3(0, 0, 1))", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
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
    registry->add<commons::camera>(entity);
    registry->add<commons::renderer>(entity);
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    commons::api::setNamespaces(lua);
    lua["bubble"]["input"] = lua.create_table();
    commons::api::setUnityApi(lua, registry, entity.id);
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
        body.velocity = Vector3(1, 2, 3)
        local light = gameObject:GetComponent("DirectionalLight")
        light.intensity = 4
        light.color = Vector3(0.1, 0.2, 0.3)
        gameObject:GetComponent("PointLight").intensity = 8
    )", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    auto body = registry->get<commons::physics>(entity.id);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getInvMass(), .5f);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getFriction(), .7f);
    EXPECT_FLOAT_EQ(body->m_rigid_body->getRestitution(), .3f);
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->visible);
    const auto cam = registry->get<commons::camera>(entity.id);
    EXPECT_FLOAT_EQ(cam->fov, 60.f);
    EXPECT_FLOAT_EQ(cam->corte_curto, .2f);
    EXPECT_FLOAT_EQ(cam->corte_longo, 500.f);
    EXPECT_TRUE(cam->flag_orth);
    EXPECT_FLOAT_EQ(cam->scale, 12.f);
    EXPECT_FLOAT_EQ(cam->ceu.g, .2f);
    EXPECT_FLOAT_EQ(body->getVelocidade().y, 2.f);
    EXPECT_FLOAT_EQ(registry->get<commons::directional_light>(entity.id)->intensity, 4.f);
    EXPECT_FLOAT_EQ(registry->get<commons::point_light>(entity.id)->intensity, 8.f);
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
