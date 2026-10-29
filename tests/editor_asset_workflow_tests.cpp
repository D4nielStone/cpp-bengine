#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <gtest/gtest.h>
#include "files_view.hpp"
#include "scene_asset_drop.hpp"
#include "scene_asset_paths.hpp"
#include "asset_drop_overlay.hpp"
#include "entities_view.hpp"
#include "entity_picking_system.hpp"
#include "undo_redo_shortcuts_system.hpp"

namespace {
template<class T, class Predicate>
T* find(bgui::element& element, Predicate predicate) {
    if (auto* value = dynamic_cast<T*>(&element); value && predicate(*value)) return value;
    if (auto* layout = element.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* value = find<T>(*child, predicate)) return value;
    return nullptr;
}
class EditorAssetWorkflowTest : public testing::Test {
protected:
    bgui::scoped_interface interface;
    std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("bengine-workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        std::filesystem::create_directory(folder);
        auto& io = bgui::get_context();
        io.m_input_map.clear(); io.m_char_buffer.clear();
        io.m_size = {1000, 700};
        editor::ui::asset_drag_active = false;
    }
    void TearDown() override {
        editor::ui::asset_drag_active = false;
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }
};
}

TEST_F(EditorAssetWorkflowTest, CreatesScriptAndUpdatesDirtyAndSaveThroughTheUiLoop) {
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(folder);
    files.build(dock);
    auto* name = find<bgui::inputbox>(dock, [](auto& value) { return value.has_class("new-script-name"); });
    auto* create = find<bgui::button>(dock, [](auto& value) { return value.has_class("new-script-button"); });
    ASSERT_NE(name, nullptr); ASSERT_NE(create, nullptr);
    name->set_buffer("Movement");
    create->on_released();
    bgui::on_update();
    ASSERT_TRUE(std::filesystem::exists(folder / "Movement.lua"));
    auto* window = find<bgui::window>(dock, [](auto& value) {
        return value.get_title().get_buffer() == "Movement.lua";
    });
    ASSERT_NE(window, nullptr);
    auto* input = find<bgui::inputbox>(*window, [](auto&) { return true; });
    ASSERT_NE(input, nullptr);
    const std::string content = "function update()\n    -- edited\nend\n";
    input->set_buffer(content);
    input->set_focused(true);
    bgui::on_update();
    EXPECT_EQ(window->get_title().get_buffer(), "Movement.lua*");
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::s] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map.clear();
    EXPECT_EQ(window->get_title().get_buffer(), "Movement.lua");
    std::ifstream saved(folder / "Movement.lua");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(saved), {}), content);
}

TEST_F(EditorAssetWorkflowTest, LuaFilesDeliverTheirPathWhenDragged) {
    std::ofstream(folder / "Movement.lua") << "function update() end";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    std::filesystem::path dropped;
    editor::ui::files_view files(folder, [&](const auto& path, const auto&) { dropped = path; });
    files.build(dock);
    auto* file = find<bgui::button>(dock, [](auto& value) {
        return find<bgui::text>(value, [](auto& text) { return text.get_buffer() == "Movement.lua"; });
    });
    ASSERT_NE(file, nullptr);
    file->on_pressed();
    file->set_drag({8, 2});
    EXPECT_TRUE(editor::ui::asset_drag_active);
    file->on_released();
    EXPECT_FALSE(editor::ui::asset_drag_active);
    EXPECT_EQ(dropped, folder / "Movement.lua");
}

TEST_F(EditorAssetWorkflowTest, SceneDropFindsTheVisibleEntityWithoutChangingSelection) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    camera->viewport({200, 200});
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    auto model = registry->get<commons::renderer>(entity.id)->m_modelo;
    model->meshes.emplace_back(std::vector<commons::vertex>{
        {{5, -2, -2}, {1, 0, 0}, {0, 0}},
        {{5, 2, -2}, {1, 0, 0}, {1, 0}},
        {{5, 0, 2}, {1, 0, 0}, {0, 1}}
    }, std::vector<unsigned>{0, 1, 2}, commons::material{});
    auto& image = bgui::get_layout().add<bgui::image>();
    image.set_final_rect(50, 40, 200, 200);
    editor::ui::entity_picking_system picker(camera, image, context, {});
    EXPECT_EQ(picker.pick_at({150, 140}), entity.id);
    EXPECT_FALSE(context->selected_entity);
    // Drops still hit the cursor when a framebuffer resize is pending.
    camera->viewport({800, 800});
    EXPECT_EQ(picker.pick_at({150, 140}), entity.id);
    context->locked_entities.insert(entity.id);
    EXPECT_FALSE(picker.pick_at({150, 140}));
    context->locked_entities.clear();
    context->hidden_entities.insert(entity.id);
    EXPECT_FALSE(picker.pick_at({150, 140}));
    EXPECT_FALSE(picker.pick_at({1, 1}));
    EXPECT_TRUE(editor::ui::viewport_contains_drop(&image, {150, 140}));
    image.set_enable(false);
    EXPECT_FALSE(editor::ui::viewport_contains_drop(&image, {150, 140}));
}

TEST_F(EditorAssetWorkflowTest, TextUndoDoesNotConsumeSceneHistory) {
    auto context = std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    bool undone = false;
    context->record_action([&]() { undone = true; }, [&]() { undone = false; });
    auto& input = bgui::get_layout().add<bgui::inputbox>("text");
    bgui::focus_keyboard(input);
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::z] = bgui::input_action::press;
    editor::ui::undo_redo_shortcuts_system shortcuts(context);
    shortcuts.on_update();
    EXPECT_FALSE(undone);
    EXPECT_EQ(context->undo_stack.size(), 1u);
}

TEST_F(EditorAssetWorkflowTest, ExternalFilesAreCopiedWithoutOverwritingAndAppearInBrowser) {
    const auto source = folder / "external";
    const auto project = folder / "project";
    std::filesystem::create_directories(source);
    std::filesystem::create_directories(project);
    std::ofstream(source / "Move.lua") << "external script";
    std::filesystem::create_directories(project / "assets");
    std::ofstream(project / "assets/Move.lua") << "existing script";
    editor::ui::files_view files(project);
    const auto imported = files.import_file(source / "Move.lua");
    EXPECT_EQ(imported, project / "assets/Move_1.lua");
    std::ifstream original(project / "assets/Move.lua");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(original), {}), "existing script");
    std::ifstream copied(imported);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(copied), {}), "external script");
    EXPECT_EQ(files.import_file(imported), imported);
    auto& dock = bgui::get_layout().add<bgui::dock>();
    files.build(dock);
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Move_1.lua"; }), nullptr);
    EXPECT_THROW(files.import_file(source / "missing.lua"), std::filesystem::filesystem_error);
}

namespace {
void write_test_png(const std::filesystem::path& path) {
    const unsigned char bytes[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,0,0,0,0,73,69,78,68,174,66,96,130};
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}
}

TEST_F(EditorAssetWorkflowTest, ModelImportKeepsMaterialsAndRelativeTexturesAfterSourceIsRemoved) {
    const auto source = folder / "source";
    const auto project = folder / "project";
    std::filesystem::create_directories(source / "textures");
    std::filesystem::create_directory(project);
    std::ofstream(source / "model.obj") << "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n";
    std::ofstream(source / "model.mtl") << "newmtl test\nmap_Kd textures/color.png\n";
    write_test_png(source / "textures/color.png");
    editor::ui::files_view files(project);
    const auto imported = files.import_file(source / "model.obj");
    EXPECT_TRUE(std::filesystem::exists(imported.parent_path() / "model.mtl"));
    EXPECT_TRUE(std::filesystem::exists(imported.parent_path() / "textures/color.png"));
    const auto second = files.import_file(source / "model.obj");
    EXPECT_NE(imported.parent_path(), second.parent_path());
    std::filesystem::remove_all(source);
    Assimp::Importer importer;
    const auto* scene = importer.ReadFile(imported.string(), 0);
    ASSERT_NE(scene, nullptr);
    ASSERT_EQ(scene->mNumMeshes, 1u);
    aiString texture;
    ASSERT_EQ(scene->mMaterials[scene->mMeshes[0]->mMaterialIndex]->GetTexture(aiTextureType_DIFFUSE, 0, &texture), AI_SUCCESS);
    const auto texture_path = imported.parent_path() / texture.C_Str();
    EXPECT_TRUE(std::filesystem::is_regular_file(texture_path));
    EXPECT_TRUE(editor::ui::path_inside(std::filesystem::canonical(texture_path), std::filesystem::canonical(project)));
}

TEST_F(EditorAssetWorkflowTest, InvalidModelsDoNotLeavePartialImports) {
    std::filesystem::create_directory(folder / "project");
    std::ofstream(folder / "bad.obj") << "not a model";
    editor::ui::files_view files(folder / "project");
    EXPECT_THROW(files.import_file(folder / "bad.obj"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_empty(folder / "project"));
}

TEST_F(EditorAssetWorkflowTest, ScenePlacementProjectsTheDropInsteadOfUsingTheOrigin) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    registry->get<commons::transform>(entity.id)->set_position({0, 5, 0});
    registry->get<commons::transform>(entity.id)->set_rotation({-30, 0, 0});
    camera->viewport({800, 600});
    camera->viewport_ptr = &camera->viewportFBO;
    auto& viewport = bgui::get_layout().add<bgui::image>();
    viewport.set_final_rect(100, 50, 400, 300);
    const auto center = editor::ui::scene_drop_position(*camera, viewport, {300, 200});
    const auto side = editor::ui::scene_drop_position(*camera, viewport, {400, 200});
    ASSERT_TRUE(center); ASSERT_TRUE(side);
    EXPECT_NEAR(center->y, 0.f, .001f);
    EXPECT_NEAR(side->y, 0.f, .001f);
    EXPECT_GT(std::abs(center->x), 1.f);
    EXPECT_GT(std::abs(center->z - side->z), 1.f);
    const auto projected = camera->worldParaTela(*side);
    EXPECT_NEAR(projected.x, 600, 2);
    EXPECT_NEAR(projected.y, 300, 2);
    EXPECT_FALSE(editor::ui::scene_drop_position(*camera, viewport, {99, 200}));
    EXPECT_FALSE(editor::ui::scene_drop_position(*camera, viewport, {500, 200}));
    const auto parallel = editor::ui::drop_position_on_ground({{1, 2, 3}, {1, 0, 0}});
    EXPECT_FLOAT_EQ(parallel.x, 6.f);
    EXPECT_FLOAT_EQ(parallel.y, 2.f);
    const auto upward = editor::ui::drop_position_on_ground({{1, 2, 3}, {0, 1, 0}});
    EXPECT_FLOAT_EQ(upward.y, 7.f);
}

TEST_F(EditorAssetWorkflowTest, DroppedImageChangesOnlyTargetMaterialsAndSupportsUndo) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    const auto target = registry->create();
    const auto selected = registry->create();
    context->selected_entity = selected.id;
    for (auto entity : {target, selected}) {
        registry->add<commons::renderer>(entity, new commons::model());
        auto* model = registry->get<commons::renderer>(entity.id)->m_modelo;
        model->meshes.emplace_back(std::vector<commons::vertex>{}, std::vector<unsigned>{}, commons::material{});
        model->meshes.emplace_back(std::vector<commons::vertex>{}, std::vector<unsigned>{}, commons::material{});
    }
    const auto image = folder / "color.png";
    write_test_png(image);
    std::string error;
    ASSERT_TRUE(editor::ui::apply_dropped_albedo(context, target.id, image, error)) << error;
    const auto renderer = registry->get<commons::renderer>(target.id);
    for (const auto& mesh : renderer->m_modelo->meshes) {
        EXPECT_EQ(mesh.getMaterial().getTextura("tex_albedo").path, image.string());
        EXPECT_TRUE(mesh.getMaterial().getUniforme("use_tex_albedo").valor.b);
    }
    EXPECT_TRUE(registry->get<commons::renderer>(selected.id)->m_modelo->meshes[0].getMaterial().textures.empty());
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(renderer->m_modelo->meshes[0].getMaterial().textures.empty());
    ASSERT_TRUE(context->redo());
    EXPECT_EQ(renderer->m_modelo->meshes[0].getMaterial().getTextura("tex_albedo").path, image.string());
    const auto actions = context->undo_stack.size();
    EXPECT_FALSE(editor::ui::apply_dropped_albedo(context, 0, image, error));
    context->locked_entities.insert(target.id);
    EXPECT_FALSE(editor::ui::apply_dropped_albedo(context, target.id, image, error));
    context->locked_entities.clear();
    EXPECT_FALSE(editor::ui::apply_dropped_albedo(context, target.id, folder / "missing.png", error));
    context->is_playing = true;
    EXPECT_FALSE(editor::ui::apply_dropped_albedo(context, target.id, image, error));
    EXPECT_EQ(context->undo_stack.size(), actions);
}

TEST_F(EditorAssetWorkflowTest, DropHintIsTranslucentDoesNotCaptureInputAndClearsAfterLeaving) {
    auto context = std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    bool native_hover = false;
    auto& overlay = bgui::get_layout().add<editor::ui::asset_drop_overlay, bgui::layer::overlay>(
        [&]() { return native_hover; }, context);
    overlay.on_update();
    EXPECT_FALSE(*overlay.style.visual.visible);
    overlay.compute_style();
    bgui::draw_data hidden;
    overlay.get_requires(&hidden);
    EXPECT_TRUE(hidden.m_quad_requires.empty());
    native_hover = true;
    overlay.on_update();
    EXPECT_TRUE(*overlay.style.visual.visible);
    bgui::draw_data shown;
    overlay.get_requires(&shown);
    EXPECT_FALSE(shown.m_quad_requires.empty());
    EXPECT_FALSE(overlay.receives_input());
    EXPECT_EQ(overlay.processed_rect(), (bgui::vec4i{0, 0, 1000, 700}));
    const auto alpha = overlay.style.visual.background.normal->a;
    EXPECT_GT(alpha, 0.f); EXPECT_LT(alpha, 1.f);
    EXPECT_NE(find<bgui::text>(overlay, [](auto& text) { return text.get_buffer() == "Arraste aqui"; }), nullptr);
    native_hover = false;
    overlay.on_update();
    EXPECT_FALSE(*overlay.style.visual.visible);
    editor::ui::asset_drag_active = true;
    overlay.on_update();
    EXPECT_TRUE(*overlay.style.visual.visible);
    context->is_playing = true;
    overlay.on_update();
    EXPECT_FALSE(*overlay.style.visual.visible);
}

TEST_F(EditorAssetWorkflowTest, ImportedScriptAttachesToTargetAndKeepsAProjectRelativePath) {
    std::filesystem::create_directory(folder / "project");
    const auto source = folder / "Movement.lua";
    std::ofstream(source) << "error('must not execute when dropped')\nfunction update() end\n";
    editor::ui::files_view files(folder / "project");
    const auto imported = files.import_file(source);
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->project_root = folder / "project";
    const auto target = registry->create();
    const auto selected = registry->create();
    context->selected_entity = selected.id;
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, target.id, imported, error)) << error;
    ASSERT_NE(registry->get<commons::code>(target.id), nullptr);
    EXPECT_EQ(registry->get<commons::code>(target.id)->file, "assets/Movement.lua");
    EXPECT_FALSE(registry->get<commons::code>(selected.id));
    EXPECT_TRUE(context->undo());
    EXPECT_FALSE(registry->get<commons::code>(target.id));
    EXPECT_TRUE(context->redo());
    EXPECT_TRUE(registry->get<commons::code>(target.id));
}

TEST_F(EditorAssetWorkflowTest, AnyFileCanBeMovedByDroppingItOnAFolder) {
    std::filesystem::create_directory(folder / "destination");
    std::ofstream(folder / "notes.txt") << "notes";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(folder);
    files.build(dock);
    bgui::on_update();
    auto* destination = find<bgui::tree>(dock, [](auto& tree) { return tree.label().get_buffer() == "destination"; });
    auto* source = find<bgui::button>(dock, [](auto& button) {
        return find<bgui::text>(button, [](auto& text) { return text.get_buffer() == "notes.txt"; });
    });
    ASSERT_NE(destination, nullptr); ASSERT_NE(source, nullptr);
    const auto rect = destination->header().processed_rect();
    bgui::get_context().m_mouse_position = {rect.x + rect.z / 2, rect.y + rect.w / 2};
    source->on_pressed();
    source->set_drag({8, 1});
    source->on_released();
    bgui::on_update();
    EXPECT_FALSE(std::filesystem::exists(folder / "notes.txt"));
    EXPECT_TRUE(std::filesystem::exists(folder / "destination/notes.txt"));
}

TEST_F(EditorAssetWorkflowTest, DeletingAFileRequiresAnExplicitConfirmationAndSupportsCancel) {
    const auto file = folder / "notes.txt";
    std::ofstream(file) << "preserve until confirmed";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(folder);
    files.build(dock);
    auto select = [&]() {
        auto* item = find<bgui::button>(dock, [](auto& button) {
            return find<bgui::text>(button, [](auto& text) { return text.get_buffer() == "notes.txt"; });
        });
        ASSERT_NE(item, nullptr);
        item->on_released();
        bgui::on_update();
    };
    select();
    auto* remove = find<bgui::button>(dock, [](auto& button) { return button.has_class("delete-file-button"); });
    ASSERT_NE(remove, nullptr);
    remove->on_released();
    bgui::on_update();
    EXPECT_TRUE(std::filesystem::exists(file));
    auto* cancel = find<bgui::button>(bgui::get_layout(), [](auto& button) { return button.has_class("cancel-file-delete"); });
    ASSERT_NE(cancel, nullptr);
    cancel->on_released();
    bgui::on_update();
    EXPECT_TRUE(std::filesystem::exists(file));
    remove = find<bgui::button>(dock, [](auto& button) { return button.has_class("delete-file-button"); });
    ASSERT_NE(remove, nullptr);
    remove->on_released();
    bgui::on_update();
    auto* confirm = find<bgui::button>(bgui::get_layout(), [](auto& button) { return button.has_class("modal-confirm"); });
    ASSERT_NE(confirm, nullptr);
    confirm->on_released();
    bgui::on_update();
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_EQ(find<bgui::window>(dock, [](auto& window) { return window.get_title().get_buffer() == "notes.txt"; }), nullptr);
}

TEST_F(EditorAssetWorkflowTest, MovingAnOpenDirtyDocumentPreservesItsBufferAndSavesToTheNewPath) {
    std::filesystem::create_directory(folder / "scripts");
    std::ofstream(folder / "notes.txt") << "original";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(folder);
    files.build(dock);
    auto* item = find<bgui::button>(dock, [](auto& button) {
        return find<bgui::text>(button, [](auto& text) { return text.get_buffer() == "notes.txt"; });
    });
    ASSERT_NE(item, nullptr);
    item->on_released();
    bgui::on_update();
    auto* window = find<bgui::window>(dock, [](auto& window) { return window.get_title().get_buffer() == "notes.txt"; });
    ASSERT_NE(window, nullptr);
    auto* input = find<bgui::inputbox>(*window, [](auto&) { return true; });
    ASSERT_NE(input, nullptr);
    input->set_buffer("unsaved changes");
    const auto moved = files.move_file(folder / "notes.txt", folder / "scripts");
    EXPECT_EQ(moved, folder / "scripts/notes.txt");
    EXPECT_EQ(input->get_buffer(), "unsaved changes");
    EXPECT_EQ(window->get_title().get_buffer(), "notes.txt*");
    input->set_focused(true);
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::s] = bgui::input_action::press;
    files.update();
    io.m_input_map.clear();
    EXPECT_EQ(editor::ui::project_files::read(moved), "unsaved changes");
    EXPECT_FALSE(std::filesystem::exists(folder / "notes.txt"));
}

TEST_F(EditorAssetWorkflowTest, FileMovesRejectCollisionsOutsideFoldersAndPlayMode) {
    std::filesystem::create_directory(folder / "destination");
    std::ofstream(folder / "notes.txt") << "source";
    std::ofstream(folder / "destination/notes.txt") << "destination";
    editor::ui::files_view files(folder);
    EXPECT_THROW(files.move_file(folder / "notes.txt", folder / "destination"), std::runtime_error);
    EXPECT_EQ(editor::ui::project_files::read(folder / "notes.txt"), "source");
    EXPECT_EQ(editor::ui::project_files::read(folder / "destination/notes.txt"), "destination");
    EXPECT_THROW(files.move_file(folder / "notes.txt", folder.parent_path()), std::runtime_error);
    EXPECT_EQ(files.move_file(folder / "notes.txt", folder), folder / "notes.txt");
    files.set_file_operation_guard([]() { return false; });
    EXPECT_THROW(files.move_file(folder / "notes.txt", folder / "destination"), std::runtime_error);
    std::ofstream(folder / "project.bengine") << "{}";
    EXPECT_THROW(editor::ui::asset_files::erase(folder / "project.bengine", folder), std::runtime_error);
    EXPECT_TRUE(std::filesystem::exists(folder / "project.bengine"));
}

TEST_F(EditorAssetWorkflowTest, MovingScriptAndTextureUpdatesLiveAndSavedScenePaths) {
    std::filesystem::create_directory(folder / "destination");
    std::ofstream(folder / "Move.lua") << "function update() end";
    write_test_png(folder / "color.png");
    std::ofstream(folder / "main.scene") << R"({"name":"Move.lua","components":{"code":{"directory":"Move.lua","scripts":["Move.lua"]},"renderer":{"materials":[{"maps":{"tex_albedo":"color.png"}}]}}})";
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->project_root = folder;
    auto entity = registry->create();
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, entity.id, folder / "Move.lua", error));
    registry->add<commons::renderer>(entity, new commons::model());
    auto* model = registry->get<commons::renderer>(entity.id)->m_modelo;
    commons::material material;
    material.set_texture("tex_albedo", commons::texture{0, (folder / "color.png").string()});
    model->meshes.emplace_back(std::vector<commons::vertex>{}, std::vector<unsigned>{}, material);
    editor::ui::files_view files(folder);
    files.set_file_moved_callback([context](const auto& from, const auto& to) { editor::ui::relocate_scene_asset(context, from, to); });
    const auto script = files.move_file(folder / "Move.lua", folder / "destination");
    EXPECT_EQ(registry->get<commons::code>(entity.id)->arquivoCompleto, script.string());
    EXPECT_EQ(registry->get<commons::code>(entity.id)->file, "destination/Move.lua");
    const auto image = files.move_file(folder / "color.png", folder / "destination");
    EXPECT_EQ(model->meshes[0].getMaterial().getTextura("tex_albedo").path, image.string());
    rapidjson::Document scene;
    scene.Parse(editor::ui::project_files::read(folder / "main.scene").c_str());
    ASSERT_FALSE(scene.HasParseError());
    EXPECT_STREQ(scene["name"].GetString(), "Move.lua");
    EXPECT_STREQ(scene["components"]["code"]["directory"].GetString(), "destination/Move.lua");
    EXPECT_STREQ(scene["components"]["code"]["scripts"][0].GetString(), "destination/Move.lua");
    EXPECT_STREQ(scene["components"]["renderer"]["materials"][0]["maps"]["tex_albedo"].GetString(), "destination/color.png");
}

TEST_F(EditorAssetWorkflowTest, MovingAModelCarriesItsDependenciesAndCanMoveBack) {
    std::filesystem::create_directories(folder / "source/textures");
    std::filesystem::create_directory(folder / "destination");
    const auto source = folder / "source/model.obj";
    std::ofstream(source) << "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n";
    std::ofstream(folder / "source/model.mtl") << "newmtl test\nmap_Kd textures/color.png\n";
    write_test_png(folder / "source/textures/color.png");
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->project_root = folder;
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    auto* model = registry->get<commons::renderer>(entity.id)->m_modelo;
    model->relocate_source_path(source);
    commons::material material;
    material.set_texture("tex_albedo", commons::texture{0, (folder / "source/textures/color.png").string()});
    model->meshes.emplace_back(std::vector<commons::vertex>{}, std::vector<unsigned>{}, material);
    editor::ui::files_view files(folder);
    files.set_file_moved_callback([context](const auto& from, const auto& to) { editor::ui::relocate_scene_asset(context, from, to); });
    const auto moved = files.move_file(source, folder / "destination");
    EXPECT_FALSE(std::filesystem::exists(source));
    EXPECT_TRUE(std::filesystem::exists(folder / "destination/model.mtl"));
    EXPECT_TRUE(std::filesystem::exists(folder / "destination/textures/color.png"));
    EXPECT_EQ(model->get_source_path(), moved.string());
    EXPECT_EQ(model->meshes[0].getMaterial().getTextura("tex_albedo").path, (folder / "destination/textures/color.png").string());
    EXPECT_EQ(files.move_file(moved, folder / "source"), source);
    EXPECT_TRUE(std::filesystem::exists(source));
}

TEST_F(EditorAssetWorkflowTest, MovingTheMainSceneUpdatesProjectAndMarker) {
    std::filesystem::create_directory(folder / "scenes");
    std::ofstream(folder / "main.scene") << "{\"entities\":[]}";
    std::ofstream(folder / "project.bengine") << "{\"scene\":\"main.scene\"}";
    editor::ui::files_view files(folder);
    files.move_file(folder / "main.scene", folder / "scenes");
    rapidjson::Document project;
    project.Parse(editor::ui::project_files::read(folder / "project.bengine").c_str());
    ASSERT_FALSE(project.HasParseError());
    EXPECT_STREQ(project["scene"].GetString(), "scenes/main.scene");
    EXPECT_EQ(editor::ui::project_files::read(folder / ".bengine-main-scene"), "scenes/main.scene\n");
}

TEST_F(EditorAssetWorkflowTest, FailedReferenceStagingLeavesTheFileAndBlockingDirectoryUntouched) {
    std::filesystem::create_directory(folder / "destination");
    std::ofstream(folder / "Move.lua") << "function update() end";
    std::ofstream(folder / "main.scene") << "{\"directory\":\"Move.lua\"}";
    std::filesystem::create_directory(folder / "main.scene.tmp");
    editor::ui::files_view files(folder);
    EXPECT_THROW(files.move_file(folder / "Move.lua", folder / "destination"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::exists(folder / "Move.lua"));
    EXPECT_FALSE(std::filesystem::exists(folder / "destination/Move.lua"));
    EXPECT_TRUE(std::filesystem::is_directory(folder / "main.scene.tmp"));
    EXPECT_EQ(editor::ui::project_files::read(folder / "main.scene"), "{\"directory\":\"Move.lua\"}");
}

TEST_F(EditorAssetWorkflowTest, ExternalFilesCanBeCopiedDirectlyIntoAProjectFolder) {
    std::filesystem::create_directories(folder / "project/destination");
    std::ofstream(folder / "notes.txt") << "source";
    editor::ui::files_view files(folder / "project");
    const auto imported = files.import_file(folder / "notes.txt", folder / "project/destination");
    EXPECT_EQ(imported, folder / "project/destination/notes.txt");
    EXPECT_EQ(editor::ui::project_files::read(imported), "source");
    EXPECT_TRUE(std::filesystem::exists(folder / "notes.txt"));
    EXPECT_THROW(files.import_file(folder / "notes.txt", folder), std::runtime_error);
}
