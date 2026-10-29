#include <gtest/gtest.h>
#include "api/lua.hpp"
#include "components/code.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/physics.hpp"
#include "core/ecs.hpp"
#include "systems/code_system.hpp"
#include <fstream>
#include <filesystem>
#include <chrono>

TEST(LuaApi, UnityStyleTransformAndLegacyApiCoexist) {
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
