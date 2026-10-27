#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <bgui.hpp>
#include "camera_movement.hpp"
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
    io.m_input_map[bgui::input_key::space] = bgui::input_action::press;
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
