#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <bgui.hpp>
#include "camera_movement.hpp"
#include "scene_navigation.hpp"
#include "entity_picking_system.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "guizmo_system.hpp"
#include "components_view.hpp"
#include "entities_view.hpp"

namespace {
template<class T, class Predicate>
T* find_element(bgui::element& root, Predicate predicate) {
    if (auto* item = dynamic_cast<T*>(&root); item && predicate(*item))
        return item;
    if (auto* layout = root.as_layout()) {
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* result = find_element<T>(*child, predicate))
                    return result;
    }
    return nullptr;
}

class EditorInteractionTest : public testing::Test {
protected:
    bgui::scoped_interface interface;
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    std::shared_ptr<editor::ui::editor_context> context =
        std::make_shared<editor::ui::editor_context>(registry);

    void SetUp() override {
        auto& io = bgui::get_context();
        io.m_input_map.clear();
        io.m_char_buffer.clear();
        io.m_size = {900, 600};
        io.m_last_mouse_left = false;
        io.m_scroll_delta_y = 0.f;
        io.m_scroll_delta_x = 0.f;
        io.m_mouse_position = {200, 150};
    }
};
}

TEST_F(EditorInteractionTest, RightDragLooksAroundWithoutMovingTheEye) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    bgui::get_context().m_mouse_position = {
        image.processed_x() + 80, image.processed_y() + 80
    };
    bgui::on_update();
    ASSERT_TRUE(editor::ui::viewport_owns_mouse(&image));
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    auto transform = registry->get<commons::transform>(entity.id);
    transform->set_position({2.f, 3.f, 4.f});
    float time = 0.f;
    editor::ui::camera_movement_system movement(camera, image, {}, [&]() { return time; });
    movement.setup(registry);
    bgui::get_context().m_input_map[bgui::input_key::mouse_right] =
        bgui::input_action::press;
    movement.update(registry);
    bgui::get_context().m_mouse_position.x += 40;
    time += 1.f / 60.f;
    movement.update(registry);
    EXPECT_GT(transform->get_rotation().y, 0.f);
    EXPECT_LT(transform->get_rotation().y, 6.f); // Lerp, not an immediate jump.
    EXPECT_EQ(transform->get_position(), (bgui::vec3{2.f, 3.f, 4.f}));
    bgui::get_context().m_input_map[bgui::input_key::mouse_right] = bgui::input_action::release;
    for (int frame = 0; frame < 60; ++frame) {
        time += 1.f / 60.f;
        movement.update(registry);
    }
    EXPECT_NEAR(transform->get_rotation().y, 6.f, 0.001f);
    EXPECT_EQ(transform->get_position(), (bgui::vec3{2.f, 3.f, 4.f}));
}

TEST_F(EditorInteractionTest, TrackpadPrimaryDragPanAndKeyboardNavigationUseLerp) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    auto& io = bgui::get_context();
    io.m_mouse_position = {image.processed_x() + 80, image.processed_y() + 80};
    bgui::on_update();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto transform = registry->get<commons::transform>(entity.id);
    float time = 0.f;
    editor::ui::camera_movement_system movement(
        registry->get<commons::camera>(entity.id), image, {}, [&]() { return time; });
    movement.setup(registry);
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    movement.update(registry);
    io.m_mouse_position.x += 40;
    time += 1.f / 60.f;
    movement.update(registry);
    EXPECT_GT(transform->get_rotation().y, 0.f);
    EXPECT_LT(transform->get_rotation().y, 6.f);
    EXPECT_EQ(transform->get_position(), (bgui::vec3{0.f, 0.f, 0.f}));
    io.m_input_map[bgui::input_key::left_shift] = bgui::input_action::press;
    io.m_mouse_position.y += 20;
    time += 1.f / 60.f;
    movement.update(registry);
    EXPECT_GT(transform->get_position().y, 0.f);
    EXPECT_LT(transform->get_position().y, 0.4f);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    io.m_input_map[bgui::input_key::left_shift] = bgui::input_action::release;
    io.m_input_map[bgui::input_key::w] = bgui::input_action::press;
    time += 1.f / 60.f;
    movement.update(registry);
    EXPECT_GT(transform->get_position().x, 0.f);
}

TEST_F(EditorInteractionTest, EntityDeleteActionHasAVisibleIconAndSupportsUndo) {
    auto entity = registry->create();
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::entities_view panel(context);
    panel.build(dock);
    auto* remove = find_element<bgui::button>(dock, [](auto& item) {
        return item.has_class("entities-remove-button");
    });
    ASSERT_NE(remove, nullptr);
    EXPECT_NE(find_element<bgui::icon>(*remove, [](auto&) { return true; }), nullptr);
    remove->on_pressed();
    remove->on_released();
    bgui::on_update();
    EXPECT_FALSE(registry->is_alive(entity.id));
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(registry->is_alive(entity.id));
    EXPECT_TRUE(registry->validate_invariants());
}

TEST_F(EditorInteractionTest, InspectorExposesPbrFactorsAndNormalMap) {
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    auto renderer = registry->get<commons::renderer>(entity.id);
    commons::material material;
    material.set_uniform("material.metallic", 0.2f);
    material.set_uniform("material.roughness", 0.6f);
    renderer->m_modelo->meshes.emplace_back(
        std::vector<commons::vertex>{}, std::vector<unsigned int>{}, material
    );
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    for (const auto* label : {"Metallic", "Roughness", "Ambient Occlusion", "Normal Map"}) {
        EXPECT_NE(find_element<bgui::text>(dock, [label](auto& item) {
            return item.get_buffer() == label;
        }), nullptr) << label;
    }
    auto* metallic = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Metallic";
    });
    ASSERT_NE(metallic, nullptr);
    metallic->get_inputbox().m_enter_func("0.85");
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].getMaterial()
        .getUniforme("material.metallic").valor.f, 0.85f);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].getMaterial()
        .getUniforme("material.metallic").valor.f, 0.2f);
    panel.update();
    metallic = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Metallic";
    });
    ASSERT_NE(metallic, nullptr);
    EXPECT_EQ(metallic->get_inputbox().get_buffer(), "0.2");
}

TEST_F(EditorInteractionTest, DeleteDoesNotRemoveAnEntityWhileEditingText) {
    const auto entity = registry->create();
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::entities_view panel(context);
    panel.build(dock);
    bgui::on_update();
    auto* input = find_element<bgui::inputbox>(dock, [](auto&) { return true; });
    ASSERT_NE(input, nullptr);
    auto& io = bgui::get_context();
    io.m_mouse_position = {input->processed_x() + input->processed_width() / 2,
        input->processed_y() + input->processed_height() / 2};
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    bgui::on_update();
    ASSERT_EQ(bgui::get_keyboard_focus(), input);
    io.m_input_map[bgui::input_key::delete_key] = bgui::input_action::press;
    bgui::on_update();
    EXPECT_TRUE(registry->is_alive(entity.id));
    io.m_input_map[bgui::input_key::delete_key] = bgui::input_action::release;
    bgui::on_update();
    bgui::clear_keyboard_focus();
    io.m_input_map[bgui::input_key::delete_key] = bgui::input_action::press;
    bgui::on_update();
    EXPECT_FALSE(registry->is_alive(entity.id));
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(registry->is_alive(entity.id));
}

TEST_F(EditorInteractionTest, CameraModifierUsesOnlyLeftControlAndRespectsTextFocus) {
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::space] = bgui::input_action::press;
    EXPECT_FALSE(editor::ui::viewport_camera_modifier());
    io.m_input_map.clear();
    io.m_input_map[bgui::input_key::right_control] = bgui::input_action::press;
    EXPECT_FALSE(editor::ui::viewport_camera_modifier());
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    EXPECT_TRUE(editor::ui::viewport_camera_modifier());
    std::string text;
    auto& input = bgui::get_layout().add<bgui::inputbox>(text);
    bgui::focus_keyboard(input);
    EXPECT_FALSE(editor::ui::viewport_camera_modifier());
}

TEST_F(EditorInteractionTest, CameraForwardRemainsContinuousAcrossYawNinetyDegrees) {
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    auto transform = registry->get<commons::transform>(entity.id);
    for (float yaw : {89.f, 91.f, 179.f, 181.f, 269.f, 271.f, 359.f, 361.f}) {
        transform->set_rotation({20.f, yaw, 0.f});
        camera->getViewMatrix();
        EXPECT_NEAR(camera->forward.x, std::cos(glm::radians(yaw)) * std::cos(glm::radians(20.f)), 0.0001f);
        EXPECT_NEAR(camera->forward.y, std::sin(glm::radians(20.f)), 0.0001f);
        EXPECT_NEAR(camera->forward.z, std::sin(glm::radians(yaw)) * std::cos(glm::radians(20.f)), 0.0001f);
    }
}

TEST_F(EditorInteractionTest, GizmoContinuesWhenItsCenterLeavesViewport) {
    commons::camera camera;
    camera.projMatriz = glm::mat4(1.f);
    camera.viewMatrix = glm::mat4(1.f);
    commons::transform target;
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {240, 200}, {}, true, true, true));
    gizmo.update(camera, &target, viewport, {viewport}, {600, 200}, {360, 0}, false, false, true);
    const auto outside = target.get_position();
    ASSERT_GT(outside.x, 1.f);
    gizmo.update(camera, &target, viewport, {viewport}, {620, 200}, {20, 0}, false, false, true);
    EXPECT_GT(target.get_position().x, outside.x);
    gizmo.update(camera, &target, viewport, {viewport}, {620, 200}, {}, false, false, false);
    EXPECT_FALSE(gizmo.is_dragging());
}

TEST_F(EditorInteractionTest, GizmoDragKeepsCaptureOutsideSceneWindow) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    auto entity = registry->create();
    registry->add<commons::transform>(entity);
    context->selected_entity = entity.id;
    context->preferences.show_grid = false;
    auto camera = std::make_shared<commons::camera>();
    camera->projMatriz = glm::mat4(1.f);
    camera->viewMatrix = glm::mat4(1.f);
    auto state = std::make_shared<editor::ui::viewport_input_state>();
    editor::ui::guizmo_system overlay(camera, image, context, state, {});
    auto& io = bgui::get_context();
    io.m_mouse_position = {image.processed_x() + image.processed_width() / 2 + 40,
                           image.processed_y() + image.processed_height() / 2};
    bgui::on_update();
    bgui::draw_data draw;
    overlay.get_requires(&draw);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    overlay.get_requires(&draw);
    ASSERT_TRUE(state->gizmo_active);
    auto transform = registry->get<commons::transform>(entity.id);
    io.m_mouse_position.x = 850;
    bgui::on_update();
    ASSERT_FALSE(editor::ui::viewport_owns_mouse(&image));
    overlay.get_requires(&draw);
    EXPECT_TRUE(state->gizmo_active);
    EXPECT_GT(transform->get_position().x, 0.f);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    overlay.get_requires(&draw);
    EXPECT_FALSE(state->gizmo_active);
    EXPECT_FALSE(context->undo_stack.empty());
    context->undo();
    EXPECT_EQ(transform->get_position(), (bgui::vec3{0.f, 0.f, 0.f}));
}

TEST_F(EditorInteractionTest, InspectorUsesCompactAlignedPropertyRows) {
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    auto* position = find_element<bgui::text>(dock, [](auto& item) {
        return item.get_buffer() == "Position";
    });
    auto* axis = find_element<bgui::text>(dock, [](auto& item) {
        return item.get_buffer() == "X";
    });
    ASSERT_NE(position, nullptr);
    ASSERT_NE(axis, nullptr);
    EXPECT_EQ(position->get_parent(), axis->get_parent()->get_parent());
    std::size_t row_children = 0;
    for (const auto& [layer, children] : position->get_parent()->as_layout()->get_elements())
        row_children += children.size();
    EXPECT_EQ(row_children, 2u);
    auto* fov = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Field of View";
    });
    auto* scale = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Orthographic Scale";
    });
    ASSERT_NE(fov, nullptr);
    ASSERT_NE(scale, nullptr);
    bgui::on_update();
    EXPECT_EQ(fov->get_inputbox().processed_x(), scale->get_inputbox().processed_x());
    EXPECT_EQ(fov->get_inputbox().processed_height(), 22);
}

TEST_F(EditorInteractionTest, RotationGizmoUsesWorldQuaternionAndKeepsWorldRings) {
    commons::camera camera;
    camera.projMatriz = glm::perspective(glm::radians(45.f), 1.f, 0.1f, 100.f);
    camera.viewMatrix = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, -5.f));
    commons::transform target({}, {35.f, 20.f, 15.f});
    const auto initial = target.get_world_model_matrix();
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    bgui::get_context().m_input_map[bgui::input_key::r] = bgui::input_action::press;
    // World Z ring: 74 pixels from center, away from the edge-on X/Y rings.
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {252, 148}, {}, true, true, true));
    EXPECT_EQ(target.get_rotation(), (bgui::vec3{35.f, 20.f, 15.f}));
    gizmo.update(camera, &target, viewport, {viewport}, {148, 148}, {-104, 0}, true, false, true);
    const auto expected = glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 0, 1)) * initial;
    const auto actual = target.get_world_model_matrix();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            EXPECT_NEAR(actual[c][r], expected[c][r], 0.001f);
    gizmo.update(camera, &target, viewport, {viewport}, {148, 148}, {}, true, false, false);
    // Ring remains in world XY after releasing an already rotated object.
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {252, 148}, {}, true, true, true));
}

TEST_F(EditorInteractionTest, RotationClockShowsSweepAndDisappearsOnRelease) {
    commons::camera camera;
    camera.projMatriz = glm::mat4(1.f);
    camera.viewMatrix = glm::mat4(1.f);
    commons::transform target;
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    bgui::get_context().m_input_map[bgui::input_key::r] = bgui::input_action::press;
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {252, 148}, {}, true, true, true));
    auto& draw = bgui::get_draw_list();
    draw.clear();
    gizmo.update(camera, &target, viewport, {viewport}, {148, 148}, {-104, 0}, true, false, true);
    int sector_vertices = 0;
    for (const auto& vertex : draw.get_vertices()) {
        if (std::abs(vertex.m_color.w - 0.22f) < 0.001f) {
            ++sector_vertices;
            EXPECT_GE(vertex.m_position.x, 0.f);
            EXPECT_LE(vertex.m_position.x, 400.f);
            EXPECT_LE(vertex.m_position.y, 200.001f);
        }
    }
    EXPECT_GT(sector_vertices, 0);
    draw.clear();
    gizmo.update(camera, &target, viewport, {viewport}, {148, 148}, {}, true, false, false);
    for (const auto& vertex : draw.get_vertices())
        EXPECT_NE(vertex.m_color.w, 0.22f);
}

TEST_F(EditorInteractionTest, RotationUsesPlaneAngleForObliqueRingAndAccumulatesTurns) {
    commons::camera camera;
    camera.projMatriz = glm::ortho(-2.f, 2.f, -2.f, 2.f, -10.f, 10.f);
    camera.viewMatrix = glm::rotate(glm::mat4(1.f), glm::radians(60.f), glm::vec3(1, 0, 0));
    commons::transform target;
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    bgui::get_context().m_input_map[bgui::input_key::r] = bgui::input_action::press;
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {252, 174}, {}, true, true, true));
    // Equal world-plane angles are not equal screen polar angles on this ellipse.
    const std::vector<bgui::vec2i> points{{148,174}, {148,226}, {252,226}, {252,174}, {148,174}};
    for (const auto& point : points)
        gizmo.update(camera, &target, viewport, {viewport}, point, {}, true, false, true);
    const auto expected = glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 0, 1));
    const auto actual = target.get_world_model_matrix();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            EXPECT_NEAR(actual[c][r], expected[c][r], 0.001f);
}

TEST_F(EditorInteractionTest, WorldRotationGizmoRespectsRotatedParent) {
    auto parent_entity = registry->create();
    registry->add<commons::transform>(parent_entity);
    auto child_entity = registry->create();
    registry->add<commons::transform>(child_entity);
    auto parent = registry->get<commons::transform>(parent_entity.id);
    auto child = registry->get<commons::transform>(child_entity.id);
    parent->set_rotation({30.f, 20.f, 10.f});
    child->set_rotation({15.f, 25.f, 35.f});
    ASSERT_TRUE(child->set_parent(parent_entity.id, false));
    const auto initial = child->get_world_model_matrix();
    commons::camera camera;
    camera.projMatriz = glm::mat4(1.f);
    camera.viewMatrix = glm::mat4(1.f);
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    bgui::get_context().m_input_map[bgui::input_key::r] = bgui::input_action::press;
    ASSERT_TRUE(gizmo.update(camera, child.get(), viewport, {viewport}, {252, 148}, {}, true, true, true));
    gizmo.update(camera, child.get(), viewport, {viewport}, {148, 148}, {-104, 0}, true, false, true);
    const auto expected = glm::rotate(glm::mat4(1.f), glm::radians(90.f), glm::vec3(0, 0, 1)) * initial;
    const auto actual = child->get_world_model_matrix();
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            EXPECT_NEAR(actual[c][r], expected[c][r], 0.001f);
}

TEST_F(EditorInteractionTest, EdgeOnRotationRemainsFiniteAndReversesWithDrag) {
    commons::camera camera;
    camera.projMatriz = glm::mat4(1.f);
    camera.viewMatrix = glm::mat4(1.f);
    commons::transform target;
    editor::transform_gizmo gizmo;
    const bgui::vec4i viewport{0, 0, 400, 400};
    bgui::get_context().m_input_map[bgui::input_key::r] = bgui::input_action::press;
    ASSERT_TRUE(gizmo.update(camera, &target, viewport, {viewport}, {200, 148}, {}, true, true, true));
    gizmo.update(camera, &target, viewport, {viewport}, {200, 170}, {0, 22}, true, false, true);
    EXPECT_GT(std::abs(target.get_rotation().x), 1.f);
    gizmo.update(camera, &target, viewport, {viewport}, {200, 148}, {0, -22}, true, false, true);
    const auto actual = target.get_world_model_matrix();
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            EXPECT_NEAR(actual[c][r], c == r ? 1.f : 0.f, 0.001f);
}

TEST_F(EditorInteractionTest, InspectorComponentsAreIndependentTreesAndAddButtonIsLast) {
    auto entity = registry->create();
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    auto* transform = find_element<bgui::tree>(dock, [](auto& item) {
        return item.label().get_buffer() == "Transform";
    });
    auto* add = find_element<bgui::button>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "+ Add Component";
    });
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(transform->get_parent(), add->get_parent());
    EXPECT_TRUE(transform->has_icon());
    bgui::on_update();
    EXPECT_GT(add->processed_y(), transform->processed_y());
}

TEST_F(EditorInteractionTest, ParentedCameraKeepsPitchAndYawAcrossFullTurns) {
    auto parent = registry->create();
    auto child = registry->create();
    registry->add<commons::camera>(child);
    auto body = registry->get<commons::transform>(parent.id);
    auto eye = registry->get<commons::transform>(child.id);
    ASSERT_TRUE(eye->set_parent(parent.id, false));
    eye->set_rotation({20.f, 0.f, 0.f});
    body->set_scale({2.f, 0.5f, 3.f});
    auto camera = registry->get<commons::camera>(child.id);
    for (float yaw : {-721.f, -181.f, -91.f, -89.f, 89.f, 91.f, 181.f, 361.f, 721.f}) {
        body->set_rotation({0.f, yaw, 0.f});
        camera->getViewMatrix();
        EXPECT_NEAR(camera->forward.x, std::cos(glm::radians(yaw)) * std::cos(glm::radians(20.f)), 0.0001f);
        EXPECT_NEAR(camera->forward.y, std::sin(glm::radians(20.f)), 0.0001f);
        EXPECT_NEAR(camera->forward.z, -std::sin(glm::radians(yaw)) * std::cos(glm::radians(20.f)), 0.0001f);
    }
}

TEST_F(EditorInteractionTest, SwitchingOrbitalAndNormalPreservesEyeAndChangesDragPivot) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    auto& io = bgui::get_context();
    io.m_mouse_position = {image.processed_x() + 80, image.processed_y() + 80};
    bgui::on_update();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto eye = registry->get<commons::transform>(entity.id);
    eye->set_position({2.f, 3.f, 4.f});
    float time = 0.f;
    editor::ui::camera_movement_system movement(
        registry->get<commons::camera>(entity.id), image, {}, [&] { return time; });
    movement.setup(registry);
    movement.set_orbital(true);
    ASSERT_TRUE(movement.is_orbital());
    EXPECT_EQ(eye->get_position(), (bgui::vec3{2.f, 3.f, 4.f}));
    io.m_input_map[bgui::input_key::mouse_right] = bgui::input_action::press;
    movement.update(registry);
    io.m_mouse_position.x += 100;
    for (int i = 0; i < 90; ++i) { time += 1.f / 60.f; movement.update(registry); }
    auto position = eye->get_position();
    EXPECT_GT((position - bgui::vec3{2.f, 3.f, 4.f}).length(), 1.f);
    EXPECT_NEAR((position - bgui::vec3{12.f, 3.f, 4.f}).length(), 10.f, 0.001f);
    movement.set_orbital(false);
    ASSERT_FALSE(movement.is_orbital());
    io.m_mouse_position.x += 100;
    for (int i = 0; i < 90; ++i) { time += 1.f / 60.f; movement.update(registry); }
    EXPECT_EQ(eye->get_position(), position);
}

TEST_F(EditorInteractionTest, SelectedOrbitUsesWorldPivotAndObjectRelativeDistance) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    auto eye = registry->get<commons::transform>(camera_entity.id);
    auto parent = registry->create();
    auto target_entity = registry->create();
    auto target = registry->get<commons::transform>(target_entity.id);
    registry->get<commons::transform>(parent.id)->set_position({20.f, 0.f, 0.f});
    target->set_parent(parent.id, false);
    target->set_position({3.f, 4.f, 5.f});
    const auto small = editor::ui::selected_orbit_distance(*target, nullptr, 45.f);
    target->set_scale({4.f, 4.f, 4.f});
    const auto large = editor::ui::selected_orbit_distance(*target, nullptr, 45.f);
    EXPECT_NEAR(large, small * 4.f, 0.001f);
    float time = 0.f;
    editor::ui::camera_movement_system movement(camera, image, {}, [&] { return time; });
    movement.setup(registry);
    movement.focus_orbit_target(target, large);
    for (int i = 0; i < 100; ++i) { time += 1.f / 60.f; movement.update(registry); }
    EXPECT_NEAR((eye->get_position() - target->get_world_position()).length(), large, 0.001f);
    EXPECT_NEAR(eye->get_position().x, 23.f - large, 0.001f);
    target->set_position({5.f, 4.f, 5.f});
    for (int i = 0; i < 100; ++i) { time += 1.f / 60.f; movement.update(registry); }
    EXPECT_NEAR(eye->get_position().x, 25.f - large, 0.001f);
}

TEST_F(EditorInteractionTest, PrimaryDragRotatesWithoutSelectingAndGizmoBlocksCamera) {
    auto& window = bgui::get_layout().add<bgui::window>("Viewport", true);
    window.style.layout.require_size(500.f, 400.f);
    auto& image = window.add<bgui::image>();
    image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    image.receives_input(true);
    bgui::on_update();
    auto& io = bgui::get_context();
    io.m_mouse_position = {image.processed_x() + 80, image.processed_y() + 80};
    bgui::on_update();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    auto eye = registry->get<commons::transform>(entity.id);
    auto state = std::make_shared<editor::ui::viewport_input_state>();
    float time = 0.f;
    editor::ui::camera_movement_system movement(camera, image, state, [&] { return time; });
    editor::ui::entity_picking_system picker(camera, image, context, state);
    context->selected_entity = entity.id;
    movement.setup(registry);
    picker.setup(registry);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    movement.update(registry); picker.update(registry);
    io.m_mouse_position.x += 40;
    time += 1.f / 60.f;
    movement.update(registry); picker.update(registry);
    EXPECT_GT(eye->get_rotation().y, 0.f);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    picker.update(registry);
    EXPECT_EQ(context->selected_entity, entity.id);
    // Let smoothing settle before checking that a new gizmo drag adds no rotation.
    for (int i = 0; i < 100; ++i) { time += 1.f / 60.f; movement.update(registry); }
    const auto rotation = eye->get_rotation();
    state->gizmo_active = true;
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    movement.update(registry);
    io.m_mouse_position.x += 80;
    time += 1.f / 60.f;
    movement.update(registry);
    EXPECT_NEAR(eye->get_rotation().y, rotation.y, 0.001f);
    state->gizmo_active = false;
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    picker.update(registry);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    picker.update(registry);
    EXPECT_EQ(context->selected_entity, entity.id);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    picker.update(registry);
    EXPECT_FALSE(context->selected_entity); // Short click on empty space still selects/clears.
}
