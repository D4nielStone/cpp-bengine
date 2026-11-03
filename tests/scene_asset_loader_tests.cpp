#include <os/asset_manager.hpp>
#include <os/resource_files.hpp>
#include "resources/scene_asset_loader.hpp"
#include "project_persistence_system.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <thread>

namespace {
class SceneAssets : public testing::Test {
protected:
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("bengine-async-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "triangle.obj";
    void SetUp() override {
        std::filesystem::create_directories(directory);
        std::ofstream(path) << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    void TearDown() override { std::filesystem::remove_all(directory); }
};
TEST_F(SceneAssets, CpuImportNeedsNoContextAndDeduplicatesRequests) {
    commons::scene_asset_loader loader;
    const auto owner = std::this_thread::get_id();
    float previous = 0;
    loader.prepare({path.string(), path.string()}, [&](const auto& status) {
        EXPECT_EQ(std::this_thread::get_id(), owner);
        EXPECT_GE(status.fraction, previous);
        EXPECT_LT(status.fraction, 1);
        previous = status.fraction;
        return true;
    });
    EXPECT_EQ(loader.status().total, 1u);
    EXPECT_EQ(loader.status().imported, 1u);
    auto first = loader.instantiate(path.string());
    auto second = loader.instantiate(path.string());
    ASSERT_EQ(first->meshes.size(), 1u);
    EXPECT_FALSE(first->meshes[0].isLoaded());
    auto vertices = first->meshes[0].getVertices();
    vertices[0].position.x = 42;
    first->meshes[0].set_vertices(vertices);
    EXPECT_NE(second->meshes[0].vertices()[0].position.x, 42);
    const auto original_indices = second->meshes[0].indices();
    first->meshes[0].set_indices({2, 1, 0});
    EXPECT_EQ(second->meshes[0].indices(), original_indices);
}
TEST_F(SceneAssets, MissingAssetFailsWithoutPublishingReadyState) {
    commons::scene_asset_loader loader;
    EXPECT_THROW(loader.prepare({(directory / "missing.obj").string()}, [](const auto&) { return true; }), std::runtime_error);
    EXPECT_EQ(loader.status().imported, 0u);
    EXPECT_EQ(loader.status().state, commons::scene_asset_loader::phase::failed);
    EXPECT_FALSE(loader.status().error.empty());
    EXPECT_LT(loader.status().fraction, 1);
}
TEST_F(SceneAssets, CancellationStopsBeforeImportAndCanRetry) {
    commons::scene_asset_loader loader;
    EXPECT_THROW(loader.prepare({path.string()}, [](const auto&) { return false; }), std::runtime_error);
    EXPECT_EQ(loader.status().imported, 0u);
    EXPECT_NO_THROW(loader.prepare({path.string()}, [](const auto&) { return true; }));
}
TEST_F(SceneAssets, CancelAfterWorkerCompletesDoesNotPublishItsModel) {
    commons::scene_asset_loader loader;
    int calls = 0;
    EXPECT_THROW(loader.prepare({path.string()}, [&](const auto&) { return ++calls == 1; }), std::runtime_error);
    EXPECT_EQ(loader.status().imported, 0u);
}
TEST_F(SceneAssets, FailureInPreparationOrUploadPreservesActiveEcs) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create(17);
    registry->add<commons::camera>(camera);
    registry->create(23);
    editor::ui::project_persistence_system project(registry, 17);
    const auto file = (directory / "project.bengine").string();
    std::string error;
    ASSERT_TRUE(project.save(file, error)) << error;
    auto transform = registry->get<commons::transform>(23);
    project.set_asset_loading_hooks({[](const auto&) { throw std::runtime_error("cancel prepare"); }, {}, {}});
    EXPECT_FALSE(project.load(file, error));
    EXPECT_EQ(error, "cancel prepare");
    EXPECT_EQ(registry->get<commons::transform>(23), transform);
    project.set_asset_loading_hooks({{}, {}, [](const auto&) { throw std::runtime_error("upload failed"); }});
    EXPECT_FALSE(project.load(file, error));
    EXPECT_EQ(error, "upload failed");
    EXPECT_EQ(registry->get<commons::transform>(23), transform);
}
}

#include "opengl_test_support.hpp"
#include "os/window.hpp"
#include <bgui_backend_gl3.hpp>
#include <glad.h>
TEST_F(SceneAssets, UploadRunsOnOwnerAndPreservesBindingsAndExternalTexturePaths) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Async assets test", 100, 100);
    bgui::set_up_gl3();
    struct shutdown { ~shutdown() { bgui::shutdown_gl3(); } } shutdown;
    const auto image = directory / "pixel.png";
    // 1x1 RGBA PNG.
    const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,0,0,0,0,73,69,78,68,174,66,96,130};
    std::ofstream(image, std::ios::binary).write(reinterpret_cast<const char*>(png), sizeof(png));
    commons::scene_asset_loader loader;
    loader.prepare({path.string()}, [](const auto&) { return true; });
    auto model = loader.instantiate(path.string());
    auto material = model->meshes[0].getMaterial();
    commons::texture texture; texture.path = image.string();
    material.set_texture("tex_albedo", texture);
    model->meshes[0].setMaterial(material);
    auto entity = window.get_ecs()->create();
    window.get_ecs()->add<commons::renderer>(entity, model.release());
    auto duplicate = window.get_ecs()->create();
    auto duplicate_model = window.get_ecs()->get<commons::renderer>(entity.id)->m_modelo->clone();
    window.get_ecs()->add<commons::renderer>(duplicate, duplicate_model.release());
    GLuint vao, buffer, bound_texture;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glGenTextures(1, &bound_texture); glBindTexture(GL_TEXTURE_2D, bound_texture);
    const auto owner = std::this_thread::get_id();
    loader.finish(window.get_ecs(), [&](const auto&) { EXPECT_EQ(std::this_thread::get_id(), owner); return true; });
    GLint actual;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &actual); EXPECT_EQ(GLuint(actual), vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &actual); EXPECT_EQ(GLuint(actual), buffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual); EXPECT_EQ(GLuint(actual), bound_texture);
    auto renderer = window.get_ecs()->get<commons::renderer>(entity.id);
    EXPECT_TRUE(renderer->m_modelo->meshes[0].isLoaded());
    const auto uploaded = renderer->m_modelo->meshes[0].getMaterial().textures.at("tex_albedo");
    EXPECT_NE(uploaded.id, 0u);
    const auto duplicate_renderer = window.get_ecs()->get<commons::renderer>(duplicate.id);
    EXPECT_EQ(duplicate_renderer->m_modelo->meshes[0].getMaterial().textures.at("tex_albedo").id, uploaded.id);
    EXPECT_TRUE(duplicate_renderer->m_modelo->meshes[0].isLoaded());
    EXPECT_FALSE(uploaded.embedded);
    EXPECT_EQ(uploaded.path, image.string());
    EXPECT_EQ(loader.status().fraction, 1.f);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &buffer); glDeleteTextures(1, &bound_texture);
}

#include "scene_loading_modal.hpp"
#include "systems/ui_system.hpp"
TEST_F(SceneAssets, ModalLoadsSavedSceneAndClosingWindowCancelsTransaction) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Scene load modal test", 240, 160);
    auto ui = window.add<commons::ui_system>();
    auto registry = window.get_ecs();
    auto camera = registry->create(17);
    registry->add<commons::camera>(camera);
    auto entity = registry->create(23);
    auto prepared = commons::model::prepare(path.string());
    registry->add<commons::renderer>(entity, prepared.release());
    editor::ui::project_persistence_system project(registry, 17);
    const auto file = (directory / "project.bengine").string();
    std::string error;
    ASSERT_TRUE(project.save(file, error)) << error;
    ASSERT_TRUE(editor::ui::load_project_with_modal(project, file, error)) << error;
    auto renderer = registry->get<commons::renderer>(23);
    ASSERT_NE(renderer, nullptr);
    EXPECT_TRUE(renderer->m_modelo->meshes[0].isLoaded());
    std::filesystem::rename(path, directory / "saved.obj");
    EXPECT_FALSE(editor::ui::load_project_with_modal(project, file, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(registry->get<commons::renderer>(23), renderer);
    std::filesystem::rename(directory / "saved.obj", path);
    window.close();
    EXPECT_FALSE(editor::ui::load_project_with_modal(project, file, error));
    EXPECT_NE(error.find("cancelled"), std::string::npos);
    EXPECT_EQ(registry->get<commons::renderer>(23), renderer);
    window.reopen();
}

TEST_F(SceneAssets, DeferredLoadStartsAfterUiCallbackReturns) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Deferred loading test", 100, 100);
    auto ui = window.add<commons::ui_system>();
    bool inside_callback = false, executed = false;
    bgui::add_function([&] {
        inside_callback = true;
        window.defer([&] {
            EXPECT_FALSE(inside_callback);
            executed = true;
            window.close();
        });
        inside_callback = false;
    });
    window.loop();
    EXPECT_TRUE(executed);
}

#include "systems/render_system.hpp"
#include <glm/gtc/matrix_transform.hpp>
TEST_F(SceneAssets, ColdAsyncImportRendersAfterLoaderIsDestroyed) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Cold async import test", 100, 100);
    bgui::set_up_gl3();
    struct shutdown { ~shutdown() { bgui::shutdown_gl3(); } } shutdown;
    commons::desload_shaders();
    {
        commons::scene_asset_loader loader;
        loader.prepare({path.string()}, [](const auto&) { return true; });
        EXPECT_TRUE(commons::shaders.empty()) << "CPU import must not access the GL shader cache";
        auto model = loader.instantiate(path.string());
        EXPECT_TRUE(glIsProgram(model->getShader().ID));
        auto material = model->meshes[0].getMaterial();
        material.set_uniform("material.albedo", commons::color{1.f, .05f, .05f, 1.f});
        model->meshes[0].setMaterial(material);
        auto entity = window.get_ecs()->create();
        window.get_ecs()->add<commons::renderer>(entity, model.release());
        loader.finish(window.get_ecs(), [](const auto&) { return true; });
    }
    commons::camera view;
    view.position = {.25f, .25f, 2.f};
    view.viewMatrix = glm::lookAt(glm::vec3{.25f, .25f, 2.f}, glm::vec3{.25f, .25f, 0.f}, glm::vec3{0, 1, 0});
    view.request_framebuffer_size({64, 64});
    commons::render_system rendering;
    rendering.render_view(window.get_ecs(), view);
    EXPECT_EQ(rendering.last_rendered_entity_count(), 1u);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, view.fbo);
    unsigned char pixel[4]{};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    EXPECT_GT(pixel[0], pixel[1] + 20);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

#include "loaders/material_asset.hpp"
TEST_F(SceneAssets, MaterialWorkerPreservesTextureReferencesAndCachesRepeatedFiles) {
    commons::material source;
    commons::texture texture;
    texture.embedded = std::make_shared<bgui::texture>();
    texture.embedded->m_size = {2, 1};
    texture.embedded->m_buffer = {255, 0, 0, 255, 0, 255, 0, 255};
    source.set_texture("tex_albedo", texture);
    std::filesystem::path file;
    std::string error;
    ASSERT_TRUE(commons::store_material_asset(directory, source, file, error)) << error;
    commons::scene_asset_loader loader;
    loader.prepare({}, [](const auto&) { return true; });
    const auto owner = std::this_thread::get_id();
    const auto pump = [&](const auto&) { EXPECT_EQ(std::this_thread::get_id(), owner); return true; };
    const auto first = loader.load_material(file, pump);
    const auto image_path = first.textures.at("tex_albedo").path;
    EXPECT_EQ(bgui::decode_texture_memory(bgui::resource_files::read(image_path), image_path).m_buffer,
        texture.embedded->m_buffer);
    std::filesystem::remove(file);
    const auto repeated = loader.load_material(file, pump);
    EXPECT_EQ(first.textures.at("tex_albedo").path, repeated.textures.at("tex_albedo").path);
    EXPECT_THROW(loader.load_material(file, [](const auto&) { return false; }), std::runtime_error);
    EXPECT_EQ(loader.status().state, commons::scene_asset_loader::phase::cancelled);
    loader.prepare({}, pump);
    EXPECT_THROW(loader.load_material(file, pump), std::runtime_error);
    EXPECT_EQ(loader.status().state, commons::scene_asset_loader::phase::failed);
}

TEST_F(SceneAssets, PlaySnapshotSaveModalCompletesAndCancellationPreservesSavedProject) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Play snapshot test", 160, 100);
    window.add<commons::ui_system>();
    auto registry = window.get_ecs();
    auto camera = registry->create(17);
    registry->add<commons::camera>(camera);
    auto entity = registry->create(23);
    registry->add<commons::renderer>(entity, commons::model::prepare(path.string()).release());
    editor::ui::project_persistence_system project(registry, 17);
    const auto file = (directory / "snapshot.bengine").string();
    std::string error;
    ASSERT_TRUE(editor::ui::save_project_with_modal(project, file, error)) << error;
    ASSERT_TRUE(editor::ui::load_project_with_modal(project, file, error)) << error;
    window.close();
    EXPECT_FALSE(editor::ui::save_project_with_modal(project, file, error));
    EXPECT_NE(error.find("cancelled"), std::string::npos);
    EXPECT_TRUE(project.save(file, error)) << "Save hooks must be cleared after cancellation: " << error;
    window.reopen();
    EXPECT_TRUE(editor::ui::load_project_with_modal(project, file, error)) << error;
}

TEST_F(SceneAssets, LargeReferencedTextureBindsAndPreservesReloads) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 display unavailable";
    commons::window window("Large texture test", 100, 100);
    bgui::set_up_gl3();
    struct shutdown { ~shutdown() { bgui::shutdown_gl3(); } } shutdown;
    commons::material source;
    commons::texture texture;
    texture.embedded = std::make_shared<bgui::texture>();
    texture.embedded->m_size = {2048, 2048};
    texture.embedded->m_buffer.resize(2048 * 2048 * 4, 255);
    source.set_texture("tex_albedo", texture);
    std::filesystem::path file;
    std::string error;
    ASSERT_TRUE(commons::store_material_asset(directory, source, file, error)) << error;
    commons::material loaded;
    ASSERT_TRUE(commons::load_material_asset(file, loaded, error)) << error;
    auto tex = loaded.textures.at("tex_albedo");
    EXPECT_FALSE(tex.path.empty());
    tex.bind(0);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) tex.bind(0);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "20 warm binds of 2048x2048 texture: " << elapsed << " ms\n";
    const auto original_path = tex.path;
    source.textures.at("tex_albedo").embedded->m_buffer[0] = 0;
    std::filesystem::path replacement;
    ASSERT_TRUE(commons::store_material_asset(directory, source, replacement, error)) << error;
    std::filesystem::copy_file(replacement, file, std::filesystem::copy_options::overwrite_existing);
    ASSERT_TRUE(commons::load_material_asset(file, loaded, error)) << error;
    tex = loaded.textures.at("tex_albedo");
    EXPECT_NE(tex.path, original_path);
    tex.bind(0);
    std::vector<unsigned char> pixels(2048 * 2048 * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    EXPECT_EQ(pixels[0], 0);
    EXPECT_EQ(pixels[1], 255);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}
