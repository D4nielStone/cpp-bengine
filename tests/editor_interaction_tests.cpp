#include <tuple>
#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <bgui.hpp>
#include "camera_movement.hpp"
#include "scene_navigation.hpp"
#include "direction_gizmo.hpp"
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
    bgui::scoped_interface ui_scope;
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

TEST_F(EditorInteractionTest, EntityDeletionHistoryDoesNotKeepItsContextAlive) {
    for (const bool leave_in_redo : {false, true}) {
        auto entity = registry->create();
        std::weak_ptr<commons::transform> removed = registry->get<commons::transform>(entity.id);
        auto owner = std::make_shared<editor::ui::editor_context>(registry);
        std::weak_ptr<editor::ui::editor_context> weak_owner = owner;
        owner->selected_entity = entity.id;
        {
            bgui::dock dock;
            editor::ui::entities_view panel(owner);
            panel.build(dock);
            auto* remove = find_element<bgui::button>(dock, [](auto& item) {
                return item.has_class("entities-remove-button");
            });
            ASSERT_NE(remove, nullptr);
            remove->on_pressed();
            remove->on_released();
            bgui::on_update();
            ASSERT_FALSE(registry->is_alive(entity.id));
            if (leave_in_redo)
                ASSERT_TRUE(owner->undo());
        }
        owner.reset();
        EXPECT_TRUE(weak_owner.expired());
        registry->remove(entity.id);
        EXPECT_TRUE(removed.expired());
    }
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
    auto* rotation = find_element<bgui::text>(dock, [](auto& item) {
        return item.get_buffer() == "Rotation";
    });
    ASSERT_NE(rotation, nullptr);
    const auto* position_row = position->get_parent();
    const auto* rotation_row = rotation->get_parent();
    EXPECT_EQ(position_row->processed_height(), 22);
    EXPECT_EQ(rotation_row->processed_y() -
        (position_row->processed_y() + position_row->processed_height()), 2);
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
        return item.get_label().get_buffer() == "Add Component";
    });
    ASSERT_NE(transform, nullptr);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(transform->get_parent(), add->get_parent());
    EXPECT_TRUE(transform->has_icon());
    bgui::on_update();
    EXPECT_GT(add->processed_y(), transform->processed_y());
}

TEST_F(EditorInteractionTest, InspectorAddsAndEditsSoundComponents) {
    auto entity = registry->create();
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();

    const auto click = [&](const std::string& label) {
        auto* button = find_element<bgui::button>(dock, [&](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(button, nullptr) << label;
        button->on_pressed();
        button->on_released();
        bgui::on_update();
    };

    click("Add Component");
    click("Sound Player");
    auto player = registry->get<commons::sound_player>(entity.id);
    ASSERT_NE(player, nullptr);
    EXPECT_NE(find_element<bgui::tree>(dock, [](auto& item) {
        return item.label().get_buffer() == "Sound Player";
    }), nullptr);
    auto* radius = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Area Radius";
    });
    ASSERT_NE(radius, nullptr);
    radius->get_inputbox().m_enter_func("24");
    EXPECT_FLOAT_EQ(player->radius, 24.f);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(player->radius, 10.f);
    ASSERT_TRUE(context->redo());
    EXPECT_FLOAT_EQ(player->radius, 24.f);

    click("Add Component");
    click("Sound Listener");
    EXPECT_NE(registry->get<commons::sound_listener>(entity.id), nullptr);
    EXPECT_NE(find_element<bgui::tree>(dock, [](auto& item) {
        return item.label().get_buffer() == "Sound Listener";
    }), nullptr);
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

TEST_F(EditorInteractionTest, InspectorShadowControlsSupportUndoAndRedo) {
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    registry->add<commons::directional_light>(entity);
    const auto renderer = registry->get<commons::renderer>(entity.id);
    const auto light = registry->get<commons::directional_light>(entity.id);
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    for (const auto& [label, value] : {std::pair{"Cast Shadows", &renderer->cast_shadows},
            {"Receive Shadows", &renderer->receive_shadows}, {"Shadows", &light->shadows}}) {
        auto* checkbox = find_element<bgui::checkbox>(dock, [label](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(checkbox, nullptr) << label;
        EXPECT_TRUE(*value);
        checkbox->on_clicked();
        EXPECT_FALSE(*value);
        ASSERT_TRUE(context->undo());
        EXPECT_TRUE(*value);
        ASSERT_TRUE(context->redo());
        EXPECT_FALSE(*value);
    }
    auto* resolution = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Shadow Resolution";
    });
    ASSERT_NE(resolution, nullptr);
    resolution->get_inputbox().m_enter_func("1024");
    EXPECT_EQ(light->shadow_resolution, 1024);
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(light->shadow_resolution, 2048);
    ASSERT_TRUE(context->redo());
    EXPECT_EQ(light->shadow_resolution, 1024);
    resolution->get_inputbox().m_enter_func("-5");
    EXPECT_EQ(light->shadow_resolution, 128);
    resolution->get_inputbox().m_enter_func("99999");
    EXPECT_EQ(light->shadow_resolution, 8192);
    auto* distance = find_element<bgui::field>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Shadow Distance";
    });
    ASSERT_NE(distance, nullptr);
    distance->get_inputbox().m_enter_func("75");
    EXPECT_FLOAT_EQ(light->shadow_distance, 75.f);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(light->shadow_distance, 50.f);
}

TEST_F(EditorInteractionTest, InspectorKeepsSectionExpansionAcrossRefreshes) {
    auto entity = registry->create();
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();

    auto* section = find_element<bgui::tree>(dock, [](auto& item) {
        return item.label().get_buffer() == "Transform";
    });
    ASSERT_NE(section, nullptr);
    ASSERT_TRUE(section->is_expanded());
    section->set_expanded(false);

    panel.refresh();
    panel.update();

    section = find_element<bgui::tree>(dock, [](auto& item) {
        return item.label().get_buffer() == "Transform";
    });
    ASSERT_NE(section, nullptr);
    EXPECT_FALSE(section->is_expanded());
}

TEST_F(EditorInteractionTest, InspectorRefreshesUnfocusedInputsWhenPropertiesChange) {
    auto entity = registry->create();
    auto transform = registry->get<commons::transform>(entity.id);
    ASSERT_NE(transform, nullptr);
    transform->set_position({2.25f, 3.5f, 4.75f});
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();

    auto* x_input = find_element<bgui::inputbox>(dock, [](auto& item) {
        return item.get_buffer() == "2.25";
    });
    ASSERT_NE(x_input, nullptr);
    transform->set_position({7.25f, 8.5f, 9.75f});
    panel.update();

    x_input = find_element<bgui::inputbox>(dock, [](auto& item) {
        return item.get_buffer() == "7.25";
    });
    EXPECT_NE(x_input, nullptr);
    EXPECT_NE(find_element<bgui::inputbox>(dock, [](auto& item) {
        return item.get_buffer() == "8.5";
    }), nullptr);
    EXPECT_NE(find_element<bgui::inputbox>(dock, [](auto& item) {
        return item.get_buffer() == "9.75";
    }), nullptr);

    ASSERT_NE(x_input, nullptr);
    x_input->set_focused(true);
    x_input->set_buffer("editing");
    transform->set_position({6.f, 8.5f, 9.75f});
    panel.update();
    EXPECT_EQ(x_input->get_buffer(), "editing");
    x_input->set_focused(false);
    panel.update();
    EXPECT_EQ(x_input->get_buffer(), "6");
}

TEST_F(EditorInteractionTest, SpotShadowControlsSupportUndoRedoAndClampValues) {
    auto entity = registry->create();
    registry->add<commons::spot_light>(entity);
    const auto light = registry->get<commons::spot_light>(entity.id);
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    auto* toggle = find_element<bgui::checkbox>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Shadows";
    });
    ASSERT_NE(toggle, nullptr);
    toggle->on_clicked();
    EXPECT_FALSE(light->shadows);
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(light->shadows);
    ASSERT_TRUE(context->redo());
    EXPECT_FALSE(light->shadows);
    for (const auto& [label, member, initial, updated, maximum] : {
        std::tuple{"Shadow Distance", &commons::spot_light::shadow_distance, 50.f, 75.f, 10000.f},
        {"Shadow Bias", &commons::spot_light::shadow_bias, 0.001f, 0.002f, 0.1f},
        {"Shadow Strength", &commons::spot_light::shadow_strength, 1.f, 0.5f, 1.f}}) {
        auto* field = find_element<bgui::field>(dock, [label](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(field, nullptr);
        field->get_inputbox().m_enter_func(std::to_string(updated));
        EXPECT_FLOAT_EQ(light.get()->*member, updated);
        ASSERT_TRUE(context->undo());
        EXPECT_FLOAT_EQ(light.get()->*member, initial);
        ASSERT_TRUE(context->redo());
        EXPECT_FLOAT_EQ(light.get()->*member, updated);
        field->get_inputbox().m_enter_func("99999");
        EXPECT_FLOAT_EQ(light.get()->*member, maximum);
    }
}

TEST_F(EditorInteractionTest, SpotLightColorUsesColorPickerAndSupportsUndoRedo) {
    auto entity = registry->create();
    registry->add<commons::spot_light>(entity);
    const auto light = registry->get<commons::spot_light>(entity.id);
    light->color = {0.2f, 0.4f, 0.6f};
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();

    auto* picker = find_element<bgui::color_picker>(dock, [](auto&) { return true; });
    ASSERT_NE(picker, nullptr);
    EXPECT_FLOAT_EQ(picker->get_color().x, 0.2f);
    EXPECT_FLOAT_EQ(picker->get_color().y, 0.4f);
    EXPECT_FLOAT_EQ(picker->get_color().z, 0.6f);
    const auto expect_color = [&light](const bgui::vec3& expected) {
        EXPECT_NEAR(light->color.x, expected.x, 0.0001f);
        EXPECT_NEAR(light->color.y, expected.y, 0.0001f);
        EXPECT_NEAR(light->color.z, expected.z, 0.0001f);
    };
    picker->set_color({0.8f, 0.3f, 0.1f, 1.f});
    expect_color({0.8f, 0.3f, 0.1f});
    ASSERT_TRUE(context->undo());
    expect_color({0.2f, 0.4f, 0.6f});
    ASSERT_TRUE(context->redo());
    expect_color({0.8f, 0.3f, 0.1f});
}


TEST_F(EditorInteractionTest, InspectorExposesBodyTypesAndColliderControlsWithHistory) {
    auto entity = registry->create();
    registry->add<commons::physics>(entity);
    auto physics = registry->get<commons::physics>(entity.id);
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context); panel.build(dock); panel.update();
    auto* type = find_element<bgui::combo_box>(dock, [](auto& item) {
        return item.get_selected_option() == "Rigid Body";
    });
    auto* shape = find_element<bgui::combo_box>(dock, [](auto& item) {
        return item.get_selected_option() == "Box";
    });
    ASSERT_NE(type, nullptr); ASSERT_NE(shape, nullptr);
    // Programmatic selection follows the same callback as user selection.
    type->set_selected_index(1);
    EXPECT_EQ(physics->type, commons::physics::body_type::character);
    EXPECT_EQ(physics->m_rigid_body->getAngularFactor(), btVector3(0,0,0));
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(physics->type, commons::physics::body_type::rigid);
    ASSERT_TRUE(context->redo());
    shape->set_selected_index(2);
    EXPECT_EQ(physics->e_forma, commons::physics::shapes::forma_esfera);
    auto* trigger = find_element<bgui::checkbox>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Trigger";
    });
    ASSERT_NE(trigger, nullptr); trigger->on_clicked(); EXPECT_TRUE(physics->trigger);
    ASSERT_TRUE(context->undo()); EXPECT_FALSE(physics->trigger);
    auto* enabled = find_element<bgui::checkbox>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Collision Enabled";
    });
    ASSERT_NE(enabled, nullptr); enabled->on_clicked(); EXPECT_FALSE(physics->collision_enabled);
    registry->remove(entity.id);
}


TEST_F(EditorInteractionTest, ColliderFitAndRotationOffsetsAreAvailableInTheInspector) {
    auto entity = registry->create(); registry->add<commons::physics>(entity);
    auto body = registry->get<commons::physics>(entity.id);
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context); panel.build(dock); panel.update();
    auto* fit = find_element<bgui::checkbox>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "Fit to Model";
    });
    ASSERT_NE(fit,nullptr); EXPECT_TRUE(body->fit_to_model);
    fit->on_clicked(); EXPECT_FALSE(body->fit_to_model);
    ASSERT_TRUE(context->undo()); EXPECT_TRUE(body->fit_to_model);
    EXPECT_NE(find_element<bgui::text>(dock, [](auto& item) {
        return item.get_buffer() == "Rotation Offset";
    }),nullptr);
    registry->remove(entity.id);
}

TEST_F(EditorInteractionTest, InspectorTextureMappingSupportsUndoAndRedo) {
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    auto renderer = registry->get<commons::renderer>(entity.id);
    renderer->m_modelo->meshes.emplace_back(std::vector<commons::vertex>{},
        std::vector<unsigned int>{}, commons::material{});
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock); panel.update();
    for (const auto& factor : commons::texture_factors) {
        auto* field = find_element<bgui::field>(dock, [&](auto& item) {
            return item.get_label().get_buffer() == factor.label;
        });
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->get_inputbox().get_buffer(), factor.fallback == 0 ? "0" : "1");
        field->get_inputbox().m_enter_func("-2.5");
        auto value = [&]() { return commons::pbr_value(renderer->m_modelo->meshes[0].getMaterial(), factor); };
        EXPECT_FLOAT_EQ(value(), -2.5f);
        ASSERT_TRUE(context->undo()); EXPECT_FLOAT_EQ(value(), factor.fallback);
        ASSERT_TRUE(context->redo()); EXPECT_FLOAT_EQ(value(), -2.5f);
    }
    auto* world = find_element<bgui::checkbox>(dock, [](auto& item) {
        return item.get_label().get_buffer() == "WorldTexture";
    });
    ASSERT_NE(world, nullptr);
    world->on_clicked();
    EXPECT_TRUE(commons::world_texture(renderer->m_modelo->meshes[0].getMaterial()));
    ASSERT_TRUE(context->undo());
    EXPECT_FALSE(commons::world_texture(renderer->m_modelo->meshes[0].getMaterial()));
    ASSERT_TRUE(context->redo());
    EXPECT_TRUE(commons::world_texture(renderer->m_modelo->meshes[0].getMaterial()));
}

TEST(DirectionGizmo, ProjectsWorldAxesUsingViewRotationOnly) {
    const auto identity = editor::ui::direction_gizmo_axes(glm::mat4(1.f));
    EXPECT_FLOAT_EQ(identity[0].x, 1.f);
    EXPECT_FLOAT_EQ(identity[1].y, 1.f);
    EXPECT_FLOAT_EQ(identity[2].z, 1.f);
    auto view = glm::lookAt(glm::vec3(4, 0, 0), glm::vec3(0), glm::vec3(0, 1, 0));
    const auto axes = editor::ui::direction_gizmo_axes(view);
    EXPECT_NEAR(axes[0].z, 1.f, 0.0001f);
    EXPECT_NEAR(axes[2].x, -1.f, 0.0001f);
    view[3] = glm::vec4(100, 200, 300, 1);
    const auto moved = editor::ui::direction_gizmo_axes(view);
    EXPECT_EQ(axes, moved);
}

TEST(DirectionGizmo, FitsTopRightWithScalingAndSmallViewports) {
    const auto rect = editor::ui::direction_gizmo_rect({20, 30, 800, 600}, 2.f);
    EXPECT_EQ(rect.x, 596);
    EXPECT_EQ(rect.y, 54);
    EXPECT_EQ(rect.z, 200);
    const auto small = editor::ui::direction_gizmo_rect({0, 0, 70, 60}, 1.f);
    EXPECT_EQ(small.z, 36);
    EXPECT_EQ(small.x + small.z, 58);
    EXPECT_EQ(editor::ui::direction_gizmo_rect({0, 0, 0, 0}, 1.f).z, 0);
}

TEST_F(EditorInteractionTest, DirectionGizmoDrawsInsideViewportWithoutTakingInput) {
    auto& root = bgui::get_layout();
    auto& image = root.add<bgui::image>();
    image.set_final_rect(20, 30, 800, 600);
    auto camera = std::make_shared<commons::camera>();
    auto& gizmo = root.add<editor::ui::direction_gizmo, bgui::layer::overlay>(camera, image);
    gizmo.on_update();
    bgui::draw_data data;
    gizmo.get_requires(&data);
    // The global draw list is rendered after all windows, even modal windows.
    EXPECT_TRUE(data.m_draw_list.get_vertices().empty());
    ASSERT_EQ(data.m_quad_requires.size(), 1u);
    const auto& call = data.m_quad_requires.front();
    EXPECT_EQ(call.m_rect.x, gizmo.processed_x());
    EXPECT_EQ(call.m_rect.y, gizmo.processed_y());
    EXPECT_EQ(call.m_rect.z, gizmo.processed_width());
    EXPECT_EQ(call.m_material.m_shader_tag, "ui::image");
    EXPECT_TRUE(call.m_material.m_texture.m_has_alpha);
    EXPECT_FALSE(call.m_material.m_texture.m_buffer.empty());
    EXPECT_FALSE(gizmo.receives_input());
    EXPECT_FALSE(gizmo.supports_render_cache());
}

TEST_F(EditorInteractionTest, DirectionGizmoStaysBeforeLaterWindowsAndModals) {
    auto& root = bgui::get_layout();
    auto& scene = root.add<bgui::window>("Scene", true);
    auto& image = scene.add<bgui::image>();
    image.set_final_rect(0, 0, 800, 600);
    auto camera = std::make_shared<commons::camera>();
    auto& gizmo = scene.add<editor::ui::direction_gizmo, bgui::layer::overlay>(camera, image);
    gizmo.on_update();
    auto& window = root.add<bgui::window>("Floating", true);
    auto& modal = root.add<bgui::modal, bgui::layer::overlay>();
    bgui::draw_data data;
    data.m_clip_rect = {0, 0, 900, 600};
    gizmo.get_requires(&data);
    window.bgui::element::get_requires(&data);
    modal.bgui::element::get_requires(&data);
    EXPECT_TRUE(data.m_draw_list.get_vertices().empty());
    ASSERT_EQ(data.m_quad_requires.size(), 3u);
    EXPECT_EQ(data.m_quad_requires.front().m_material.m_shader_tag, "ui::image");
    data.m_quad_requires.pop();
    EXPECT_NE(data.m_quad_requires.front().m_material.m_shader_tag, "ui::image");
    data.m_quad_requires.pop();
    EXPECT_NE(data.m_quad_requires.front().m_material.m_shader_tag, "ui::image");
}

TEST_F(EditorInteractionTest, DirectionGizmoTextureTracksRotationAndScale) {
    auto& root = bgui::get_layout();
    auto& image = root.add<bgui::image>();
    image.set_final_rect(0, 0, 800, 600);
    auto camera = std::make_shared<commons::camera>();
    camera->viewMatrix = glm::mat4(1.f);
    auto& gizmo = root.add<editor::ui::direction_gizmo>(camera, image);
    gizmo.on_update();
    bgui::draw_data first;
    gizmo.get_requires(&first);
    const auto original = first.m_quad_requires.front().m_material.m_texture;
    ASSERT_FALSE(original.m_buffer.empty());
    EXPECT_EQ(original.m_buffer[3], 0); // Transparent outside the disk.
    bgui::draw_data second;
    gizmo.get_requires(&second);
    EXPECT_EQ(second.m_quad_requires.front().m_material.m_texture.m_revision, original.m_revision);
    camera->viewMatrix = glm::lookAt(glm::vec3(4, 0, 0), glm::vec3(0), glm::vec3(0, 1, 0));
    bgui::draw_data rotated;
    gizmo.get_requires(&rotated);
    const auto changed = rotated.m_quad_requires.front().m_material.m_texture;
    EXPECT_GT(changed.m_revision, original.m_revision);
    EXPECT_NE(changed.m_buffer, original.m_buffer);
    image.set_final_rect(0, 0, 70, 60);
    gizmo.on_update();
    bgui::draw_data resized;
    gizmo.get_requires(&resized);
    EXPECT_EQ(resized.m_quad_requires.front().m_material.m_texture.m_buffer.size(), 36u * 36u * 4u);
}

TEST_F(EditorInteractionTest, CameraInspectorUsesOneCubemapImageAndSupportsUndo) {
    auto entity = registry->create(); registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->skybox_image = "assets/original-cross.png";
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context); panel.build(dock); panel.update();
    auto* image = find_element<bgui::inputbox>(dock, [](auto& field) {
        return field.get_buffer() == "assets/original-cross.png";
    });
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(find_element<bgui::text>(dock, [](auto& text) {
        return text.get_buffer() == "Right (+X)";
    }), nullptr);
    image->m_enter_func("assets/new-cross.png");
    EXPECT_EQ(camera->skybox_image, "assets/new-cross.png");
    ASSERT_TRUE(context->undo()); EXPECT_EQ(camera->skybox_image, "assets/original-cross.png");
    ASSERT_TRUE(context->redo()); EXPECT_EQ(camera->skybox_image, "assets/new-cross.png");
    registry->entities.clear();
}

namespace {
class helper_test_panel final : public editor::ui::panel {
public:
    std::vector<std::pair<std::function<void()>, std::function<void()>>> edits;
    void build(bgui::dock&) override {}
    void update() override {}
protected:
    void record_edit(std::function<void()> undo, std::function<void()> redo) override {
        edits.emplace_back(std::move(undo), std::move(redo));
    }
};
}

TEST_F(EditorInteractionTest, PanelFloatBindingValidatesInputAndRecordsHistory) {
    helper_test_panel panel;
    bgui::linear root(bgui::orientation::vertical);
    float value = 2.f;
    panel.add_float(root, "Value", &value);
    auto* field = find_element<bgui::field>(root, [](const auto&) { return true; });
    ASSERT_NE(field, nullptr);
    for (const char* invalid : {"nan", "inf", "1junk", "", "1e100"}) {
        field->get_inputbox().m_enter_func(invalid);
        EXPECT_EQ(value, 2.f);
    }
    EXPECT_TRUE(panel.edits.empty());
    field->get_inputbox().m_enter_func("3.5");
    EXPECT_EQ(value, 3.5f);
    ASSERT_EQ(panel.edits.size(), 1u);
    panel.edits.back().first();
    EXPECT_EQ(value, 2.f);
    panel.edits.back().second();
    EXPECT_EQ(value, 3.5f);
}

TEST_F(EditorInteractionTest, PanelVectorBindingEditsOnlySelectedAxis) {
    helper_test_panel panel;
    bgui::linear root(bgui::orientation::vertical);
    bgui::vec3 value{1.f, 2.f, 3.f};
    panel.add_vec_field(root, "Vector", &value);
    auto* input = find_element<bgui::inputbox>(root, [](const auto&) { return true; });
    ASSERT_NE(input, nullptr);
    input->m_enter_func("4");
    EXPECT_EQ(value, (bgui::vec3{4.f, 2.f, 3.f}));
    ASSERT_EQ(panel.edits.size(), 1u);
    panel.edits.back().first();
    EXPECT_EQ(value, (bgui::vec3{1.f, 2.f, 3.f}));
}

TEST_F(EditorInteractionTest, PanelBindingsRejectNullStorage) {
    helper_test_panel panel;
    bgui::linear root(bgui::orientation::vertical);
    EXPECT_THROW(panel.add_float(root, "Value", static_cast<float*>(nullptr)), std::invalid_argument);
    EXPECT_THROW(panel.add_vec_field(root, "Vector", static_cast<bgui::vec3*>(nullptr)), std::invalid_argument);
    EXPECT_THROW(panel.add_color(root, static_cast<bgui::color*>(nullptr)), std::invalid_argument);
}

TEST_F(EditorInteractionTest, PanelHistoryReadsCurrentBoundValue) {
    helper_test_panel panel;
    bgui::linear root(bgui::orientation::vertical);
    float value = 2.f;
    panel.add_float(root, "Value", &value);
    auto* field = find_element<bgui::field>(root, [](const auto&) { return true; });
    ASSERT_NE(field, nullptr);
    value = 7.f;
    field->get_inputbox().m_enter_func("9");
    ASSERT_EQ(panel.edits.size(), 1u);
    panel.edits.back().first();
    EXPECT_EQ(value, 7.f);
}


TEST_F(EditorInteractionTest, EditorCameraInspectorExposesEnvironmentWithoutComponentManagement) {
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    context->editor_camera_entity = entity.id;
    context->selected_entity = entity.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context);
    panel.build(dock);
    panel.update();
    EXPECT_NE(find_element<bgui::text>(dock, [](auto& text) {
        return text.get_buffer() == "Editor Camera";
    }), nullptr);
    EXPECT_EQ(find_element<bgui::button>(dock, [](auto& button) {
        return button.get_label().get_buffer() == "Add Component";
    }), nullptr);
    auto* section = find_element<bgui::tree>(dock, [](auto& tree) {
        return tree.label().get_buffer() == "Camera";
    });
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(find_element<bgui::button>(section->header(), [](auto& button) { return button.has_class("component-remove-button"); }), nullptr);
    auto* fov = find_element<bgui::field>(dock, [](auto& field) {
        return field.get_label().get_buffer() == "Field of View";
    });
    ASSERT_NE(fov, nullptr);
    fov->get_inputbox().m_enter_func("200");
    EXPECT_FLOAT_EQ(camera->fov, 179.f);
    auto* atmosphere = find_element<bgui::field>(dock, [](auto& field) {
        return field.get_label().get_buffer() == "Atmosphere Zenith R";
    });
    ASSERT_NE(atmosphere, nullptr);
    atmosphere->get_inputbox().m_enter_func("0.4");
    EXPECT_FLOAT_EQ(camera->atmosphere_zenith.x, 0.4f);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(camera->atmosphere_zenith.x, 0.08f);
    auto* image = find_element<bgui::inputbox>(dock, [](auto& field) {
        return field.get_buffer().empty();
    });
    ASSERT_NE(image, nullptr);
    image->m_enter_func("assets/editor-sky.png");
    EXPECT_EQ(camera->skybox_image, "assets/editor-sky.png");
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(camera->skybox_image.empty());
    registry->entities.clear();
}
