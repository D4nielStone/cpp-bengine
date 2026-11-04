#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>
#include <bgui.hpp>
#include <os/asset_manager.hpp>
#include <bgui_backend_freetype.hpp>
#include <gtest/gtest.h>
#include "components/camera.hpp"
#include "components/code.hpp"
#include "systems/code_system.hpp"
#include "systems/physics_system.hpp"
#include <tuple>
#include <cmath>
#include "components/renderer.hpp"
#include "components/directional_light.hpp"
#include "components/spot_light.hpp"
#include "systems/render_system.hpp"
#include "project_persistence_system.hpp"
#include "viewport_sync.hpp"
#include "camera_viewport.hpp"
#include "material_thumbnail.hpp"
#include "guizmo/grid_gizmo.hpp"
#include "components/physics.hpp"
#include "project_creation_system.hpp"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <array>
#include <cstring>
#include <iterator>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace {
class EditorRenderTest : public testing::Test {
protected:
    GLFWwindow* window{};
    void SetUp() override {
        if (!glfwInit())
            GTEST_SKIP() << "No display for OpenGL verification";
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(64, 64, "Editor rendering test", nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            GTEST_SKIP() << "OpenGL 3.3 unavailable";
        }
        glfwMakeContextCurrent(window);
        bgui::set_up_gl3();
    }
    void TearDown() override {
        if (window) {
            commons::desload_shaders();
            bgui::shutdown_gl3();
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }
};
}

TEST_F(EditorRenderTest, TextureResolutionDefaultsToSeventyPercentAndReadsImageOverride) {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() /
        ("bengine-texture-scale-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(folder);
    const auto path = folder / "image.png";
    fs::copy_file(fs::path(BENGINE_SOURCE_DIR) / "tests/fixtures/cubemap_cross.png", path);
    const auto& source = bgui::asset_manager::get_instance().load_texture(path.string());
    const auto expect_dimensions = [&](int percent) {
        commons::texture texture{0, path.string()};
        texture.bind(0);
        GLint width{}, height{};
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        EXPECT_EQ(width, std::max(1, int(std::lround(source.m_size[0] * percent / 100))));
        EXPECT_EQ(height, std::max(1, int(std::lround(source.m_size[1] * percent / 100))));
    };
    expect_dimensions(70);
    const auto settings = commons::texture_resolution_path(path);
    std::ofstream(settings) << "100";
    expect_dimensions(100);
    std::ofstream(settings) << "101";
    expect_dimensions(70);
    fs::remove_all(folder);
}

TEST_F(EditorRenderTest, UploadsTexturePathsAndRoundTripsPbrMaterials) {
    const auto source = std::filesystem::path(BENGINE_SOURCE_DIR);
    const auto image_path = source / "tests/fixtures/cubemap_cross.png";
    commons::texture normal{0, image_path.string()};
    normal.bind(0);
    GLint binding{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_GT(binding, 0); // A path-only texture must not bind texture zero.

    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create(17);
    registry->add<commons::camera>(camera);
    auto entity = registry->create(23);
    registry->add<commons::renderer>(entity, (source / "commons/assets/models/cube.obj").string().c_str());
    auto renderer = registry->get<commons::renderer>(entity.id);
    ASSERT_FALSE(renderer->m_modelo->meshes.empty());
    renderer->cast_shadows = false;
    renderer->receive_shadows = false;
    auto material = renderer->m_modelo->meshes[0].getMaterial();
    material.set_uniform("material.metallic", 0.73f);
    material.set_uniform("material.roughness", 0.39f);
    material.set_uniform("material.ao", 0.87f);
    material.set_uniform("uvMundo", true);
    material.set_uniform("material.uvOffsetX", -0.25f);
    material.set_uniform("material.uvOffsetY", 0.5f);
    material.set_uniform("material.uvRepeatX", 3.f);
    material.set_uniform("material.uvRepeatY", 4.f);
    material.set_texture("tex_normal", normal);
    material.set_uniform("use_tex_normal", true);
    renderer->m_modelo->meshes[0].setMaterial(material);

    auto folder = std::filesystem::temp_directory_path() /
        ("bengine-pbr-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto project = folder / "project.bengine";
    editor::ui::project_persistence_system persistence(registry, camera.id);
    std::string error;
    ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->cast_shadows);
    EXPECT_FALSE(registry->get<commons::renderer>(entity.id)->receive_shadows);
    const auto loaded = registry->get<commons::renderer>(entity.id)->m_modelo->meshes[0].getMaterial();
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.metallic").valor.f, 0.73f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.roughness").valor.f, 0.39f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.ao").valor.f, 0.87f);
    EXPECT_TRUE(loaded.getUniforme("uvMundo").valor.b);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.uvOffsetX").valor.f, -0.25f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.uvOffsetY").valor.f, 0.5f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.uvRepeatX").valor.f, 3.f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.uvRepeatY").valor.f, 4.f);
    EXPECT_EQ(loaded.getTextura("tex_normal").path, image_path.string());
    const auto enabled = loaded.getUniforme("use_tex_normal");
    EXPECT_EQ(enabled.m_type, commons::uniform::type::Bool);
    if (enabled.m_type == commons::uniform::type::Bool)
        EXPECT_TRUE(enabled.valor.b);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    const auto original_renderer=registry->get<commons::renderer>(entity.id);
    std::filesystem::path sidecar;
    for(const auto& file : std::filesystem::recursive_directory_iterator(folder))
        if(file.path().extension()==".bmat") { sidecar=file.path();break; }
    ASSERT_FALSE(sidecar.empty());
    std::ofstream(sidecar) << "{invalid";
    EXPECT_FALSE(persistence.load(project.string(),error));
    EXPECT_EQ(registry->get<commons::renderer>(entity.id),original_renderer);
    EXPECT_FLOAT_EQ(original_renderer->m_modelo->meshes[0].getMaterial().getUniforme("material.metallic").valor.f,.73f);
    std::filesystem::remove_all(folder);
}

TEST_F(EditorRenderTest, FrustumCullingSkipsEntitiesOutsideCameraView) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create(1001u);
    registry->add<commons::camera>(camera_entity);
    registry->add<commons::transform>(camera_entity,
        bgui::vec3{0.f, 0.f, 0.f},
        bgui::vec3{0.f, -90.f, 0.f},
        bgui::vec3{1.f, 1.f, 1.f});

    auto visible = registry->create(1002u);
    registry->add<commons::transform>(visible, bgui::vec3{0.f, 0.f, -3.f});
    const auto visible_model = (std::filesystem::path(BENGINE_SOURCE_DIR) / "commons/assets/models/cube.obj").string();
    registry->add<commons::renderer>(visible, visible_model.c_str());

    auto hidden = registry->create(1003u);
    registry->add<commons::transform>(hidden, bgui::vec3{0.f, 0.f, 6.f});
    registry->add<commons::renderer>(hidden, visible_model.c_str());

    registry->get<commons::camera>(camera_entity.id)->viewport({64, 64});
    commons::render_system render;
    render.update(registry);

    EXPECT_EQ(render.last_rendered_entity_count(), 1u);

    auto parent = registry->create();
    auto parent_transform = registry->get<commons::transform>(parent.id);
    parent_transform->set_position({5.f, 0.f, -3.f});
    parent_transform->set_scale({5.f, 1.f, 1.f});
    auto visible_transform = registry->get<commons::transform>(visible.id);
    visible_transform->set_position({});
    ASSERT_TRUE(visible_transform->set_parent(parent.id, false));
    render.update(registry);
    EXPECT_EQ(render.last_rendered_entity_count(), 1u);
}

TEST_F(EditorRenderTest, KeepsEmbeddedModelTexturesWhenLoadingSavedScene) {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() /
        ("bengine-embedded-texture-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct cleanup {
        fs::path path;
        ~cleanup() { fs::remove_all(path); }
    } cleanup{folder};

    aiScene scene;
    scene.mRootNode = new aiNode("root");
    scene.mRootNode->mNumMeshes = 1;
    scene.mRootNode->mMeshes = new unsigned[1]{0};
    scene.mNumMeshes = 1;
    scene.mMeshes = new aiMesh*[1]{new aiMesh};
    auto* mesh = scene.mMeshes[0];
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh->mNormals = new aiVector3D[3]{{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    mesh->mTextureCoords[0] = new aiVector3D[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh->mNumUVComponents[0] = 2;
    mesh->mNumFaces = 1;
    mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3;
    mesh->mFaces[0].mIndices = new unsigned[3]{0, 1, 2};
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    scene.mNumMaterials = 1;
    scene.mMaterials = new aiMaterial*[1]{new aiMaterial};
    aiString embedded_reference("*0");
    scene.mMaterials[0]->AddProperty(
        &embedded_reference,
        AI_MATKEY_TEXTURE_DIFFUSE(0)
    );
    const auto image_path = fs::path(BENGINE_SOURCE_DIR) /
        "external/cpp-bgui/tests/assets/rgb.jpg";
    std::ifstream image(image_path, std::ios::binary);
    const std::string image_bytes{std::istreambuf_iterator<char>(image), {}};
    ASSERT_FALSE(image_bytes.empty());
    scene.mNumTextures = 1;
    scene.mTextures = new aiTexture*[1]{new aiTexture};
    auto* embedded = scene.mTextures[0];
    embedded->mWidth = static_cast<unsigned>(image_bytes.size());
    embedded->pcData = new aiTexel[
        (image_bytes.size() + sizeof(aiTexel) - 1) / sizeof(aiTexel)
    ];
    std::memcpy(embedded->pcData, image_bytes.data(), image_bytes.size());
    std::strcpy(embedded->achFormatHint, "jpg");

    const auto model_path = folder / "embedded.glb";
    Assimp::Exporter exporter;
    ASSERT_EQ(exporter.Export(&scene, "glb2", model_path.string()), AI_SUCCESS)
        << exporter.GetErrorString();

    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create(17);
    registry->add<commons::camera>(camera);
    auto entity = registry->create(23);
    registry->add<commons::renderer>(entity, model_path.string().c_str());
    auto renderer = registry->get<commons::renderer>(entity.id);
    ASSERT_FALSE(renderer->m_modelo->meshes.empty());
    ASSERT_NE(
        renderer->m_modelo->meshes[0].getMaterial()
            .getTextura("tex_albedo").embedded,
        nullptr
    );

    EXPECT_NE(renderer->m_modelo->meshes[0].getMaterial()
        .getTextura("tex_albedo").embedded->m_revision, 0u);
    editor::ui::project_persistence_system persistence(registry, camera.id);
    const auto project_path = folder / "project.bengine";
    std::string error;
    ASSERT_TRUE(persistence.save(project_path.string(), error)) << error;
    ASSERT_TRUE(persistence.load(project_path.string(), error)) << error;

    const auto loaded_renderer = registry->get<commons::renderer>(entity.id);
    ASSERT_NE(loaded_renderer, nullptr);
    const auto loaded_texture = loaded_renderer->m_modelo->meshes[0]
        .getMaterial().getTextura("tex_albedo");
    std::ifstream saved_image(loaded_texture.path, std::ios::binary);
    ASSERT_TRUE(saved_image.is_open());
    const std::string saved_bytes{std::istreambuf_iterator<char>(saved_image), {}};
    EXPECT_EQ(saved_bytes, image_bytes);
    loaded_texture.bind(0);
    GLint binding{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
    EXPECT_GT(binding, 0);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, SmallInterfaceTextUsesAntialiasedMipmappedGlyphs) {
    bgui::set_up_freetype();
    struct cleanup { ~cleanup() { bgui::shutdown_freetype(); } } cleanup;
    auto& font = bgui::ft_load_font("Editor quality regression",
        (std::filesystem::path(COMMONS_ASSET_DIR) / "fonts/HackNerdFont-Regular.ttf").string(), 40);
    EXPECT_TRUE(std::any_of(font.atlas.m_buffer.begin(), font.atlas.m_buffer.end(),
        [](unsigned char coverage) { return coverage > 0 && coverage < 255; }))
        << "FreeType must retain partial glyph coverage";
    EXPECT_TRUE(font.atlas.m_generate_mipmap)
        << "40px glyphs are displayed at about 11px and need minification filtering";
    const auto id = bgui::gl3_get_texture(font.atlas);
    glBindTexture(GL_TEXTURE_2D, id);
    GLint level_width{};
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &level_width);
    EXPECT_GT(level_width, 0);
}

TEST_F(EditorRenderTest, NeutralNormalMapPreservesLightingOnAHorizontalSurface) {
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    commons::shader shader;
    ASSERT_NE(shader.ID, 0u);
    shader.use();
    const auto model = glm::mat4(1.f);
    const auto view = glm::lookAt(glm::vec3{0, 3, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1});
    const auto projection = glm::ortho(-1.f, 1.f, -1.f, 1.f, 0.1f, 10.f);
    shader.setMat4("model", glm::value_ptr(model));
    shader.setMat4("view", glm::value_ptr(view));
    shader.setMat4("projection", glm::value_ptr(projection));
    shader.setVec3("viewPos", {0, 3, 0});
    shader.setVec3("dirLight.direction", {0, -1, 0});
    shader.setVec3("dirLight.color", {1, 1, 1});
    shader.setVec3("dirLight.ambient", {0, 0, 0});
    shader.setFloat("dirLight.intensity", 1.f);
    shader.setInt("pointLightCount", 0);
    shader.setInt("spotLightCount", 0);

    commons::material material;
    material.set_uniform("material.albedo", commons::color{0.5f, 0.5f, 0.5f, 1.f});
    material.set_uniform("material.metallic", 0.f);
    material.set_uniform("material.roughness", 0.5f);
    material.set_uniform("material.ao", 1.f);
    commons::mesh plane({
        {{-1, 0, -1}, {0, 1, 0}, {0, 0}},
        {{1, 0, -1}, {0, 1, 0}, {1, 0}},
        {{1, 0, 1}, {0, 1, 0}, {1, 1}},
        {{-1, 0, 1}, {0, 1, 0}, {0, 1}}
    }, {0, 1, 2, 0, 2, 3}, material);
    plane.load();
    auto render = [&]() {
        glClear(GL_COLOR_BUFFER_BIT);
        plane.draw(shader);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    };
    const auto untextured = render();
    ASSERT_GT(untextured[0], 30);
    shader.setVec3("spotLights[0].position", {0, 3, 0});
    shader.setVec3("spotLights[0].direction", {0, -1, 0});
    shader.setVec3("spotLights[0].color", {1, 1, 1});
    shader.setFloat("spotLights[0].intensity", 10.f);
    shader.setFloat("spotLights[0].constant", 1.f);
    shader.setFloat("spotLights[0].linear", 0.f);
    shader.setFloat("spotLights[0].quadratic", 0.f);
    shader.setFloat("spotLights[0].innerCutoff", std::cos(glm::radians(10.f)));
    shader.setFloat("spotLights[0].outerCutoff", std::cos(glm::radians(20.f)));
    shader.setInt("spotLightCount", 1);
    const auto spot_lit = render();
    EXPECT_GT(spot_lit[0], untextured[0]);
    shader.setVec3("spotLights[0].direction", {1, 0, 0});
    EXPECT_NEAR(render()[0], untextured[0], 2);
    shader.setInt("spotLightCount", 0);
    unsigned int normal{};
    glGenTextures(1, &normal);
    glBindTexture(GL_TEXTURE_2D, normal);
    const unsigned char flat_normal[]{128, 128, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, flat_normal);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    material.set_texture("tex_normal", commons::texture{normal, ""});
    material.set_uniform("use_tex_normal", true);
    plane.setMaterial(material);
    const auto mapped = render();
    EXPECT_NEAR(mapped[0], untextured[0], 2);
    const unsigned char white[]{255, 255, 255, 255};
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, normal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    material.textures.erase("tex_normal");
    material.set_uniform("use_tex_normal", false);
    material.set_texture("tex_roughness", commons::texture{normal, ""});
    material.set_uniform("use_tex_roughness", true);
    plane.setMaterial(material);
    const auto roughness_mapped = render();
    EXPECT_NEAR(roughness_mapped[0], untextured[0], 2)
        << "A white map must preserve the editable material roughness factor";
    glDeleteTextures(1, &normal);
}

TEST(EditorViewport, UsesLatestFrameAndIgnoresEmptyLayout) {
    struct test_viewport final : editor::ui::editor_viewport {
        editor::ui::viewport_frame current{42, 32, 24};
        bgui::vec2i requested{};
        editor::ui::viewport_frame frame() const override { return current; }
        void request_size(int width, int height) override { requested = {width, height}; }
    } viewport;
    bgui::scoped_interface ui_scope;
    auto& image = bgui::get_layout().add<bgui::image>();
    editor::ui::viewport_sync_system sync(viewport, image);
    image.set_final_rect(0, 0, 0, 20);
    sync.update({});
    EXPECT_EQ(viewport.requested, (bgui::vec2i{0, 0}));
    image.set_final_rect(0, 0, 80, 60);
    sync.update({});
    EXPECT_EQ(viewport.requested, (bgui::vec2i{80, 60}));
    EXPECT_EQ(image.get_texture().m_id, 42u);
    EXPECT_EQ(image.get_texture().m_size, (bgui::vec2{32, 24}));
    viewport.current = {43, 80, 60};
    sync.update({});
    EXPECT_EQ(image.get_texture().m_id, 43u);
    EXPECT_EQ(image.get_texture().m_size, (bgui::vec2{80, 60}));
}

TEST_F(EditorRenderTest, DeferredViewportResizePreservesRenderedPixelsUntilNextCameraPass) {
    bgui::scoped_interface ui_scope;
    auto camera = std::make_shared<commons::camera>();
    camera->viewport({32, 24});
    camera->createFB();
    auto& image = bgui::get_layout().add<bgui::image>();
    editor::ui::camera_viewport viewport(camera);
    editor::ui::viewport_sync_system sync(viewport, image);
    camera->config.sky_solid_color = {1.f, 0.f, 0.f, 1.f};
    camera->drawFB();
    for (const bgui::vec2i size : {bgui::vec2i{40, 30}, {63, 47}, {29, 17}}) {
        image.set_final_rect(15, 20, size.x, size.y);
        const auto rendered_size = camera->viewportFBO;
        // UI mutation happens after the scene render. It must preserve that image.
        sync.update({});
        EXPECT_EQ(camera->viewportFBO, rendered_size);
        glBindTexture(GL_TEXTURE_2D, camera->framebuffer_texture());
        GLint storage_width{};
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &storage_width);
        EXPECT_EQ(storage_width, rendered_size.x);
        camera->drawFB();
        EXPECT_EQ(camera->viewportFBO, size);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        bgui::draw_data draw;
        image.get_requires(&draw);
        ASSERT_EQ(draw.m_quad_requires.size(), 1u);
        EXPECT_EQ(draw.m_quad_requires.front().m_material.m_texture.m_id,
                  camera->framebuffer_texture());
        std::vector<unsigned char> pixels(size.x * size.y * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glBindTexture(GL_TEXTURE_2D, camera->framebuffer_texture());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 3) {
            ASSERT_EQ(pixels[pixel], 255);
            ASSERT_EQ(pixels[pixel + 1], 0);
            ASSERT_EQ(pixels[pixel + 2], 0);
        }
    }
    const auto before = camera->viewportFBO;
    image.set_final_rect(0, 0, 71, 53);
    sync.update({});
    image.set_final_rect(0, 0, before.x, before.y);
    sync.update({});
    camera->request_framebuffer_size({0, -1});
    camera->drawFB();
    EXPECT_EQ(camera->viewportFBO, before);
    auto preview = std::make_shared<commons::camera>();
    preview->request_framebuffer_size({19, 13});
    EXPECT_FALSE(preview->flag_fb);
    preview->drawFB();
    EXPECT_TRUE(preview->flag_fb);
    EXPECT_EQ(preview->viewportFBO, (bgui::vec2i{19, 13}));
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, SpotShadowsRespectCasterReceiverAndLightControls) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    registry->get<commons::transform>(camera_entity.id)->set_position({0,6,8});
    registry->get<commons::transform>(camera_entity.id)->set_rotation({-36.8699f,-90,0});
    camera->position = {0, 6, 8};
    camera->viewMatrix = glm::lookAt(glm::vec3{0,6,8}, glm::vec3{0,0,0}, glm::vec3{0,1,0});
    camera->config.flag_orth = true;
    camera->config.scale = 5.f;
    camera->request_framebuffer_size({128,128});
    auto light_entity = registry->create();
    registry->add<commons::directional_light>(light_entity, bgui::vec3{0,-1,0}, bgui::vec3{0.02f}, bgui::vec3{0}, 1.f);
    registry->get<commons::directional_light>(light_entity.id)->shadows = false;
    registry->add<commons::spot_light>(light_entity);
    auto light = registry->get<commons::spot_light>(light_entity.id);
    light->position = {-2, 6, 0};
    light->direction = {2, -6, 0};
    light->inner_cutoff = 35.f;
    light->outer_cutoff = 45.f;
    light->intensity = 30.f;
    light->shadow_distance = 12.f;
    commons::material surface;
    surface.set_uniform("material.albedo", commons::color{0.6f,0.6f,0.6f,1.f});
    surface.set_uniform("material.metallic", 0.f);
    surface.set_uniform("material.roughness", 0.5f);
    surface.set_uniform("material.ao", 1.f);
    auto make_plane = [&]() {
        auto model = new commons::model();
        model->meshes.emplace_back(std::vector<commons::vertex>{
            {{-1,0,-1},{0,1,0},{0,0}}, {{1,0,-1},{0,1,0},{1,0}},
            {{1,0,1},{0,1,0},{1,1}}, {{-1,0,1},{0,1,0},{0,1}}},
            std::vector<unsigned int>{0,2,1,0,3,2}, surface);
        model->meshes[0].load();
        return model;
    };
    auto floor_entity = registry->create();
    registry->add<commons::renderer>(floor_entity, make_plane());
    registry->get<commons::transform>(floor_entity.id)->set_scale({5,1,5});
    auto floor = registry->get<commons::renderer>(floor_entity.id);
    auto cube_entity = registry->create();
    registry->get<commons::transform>(cube_entity.id)->set_position({0,2,0});
    registry->add<commons::renderer>(cube_entity, make_plane());
    auto cube = registry->get<commons::renderer>(cube_entity.id);
    commons::render_system system;
    auto render = [&](int width = 128, int height = 128) {
        system.update(registry);
        std::vector<unsigned char> pixels(width * height * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
        return pixels;
    };
    const auto darkened = [](const auto& a, const auto& b, int threshold = 10) {
        int count = 0;
        for (size_t i = 0; i < a.size(); i += 3)
            if (int(b[i]) - int(a[i]) > threshold) ++count;
        return count;
    };
    const auto shadowed = render();
    light->shadow_bias = 0.1f;
    const auto high_bias_shadowed = render();
    light->shadow_bias = 0.001f;
    light->shadows = false;
    const auto lit = render();
    EXPECT_GT(darkened(shadowed, lit), 20);
    EXPECT_GT(darkened(high_bias_shadowed, lit), 20);
    EXPECT_LT(darkened(shadowed, high_bias_shadowed), 10);
    light->shadows = true;
    floor->receive_shadows = false;
    EXPECT_LT(darkened(render(), lit), 10);
    floor->receive_shadows = true;
    cube->cast_shadows = false;
    EXPECT_LT(darkened(render(), lit), 10);
    cube->cast_shadows = true;
    light->shadow_strength = 0.f;
    EXPECT_EQ(render(), lit);
    light->shadow_strength = 1.f;
    light->shadow_distance = 1.f;
    EXPECT_EQ(render(), lit);
    light->shadow_distance = 12.f;
    cube->visible = false;
    const auto no_caster = render();
    light->shadows = false;
    EXPECT_EQ(render(), no_caster);
    cube->visible = true;
    // Moving a light to another atlas tile must preserve its shadow.
    auto second_entity = registry->create();
    registry->add<commons::spot_light>(second_entity);
    auto second = registry->get<commons::spot_light>(second_entity.id);
    second->position = light->position;
    second->direction = light->direction;
    second->inner_cutoff = light->inner_cutoff;
    second->outer_cutoff = light->outer_cutoff;
    second->intensity = light->intensity;
    second->shadow_distance = light->shadow_distance;
    light->intensity = 0.f;
    second->shadows = true;
    EXPECT_EQ(render(), shadowed);
    second->shadows = false;
    EXPECT_EQ(render(), lit);
    // Concurrent lights have independent shadow switches.
    light->intensity = 30.f;
    light->shadows = true;
    second->shadows = true;
    const auto both_shadowed = render();
    second->shadows = false;
    EXPECT_GT(darkened(both_shadowed, render()), 20);
    registry->remove(second_entity.id);
    EXPECT_EQ(render(), shadowed);
    light->position = {0, 6, 0};
    light->direction = {0, -1, 0};
    render();
    light->direction = {0, 0, 0};
    render();

    // Camera-space occlusion uses visible geometry even without a spotlight or shadow receivers.
    registry->remove(light_entity.id);
    floor->receive_shadows = false;
    cube->cast_shadows = false;
    registry->get<commons::transform>(cube_entity.id)->set_position({0, 0.25f, 0});
    auto& post = camera->config.post_processing;
    using mode = commons::ambient_occlusion_mode;
    for (bool orthographic : {true, false}) {
        camera->config.flag_orth = orthographic;
        camera->request_framebuffer_size(orthographic ? bgui::vec2i{160, 96} : bgui::vec2i{144, 96});
        const int width = orthographic ? 160 : 144;
        post.occlusion = mode::off;
        const auto unoccluded = render(width, 96);
        std::vector<unsigned char> previous;
        for (auto effect : {mode::ssao, mode::hbao, mode::gtao}) {
            post.occlusion = effect;
            const auto occluded = render(width, 96);
            EXPECT_GT(darkened(occluded, unoccluded, 0), 4) << orthographic << ' ' << int(effect);
            EXPECT_NE(occluded, previous);
            previous = occluded;
            post.strength = 0.5f;
            const auto partial = render(width, 96);
            EXPECT_GT(darkened(occluded, partial, 0), 4);
            post.strength = 0.f;
            EXPECT_EQ(render(width, 96), unoccluded);
            post.strength = 1.f;
            post.radius = 0.05f;
            EXPECT_EQ(render(width, 96), unoccluded);
            post.radius = 0.75f;
        }
    }
    cube->visible = false;
    const auto isolated_plane = render(144, 96);
    post.occlusion = mode::off;
    EXPECT_EQ(render(144, 96), isolated_plane);

    // The same pass also works with the default framebuffer and an offset viewport.
    camera->deleteFB();
    camera->viewport({56, 56});
    glViewport(4, 4, 56, 56);
    cube->visible = true;
    const auto direct = render(64, 64);
    post.occlusion = mode::gtao;
    EXPECT_GT(darkened(render(64, 64), direct, 0), 4);
}

TEST(ShadowSettings, SpotSettingsValidateAndDefaultForLegacyScenes) {
    commons::spot_light light;
    rapidjson::Document data(rapidjson::kObjectType);
    ASSERT_TRUE(light.serialize(data, data.GetAllocator()));
    for (const auto* key : {"shadows", "shadow_distance", "shadow_bias", "shadow_strength"}) {
        rapidjson::Value saved;
        saved.CopyFrom(data[key], data.GetAllocator());
        data[key].SetString("invalid");
        EXPECT_FALSE(light.analyze(data)) << key;
        data[key].CopyFrom(saved, data.GetAllocator());
    }
    data["shadow_distance"].SetFloat(0.f);
    EXPECT_FALSE(light.analyze(data));
    data["shadow_distance"].SetFloat(50.f);
    data["shadow_strength"].SetFloat(2.f);
    EXPECT_FALSE(light.analyze(data));
    data["shadow_strength"].SetFloat(1.f);
    data["shadow_bias"].SetFloat(-1.f);
    EXPECT_FALSE(light.analyze(data));
    data["shadow_bias"].SetFloat(0.001f);
    for (const auto* key : {"shadows", "shadow_distance", "shadow_bias", "shadow_strength"})
        data.RemoveMember(key);
    light.shadows = false;
    light.shadow_distance = 75.f;
    ASSERT_TRUE(light.analyze(data));
    EXPECT_TRUE(light.shadows);
    EXPECT_FLOAT_EQ(light.shadow_distance, 50.f);
    EXPECT_FLOAT_EQ(light.shadow_bias, 0.001f);
    EXPECT_FLOAT_EQ(light.shadow_strength, 1.f);
}

TEST_F(EditorRenderTest, DirectionalShadowsRespectCasterReceiverAndLightControls) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    registry->get<commons::transform>(camera_entity.id)->set_position({0,6,8});
    registry->get<commons::transform>(camera_entity.id)->set_rotation({-36.8699f,-90,0});
    camera->position = {0, 6, 8};
    camera->viewMatrix = glm::lookAt(glm::vec3{0,6,8}, glm::vec3{0,0,0}, glm::vec3{0,1,0});
    camera->config.flag_orth = true;
    camera->config.scale = 5.f;
    camera->request_framebuffer_size({128,128});
    auto light_entity = registry->create();
    registry->add<commons::directional_light>(light_entity, bgui::vec3{1,-2,0}, bgui::vec3{0.1f}, bgui::vec3{1}, 1.f);
    auto light = registry->get<commons::directional_light>(light_entity.id);
    light->shadow_distance = 12.f;
    commons::material surface;
    surface.set_uniform("material.albedo", commons::color{0.6f,0.6f,0.6f,1.f});
    surface.set_uniform("material.metallic", 0.f);
    surface.set_uniform("material.roughness", 0.5f);
    surface.set_uniform("material.ao", 1.f);
    auto make_plane = [&]() {
        auto model = new commons::model();
        model->meshes.emplace_back(std::vector<commons::vertex>{
            {{-1,0,-1},{0,1,0},{0,0}}, {{1,0,-1},{0,1,0},{1,0}},
            {{1,0,1},{0,1,0},{1,1}}, {{-1,0,1},{0,1,0},{0,1}}},
            std::vector<unsigned int>{0,2,1,0,3,2}, surface);
        model->meshes[0].load();
        return model;
    };
    auto floor_entity = registry->create();
    registry->add<commons::renderer>(floor_entity, make_plane());
    registry->get<commons::transform>(floor_entity.id)->set_scale({5,1,5});
    auto floor = registry->get<commons::renderer>(floor_entity.id);
    auto cube_entity = registry->create();
    registry->get<commons::transform>(cube_entity.id)->set_position({0,2,0});
    registry->add<commons::renderer>(cube_entity, make_plane());
    auto cube = registry->get<commons::renderer>(cube_entity.id);
    commons::render_system system;
    auto render = [&]() {
        system.update(registry);
        std::vector<unsigned char> pixels(128*128*3);
        glReadPixels(0,0,128,128,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
        return pixels;
    };
    auto shadowed = render();
    light->shadows = false;
    auto lit = render();
    auto darkened = [](const auto& a, const auto& b) {
        int count = 0;
        for (size_t i = 0; i < a.size(); i += 3)
            if (int(b[i]) - int(a[i]) > 10) ++count;
        return count;
    };
    light->shadows = false;
    cube->visible = false;
    const auto unspotted = render();
    auto spot_entity = registry->create();
    registry->add<commons::spot_light>(spot_entity);
    auto spot = registry->get<commons::spot_light>(spot_entity.id);
    spot->position = {0, 4, 0};
    spot->direction = {0, -1, 0};
    spot->intensity = 30.f;
    EXPECT_GT(darkened(unspotted, render()), 20);
    spot->direction = {1, 0, 0};
    EXPECT_LT(darkened(unspotted, render()), 10);
    registry->remove(spot_entity.id);
    cube->visible = true;
    light->shadows = true;
    EXPECT_GT(darkened(shadowed, lit), 20);
    auto previous_shadow = shadowed;
    for (const int resolution : {128, 1024, 2048}) {
        light->shadow_resolution = resolution;
        light->shadows = true;
        const auto resized_shadow = render();
        EXPECT_GT(darkened(resized_shadow, lit), 20) << resolution;
        EXPECT_NE(resized_shadow, previous_shadow) << resolution;
        previous_shadow = resized_shadow;
    }
    light->shadows = true;
    floor->receive_shadows = false;
    EXPECT_LT(darkened(render(), lit), 10);
    floor->receive_shadows = true;
    cube->cast_shadows = false;
    EXPECT_LT(darkened(render(), lit), 10);
    cube->cast_shadows = true;
    light->shadow_strength = 0;
    EXPECT_EQ(render(), lit);
    light->shadow_strength = 1;
    registry->add<commons::directional_light>(camera_entity, bgui::vec3{1,-2,0}, bgui::vec3{0.1f}, bgui::vec3{1}, 1.f);
    registry->get<commons::directional_light>(camera_entity.id)->shadows = false;
    render();
    GLint enabled = 1;
    const auto program = floor->m_modelo->getShader().ID;
    glGetUniformiv(program, glGetUniformLocation(program, "shadowEnabled"), &enabled);
    EXPECT_EQ(enabled, 0); // The camera's light overrides the global light.
    registry->remove<commons::directional_light>(camera_entity.id);
    cube->visible = false;
    auto hidden_shadowed = render();
    light->shadows = false;
    EXPECT_EQ(hidden_shadowed, render());
    light->shadows = true;
    // An isolated plane must not shadow itself, even at grazing light angles.
    light->direction = {1,-0.2f,0};
    registry->get<commons::transform>(cube_entity.id)->set_position({0,0.5f,0});
    light->shadow_bias = 0.000001f;
    for (const int resolution : {128, 512, 2048}) {
        light->shadow_resolution = resolution;
        light->shadows = true;
        auto grazing_shadowed = render();
        light->shadows = false;
        const auto grazing_lit = render();
        EXPECT_LT(darkened(grazing_shadowed, grazing_lit), 10) << resolution;
        cube->visible = true;
        light->shadows = true;
        EXPECT_GT(darkened(render(), grazing_lit), 20) << resolution;
        cube->visible = false;
    }
    light->shadows = true;
    cube->visible = true;
    registry->get<commons::transform>(cube_entity.id)->set_position({0,2,0});
    light->direction = {0,-1,0}; // Stable up vector for vertical lights.
    render();
    light->direction = {0,0,0}; // Invalid directions safely disable the pass.
    render();
}


TEST_F(EditorRenderTest, ColliderOverlayDrawsWireframeAndRestoresOpenGLState) {
    auto registry = std::make_shared<commons::ecs>();
    auto eye = registry->create(1); registry->add<commons::camera>(eye);
    auto camera = registry->get<commons::camera>(1);
    registry->get<commons::transform>(1)->set_position({0,0,4});
    registry->get<commons::transform>(1)->set_rotation({0,-90,0});
    camera->viewport({64,64}); camera->createFB(); camera->getViewMatrix(); camera->obtProjectionMatrix();
    auto object = registry->create(2); registry->add<commons::physics>(object,true);
    camera->config.sky_solid_color = {0,0,0,1}; camera->drawFB();
    glBindFramebuffer(GL_FRAMEBUFFER, camera->fbo);
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glLineWidth(1.f);
    GLint before_fbo; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&before_fbo);
    editor::grid_gizmo overlay;
    overlay.draw_colliders(*camera,{0,0,64,64},registry);
    GLint after_fbo; glGetIntegerv(GL_FRAMEBUFFER_BINDING,&after_fbo);
    EXPECT_EQ(before_fbo,after_fbo); EXPECT_FALSE(glIsEnabled(GL_DEPTH_TEST)); EXPECT_FALSE(glIsEnabled(GL_BLEND));
    std::array<unsigned char,64*64*3> pixels{};
    glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    int green = 0;
    for (size_t i=0;i<pixels.size();i+=3) if (pixels[i+1]>150 && pixels[i+1]>pixels[i]*2) ++green;
    EXPECT_GT(green,10);
    glClear(GL_COLOR_BUFFER_BIT);
    overlay.draw_point_light(*camera, {0,0,64,64}, {0,0,0});
    std::fill(pixels.begin(), pixels.end(), 0);
    glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    int yellow = 0;
    for (size_t i=0;i<pixels.size();i+=3)
        if (pixels[i] > 180 && pixels[i+1] > 150 && pixels[i+2] < 120) ++yellow;
    EXPECT_GT(yellow,10);
    glDepthMask(GL_TRUE);
    glClearDepth(0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearDepth(1.0);
    overlay.draw_colliders(*camera, {0,0,64,64}, registry);
    overlay.draw_camera_frustum(*camera, *camera, {0,0,64,64});
    overlay.draw_point_light(*camera, {0,0,64,64}, {0,0,0});
    glReadPixels(0,0,64,64,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    EXPECT_TRUE(std::all_of(pixels.begin(), pixels.end(), [](auto value) { return value == 0; }));
    EXPECT_FALSE(glIsEnabled(GL_DEPTH_TEST));
    registry->remove(2);registry->remove(1);
}

TEST_F(EditorRenderTest, SoundPlayerAreaRendersAVisibleSphere) {
    auto camera = std::make_shared<commons::camera>();
    camera->viewport({64, 64});
    camera->createFB();
    camera->viewMatrix = glm::mat4(1.f);
    camera->projMatriz = glm::mat4(1.f);
    camera->config.sky_solid_color = {0.f, 0.f, 0.f, 1.f};
    camera->drawFB();

    editor::grid_gizmo overlay;
    glBindFramebuffer(GL_FRAMEBUFFER, camera->fbo);
    overlay.draw_sound_player_area(*camera, {0, 0, 64, 64}, {0.f, 0.f, 0.f}, 0.75f);
    std::array<unsigned char, 64 * 64 * 3> pixels{};
    glReadPixels(0, 0, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    int cyan = 0;
    for (std::size_t index = 0; index < pixels.size(); index += 3)
        if (pixels[index + 1] > 100 && pixels[index + 2] > 150 &&
            pixels[index + 2] > pixels[index] + 50)
            ++cyan;
    EXPECT_GT(cyan, 20);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST(ShadowSettings, ResolutionRoundTripsAndValidatesWithoutGpu) {
    commons::directional_light light;
    light.shadow_resolution = 1024;
    rapidjson::Document data(rapidjson::kObjectType);
    ASSERT_TRUE(light.serialize(data, data.GetAllocator()));
    commons::directional_light loaded;
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_EQ(loaded.shadow_resolution, 1024);
    for (int invalid : {-1, 0, 127, 8193}) {
        data["shadow_resolution"].SetInt(invalid);
        EXPECT_FALSE(loaded.analyze(data));
    }
    data["shadow_resolution"].SetFloat(512.5f);
    EXPECT_FALSE(loaded.analyze(data));
    data["shadow_resolution"].SetString("invalid");
    EXPECT_FALSE(loaded.analyze(data));
    data.RemoveMember("shadow_resolution");
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_EQ(loaded.shadow_resolution, 2048);
}

TEST_F(EditorRenderTest, TextureOffsetRepeatAndWorldProjectionChangePixelsAndResetBetweenMaterials) {
    glViewport(0, 0, 64, 64);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);
    commons::shader shader;
    shader.use();
    const auto identity = glm::mat4(1.f);
    shader.setMat4("view", glm::value_ptr(identity));
    shader.setMat4("projection", glm::value_ptr(identity));
    shader.setMat4("model", glm::value_ptr(identity));
    shader.setBool("shadowEnabled", false);
    shader.setVec3("viewPos", {0, 0, 3});
    shader.setVec3("dirLight.ambient", {1, 1, 1});
    shader.setVec3("dirLight.color", {0, 0, 0});
    shader.setVec3("dirLight.direction", {0, 0, -1});
    shader.setFloat("dirLight.intensity", 1.f);
    shader.setInt("pointLightCount", 0);
    GLuint texture{};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char pixels[]{255,0,0,255, 0,255,0,255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    commons::material material;
    material.set_uniform("material.albedo", commons::color{1,1,1,1});
    material.set_uniform("material.ao", 1.f);
    material.set_uniform("material.roughness", 0.5f);
    material.set_uniform("material.metallic", 0.f);
    material.set_texture("tex_albedo", commons::texture{texture, ""});
    material.set_uniform("use_tex_albedo", true);
    commons::mesh plane({
        {{-1,-1,0}, {0,0,1}, {0,0}}, {{1,-1,0}, {0,0,1}, {1,0}},
        {{1,1,0}, {0,0,1}, {1,1}}, {{-1,1,0}, {0,0,1}, {0,1}}
    }, {0,1,2,0,2,3}, material);
    plane.load();
    auto render = [&](const commons::material& value) {
        plane.setMaterial(value);
        glClear(GL_COLOR_BUFFER_BIT);
        plane.draw(shader);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(16, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    };
    const auto original = render(material);
    EXPECT_GT(original[0], 200);
    EXPECT_LT(original[1], 10);
    auto edited = material;
    edited.set_uniform("material.uvOffsetX", 0.5f);
    EXPECT_GT(render(edited)[1], 200);
    edited = material;
    edited.set_uniform("material.uvRepeatX", 6.f); // UV > 1 must wrap, not clamp.
    EXPECT_GT(render(edited)[1], 200);
    edited.set_uniform("material.uvRepeatX", 4.f);
    EXPECT_GT(render(edited)[0], 200);
    edited = material;
    edited.set_uniform("uvMundo", true);
    const auto world = render(edited);
    EXPECT_GT(world[1], 200); // Vertical face projects XY, not XZ.
    const auto scaled = glm::scale(identity, glm::vec3{2,2,1});
    shader.setMat4("model", glm::value_ptr(scaled));
    EXPECT_EQ(render(edited), world); // Fixed world position keeps texel density.
    shader.setMat4("model", glm::value_ptr(identity));
    EXPECT_EQ(render(material), original); // Shared shader must not leak UV state.
    GLint wrap{};
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &wrap);
    EXPECT_EQ(wrap, GL_CLAMP_TO_EDGE); // BGUI texture state is preserved.
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    glDeleteTextures(1, &texture);
}

TEST(CameraBackground, DefaultsToAtmosphereAndRoundTripsCubemapWithoutGpu) {
    commons::camera camera;
    EXPECT_FALSE(camera.config.use_skybox);
    rapidjson::Document data(rapidjson::kObjectType);
    camera.config.use_skybox = true;
    camera.config.skybox_image = "assets/cross.png";
    camera.skybox_faces = {"right.png", "left.png", "top.png", "bottom.png", "front.png", "back.png"};
    camera.config.atmosphere_zenith = {0.1f, 0.2f, 0.3f};
    camera.config.fog_enabled = true;
    camera.config.fog_color = {0.15f, 0.25f, 0.35f, 1.f};
    camera.config.fog_density = 0.04f;
    camera.config.corte_curto = 0.25f;
    ASSERT_TRUE(camera.serialize(data, data.GetAllocator()));
    ASSERT_TRUE(data.HasMember("znear"));
    EXPECT_FLOAT_EQ(data["znear"].GetFloat(), 0.25f);
    commons::camera loaded;
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_FLOAT_EQ(loaded.config.corte_curto, 0.25f);
    EXPECT_TRUE(loaded.config.use_skybox);
    EXPECT_EQ(loaded.skybox_faces, camera.skybox_faces);
    EXPECT_EQ(loaded.config.skybox_image, camera.config.skybox_image);
    EXPECT_FLOAT_EQ(loaded.config.atmosphere_zenith.y, 0.2f);
    EXPECT_TRUE(loaded.config.fog_enabled);
    EXPECT_FLOAT_EQ(loaded.config.fog_color.g, 0.25f);
    EXPECT_FLOAT_EQ(loaded.config.fog_density, 0.04f);
    EXPECT_EQ(loaded.m_skybox, nullptr); // Loading never allocates GPU resources.
    rapidjson::Document legacy(rapidjson::kObjectType);
    commons::camera legacy_loaded;
    ASSERT_TRUE(legacy_loaded.analyze(legacy));
    EXPECT_FLOAT_EQ(legacy_loaded.config.corte_curto, 0.1f);
    EXPECT_FALSE(legacy_loaded.config.use_skybox);
    EXPECT_TRUE(legacy_loaded.config.skybox_image.empty());
    EXPECT_FALSE(legacy_loaded.config.fog_enabled);
    rapidjson::Document invalid(rapidjson::kObjectType);
    invalid.AddMember("znear", 300.f, invalid.GetAllocator());
    EXPECT_FALSE(legacy_loaded.analyze(invalid));
}

TEST_F(EditorRenderTest, AtmosphereIsGradientAndMissingCubemapFallsBack) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(1);
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->viewportFBO = {64, 64};
    camera->createFB();
    registry->get<commons::transform>(entity.id)->set_rotation({0.f, -90.f, 0.f});
    commons::render_system render;
    render.update(registry);
    std::array<unsigned char, 3> top{}, bottom{}, fallback{};
    glReadPixels(32, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, top.data());
    glReadPixels(32, 3, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, bottom.data());
    EXPECT_NE(top, bottom);
    float depth = 0.f;
    glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    EXPECT_FLOAT_EQ(depth, 1.f);
    registry->get<commons::transform>(entity.id)->set_position({10.f, 20.f, 30.f});
    render.update(registry);
    glReadPixels(32, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, fallback.data());
    EXPECT_EQ(top, fallback); // Background depends on rotation, never position.
    camera->config.use_skybox = true;
    camera->skybox_faces.fill("/missing/cubemap.png");
    render.update(registry);
    glReadPixels(32, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, fallback.data());
    EXPECT_EQ(top, fallback);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, CubemapOverridesAtmosphereAndDisableRestoresIt) {
    const std::vector<std::vector<unsigned char>> images = {
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130},
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,96,248,207,240,31,0,4,1,1,255,113,235,71,229,0,0,0,0,73,69,78,68,174,66,96,130},
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,96,96,248,255,31,0,3,2,1,255,230,119,11,174,0,0,0,0,73,69,78,68,174,66,96,130},
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,255,159,225,63,0,7,253,2,254,166,148,197,180,0,0,0,0,73,69,78,68,174,66,96,130},
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,240,255,63,0,6,254,2,254,12,117,137,222,0,0,0,0,73,69,78,68,174,66,96,130},
        {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,96,248,255,255,63,0,5,255,2,254,94,14,59,173,0,0,0,0,73,69,78,68,174,66,96,130}
    };
    const auto folder = std::filesystem::temp_directory_path() /
        ("bengine-sky-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    std::array<std::string, 6> paths;
    for (unsigned i = 0; i < 6; ++i) {
        paths[i] = (folder / (std::to_string(i) + ".png")).string();
        std::ofstream output(paths[i], std::ios::binary);
        output.write(reinterpret_cast<const char*>(images[i].data()), images[i].size());
    }
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(1);
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->viewportFBO = {64, 64};
    camera->createFB();
    camera->skybox_faces = paths;
    camera->config.use_skybox = true;
    commons::render_system render;
    const bgui::vec3 rotations[] = {{0,0,0}, {0,180,0}, {90,0,0}, {-90,0,0}, {0,90,0}, {0,-90,0}};
    const std::array<unsigned char, 3> colors[] = {{255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}, {255,0,255}, {0,255,255}};
    std::array<unsigned char, 4> pixel{};
    for (unsigned i = 0; i < 6; ++i) {
        registry->get<commons::transform>(entity.id)->set_rotation(rotations[i]);
        render.update(registry);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        for (unsigned c = 0; c < 3; ++c) EXPECT_NEAR(pixel[c], colors[i][c], 2) << "Face " << i;
    }
    camera->config.use_skybox = false;
    render.update(registry);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[1], 100);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    std::filesystem::remove_all(folder);
}

TEST_F(EditorRenderTest, CrossCubemapMapsAllSixFacesAndRestoresUploadState) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->viewportFBO = {64, 64}; camera->createFB();
    camera->config.use_skybox = true;
    camera->config.skybox_image = (std::filesystem::path(BENGINE_SOURCE_DIR) / "tests/fixtures/cubemap_cross.png").string();
    commons::render_system render;
    const bgui::vec3 rotations[] = {{0,0,0},{0,180,0},{90,0,0},{-90,0,0},{0,90,0},{0,-90,0}};
    const std::array<unsigned char, 3> colors[] = {{255,0,0},{0,255,0},{0,0,255},{255,255,0},{255,0,255},{0,255,255}};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 7);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    for (unsigned i = 0; i < 6; ++i) {
        registry->get<commons::transform>(entity.id)->set_rotation(rotations[i]);
        render.update(registry);
        std::array<unsigned char, 4> pixel{};
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        for (unsigned c = 0; c < 3; ++c) EXPECT_NEAR(pixel[c], colors[i][c], 2) << "Face " << i;
    }
    GLint state{};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &state); EXPECT_EQ(state, 8);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &state); EXPECT_EQ(state, 7);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &state); EXPECT_EQ(state, 2);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &state); EXPECT_EQ(state, 1);
    registry->get<commons::transform>(entity.id)->set_rotation({0,0,0});
    render.update(registry);
    const std::array<unsigned char, 3> corners[] = {{255,255,255},{0,255,0},{0,0,255},{255,255,0}};
    const int positions[4][2] = {{60,60},{3,60},{60,3},{3,3}};
    for (unsigned i = 0; i < 4; ++i) {
        std::array<unsigned char, 4> pixel{};
        glReadPixels(positions[i][0], positions[i][1], 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        for (unsigned c = 0; c < 3; ++c) EXPECT_NEAR(pixel[c], corners[i][c], 2) << "Corner " << i;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0); glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    registry->entities.clear();
}

TEST_F(EditorRenderTest, InvalidCrossImageFallsBackToAtmosphereWithoutGpuErrors) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(); registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->viewportFBO = {64,64}; camera->createFB();
    commons::render_system render;
    render.update(registry);
    std::array<unsigned char,4> atmosphere{}, fallback{};
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,atmosphere.data());
    camera->config.use_skybox = true;
    for (const auto* path : {"external/cpp-bgui/tests/assets/rgb.jpg", "missing-cross.png"}) {
        camera->config.skybox_image = (std::filesystem::path(BENGINE_SOURCE_DIR) / path).string();
        render.update(registry);
        glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,fallback.data());
        EXPECT_EQ(atmosphere, fallback);
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
    registry->entities.clear();
}

TEST_F(EditorRenderTest, BuiltinFppTemplateMovementFollowsCameraAndPickupIsSeparateFromArena) {
    const auto project_directory = std::filesystem::temp_directory_path() /
        ("bengine-render-template-" +
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
    auto player_object = registry->create(106);
    registry->entity_names[player_object.id] = "PlayerCapsule";
    auto player_transform = registry->get<commons::transform>(player_object.id);
    player_transform->set_position({0, 0.1f, 10});
    player_transform->set_rotation({0, 90, 0});
    registry->add<commons::physics>(player_object);
    auto player_body = registry->get<commons::physics>(player_object.id);
    player_body->configure_collision(commons::physics::shapes::forma_capsula,
        {0.7f, 0.6f, 0.7f}, {0, 0.9f, 0}, true, false);
    player_body->set_body_type(commons::physics::body_type::character);
    player_body->setFriccao(0.f);
    auto camera_object = registry->create(100);
    registry->entity_names[camera_object.id] = "PlayerCamera";
    registry->add<commons::camera>(camera_object);
    registry->get<commons::transform>(camera_object.id)->set_parent(player_object.id, false);
    auto pickup_object = registry->create(103);
    registry->entity_names[pickup_object.id] = "PickupOrb";
    registry->get<commons::transform>(pickup_object.id)->set_position({20, 1, 0});
    registry->add<commons::renderer>(pickup_object);
    registry->add<commons::physics>(pickup_object, true);
    auto pillar_object = registry->create(107);
    registry->entity_names[pillar_object.id] = "ArenaPillar";
    auto floor = registry->create(101);
    registry->get<commons::transform>(floor.id)->set_position({0, -0.5f, 0});
    registry->add<commons::physics>(floor, true);
    registry->get<commons::physics>(floor.id)->configure_collision(
        commons::physics::shapes::box_shape, {40, 1, 40}, {0, 0, 0}, true, false);
    const auto scripts_path = project_directory / "scripts";
    registry->add<commons::code>(player_object, (scripts_path / "fpp_player.lua").string());
    registry->add<commons::code>(camera_object, (scripts_path / "fpp_camera.lua").string());
    registry->add<commons::code>(pickup_object, (scripts_path / "pickup.lua").string());
    const auto player = registry->find_by_name("PlayerCapsule");
    const auto camera_entity = registry->find_by_name("PlayerCamera");
    const auto pickup = registry->find_by_name("PickupOrb");
    ASSERT_TRUE(player); ASSERT_TRUE(camera_entity); ASSERT_TRUE(pickup);
    EXPECT_EQ(registry->get<commons::transform>(*camera_entity)->parent_id, *player);
    EXPECT_EQ(registry->get<commons::transform>(*camera_entity)->get_scale(), (bgui::vec3{1,1,1}));
    const auto pillar = registry->find_by_name("ArenaPillar");
    ASSERT_TRUE(pillar);
    EXPECT_FALSE(registry->has<commons::code>(*pillar));
    commons::code_system scripts;
    scripts.setup(registry);
    auto body = registry->get<commons::physics>(*player);
    body->setVelocidade({0,-3,0});
    auto controller = registry->get<commons::code>(*player);
    sol::table input = controller->estado_lua["Input"];
    input.set_function("GetAxis", [](const std::string& axis) { return axis == "Vertical" ? 1.f : 0.f; });
    input.set_function("GetMouseDelta", [] { return std::tuple<float,float>{10,0}; });
    ASSERT_NO_THROW(controller->update());
    const auto velocity = body->getVelocidade();
    auto camera = registry->get<commons::camera>(*camera_entity);
    camera->getViewMatrix();
    EXPECT_NEAR(velocity.x, camera->forward.x * 5.f, 0.0001f);
    EXPECT_NEAR(velocity.z, camera->forward.z * 5.f, 0.0001f);
    EXPECT_FLOAT_EQ(velocity.y, -3.f);
    commons::physics_system physics;
    physics.setup(registry);
    const auto start = body->m_rigid_body->getWorldTransform().getOrigin();
    for (int step = 0; step < 120; ++step) physics.world()->stepSimulation(1.f / 60.f, 0);
    const auto landed = body->m_rigid_body->getWorldTransform().getOrigin();
    EXPECT_GT(landed.y(), -0.1f); EXPECT_LT(landed.y(), 0.25f);
    EXPECT_NEAR(std::hypot(landed.x() - start.x(), landed.z() - start.z()), 10.f, 0.2f);
    EXPECT_EQ(body->m_rigid_body->getAngularFactor(), btVector3(0,0,0));
    physics.speed = 0; physics.update(registry);
    registry->get<commons::transform>(*player)->set_position(registry->get<commons::transform>(*pickup)->get_position());
    auto game = registry->get<commons::code>(*pickup);
    ASSERT_NO_THROW(game->update());
    EXPECT_FALSE(registry->get<commons::renderer>(*pickup)->visible);
    EXPECT_FALSE(registry->has<commons::physics>(*pickup));
    ASSERT_NO_THROW(game->update());
    physics.update(registry);
    registry->entities.clear();
}

TEST(CameraSettings, RejectsMalformedCrossPathsWithoutTruncatingThem) {
    commons::camera camera;
    camera.config.skybox_image = "assets/original.png";
    for (const auto* json : {R"({"skybox_image":42})", R"({"skybox_image":"bad\u0000path"})"}) {
        rapidjson::Document data; data.Parse(json);
        EXPECT_FALSE(camera.analyze(data));
        EXPECT_EQ(camera.config.skybox_image, "assets/original.png");
    }
    camera.config.skybox_image = std::string("bad\0path", 8);
    rapidjson::Document data(rapidjson::kObjectType);
    EXPECT_FALSE(camera.serialize(data, data.GetAllocator()));
}

TEST(CameraSettings, RejectsInvalidFogSettingsWithoutMutatingConfig) {
    commons::camera camera;
    camera.config.fog_enabled = true;
    camera.config.fog_density = 0.2f;
    camera.config.fog_color = {0.1f, 0.2f, 0.3f, 1.f};

    rapidjson::Document invalid(rapidjson::kObjectType);
    invalid.AddMember("fog_density", -1.f, invalid.GetAllocator());
    EXPECT_FALSE(camera.analyze(invalid));
    EXPECT_TRUE(camera.config.fog_enabled);
    EXPECT_FLOAT_EQ(camera.config.fog_density, 0.2f);
    EXPECT_FLOAT_EQ(camera.config.fog_color.g, 0.2f);

    camera.config.fog_density = std::numeric_limits<float>::infinity();
    rapidjson::Document output(rapidjson::kObjectType);
    EXPECT_FALSE(camera.serialize(output, output.GetAllocator()));
}

TEST_F(EditorRenderTest, CameraFilterSkipsUnusedFramebuffersAndCanBeCleared) {
    auto registry = std::make_shared<commons::ecs>();
    auto first = registry->create();
    auto second = registry->create();
    registry->add<commons::camera>(first);
    registry->add<commons::camera>(second);
    auto first_camera = registry->get<commons::camera>(first.id);
    auto second_camera = registry->get<commons::camera>(second.id);
    first_camera->viewport({32, 32});
    second_camera->viewport({32, 32});
    first_camera->request_framebuffer_size({48, 48});
    second_camera->request_framebuffer_size({48, 48});
    commons::render_system render;
    render.set_camera_filter([id = first.id](std::uint32_t entity) { return entity == id; });
    render.update(registry);
    EXPECT_EQ(render.last_camera_count(), 1u);
    EXPECT_EQ(first_camera->viewportFBO.x, 48);
    EXPECT_EQ(second_camera->viewportFBO.x, 32);
    render.set_camera_filter([](std::uint32_t) { return false; });
    render.update(registry);
    EXPECT_EQ(render.last_camera_count(), 0u);
    render.set_camera_filter({});
    render.update(registry);
    EXPECT_EQ(render.last_camera_count(), 2u);
    EXPECT_EQ(second_camera->viewportFBO.x, 48);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

#include "model_editor_view.hpp"
#include "loaders/editable_model.hpp"
#include "loaders/material_asset.hpp"

TEST_F(EditorRenderTest, ExplicitViewPreservesGlStateAndUsesFullScene) {
    auto registry=std::make_shared<commons::ecs>();
    auto object=registry->create();
    auto model=std::make_unique<commons::model>();
    commons::material material;
    material.set_uniform("material.albedo",commons::color{1.f,.1f,.1f,1.f});
    model->meshes.emplace_back(std::vector<commons::vertex>{
        {{-1,-1,0},{0,0,1},{}},{{1,-1,0},{0,0,1},{}},{{0,1,0},{0,0,1},{}}},
        std::vector<unsigned>{0,1,2},material);
    registry->add<commons::renderer>(object,model.release());
    commons::camera view;
    view.position={0,0,3};
    view.viewMatrix=glm::lookAt(glm::vec3{0,0,3},glm::vec3{},glm::vec3{0,1,0});
    view.request_framebuffer_size({64,64});
    glViewport(3,5,23,27);
    glEnable(GL_SCISSOR_TEST);glScissor(1,2,8,9);
    glEnable(GL_BLEND);glDepthMask(GL_FALSE);glColorMask(GL_TRUE,GL_FALSE,GL_TRUE,GL_FALSE);
    glActiveTexture(GL_TEXTURE3);
    commons::render_system rendering;
    rendering.render_view(registry,view);
    EXPECT_EQ(rendering.last_camera_count(),1u);
    EXPECT_EQ(rendering.last_rendered_entity_count(),1u);
    GLint viewport[4]{};glGetIntegerv(GL_VIEWPORT,viewport);
    EXPECT_EQ(viewport[0],3);EXPECT_EQ(viewport[1],5);EXPECT_EQ(viewport[2],23);EXPECT_EQ(viewport[3],27);
    GLint active{},framebuffer{};glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&framebuffer);
    EXPECT_EQ(active,GL_TEXTURE3);EXPECT_EQ(framebuffer,0);
    EXPECT_TRUE(glIsEnabled(GL_SCISSOR_TEST));EXPECT_TRUE(glIsEnabled(GL_BLEND));
    GLboolean mask{};glGetBooleanv(GL_DEPTH_WRITEMASK,&mask);EXPECT_FALSE(mask);
    ASSERT_NE(view.framebuffer_texture(),0u);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,view.fbo);
    std::array<unsigned char,4> pixel{};
    glReadPixels(32,32,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    EXPECT_GT(pixel[0],pixel[1]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,0);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDepthMask(GL_TRUE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    EXPECT_EQ(glGetError(),GL_NO_ERROR);
}

#include "game_view.hpp"

TEST_F(EditorRenderTest, GamePanelRendersToItsOwnFramebuffer) {
    bgui::scoped_interface scope;
    bgui::get_context().m_size = {900, 650};
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto source = registry->get<commons::camera>(entity.id);
    source->viewportFBO = {180, 100};
    source->createFB();
    ASSERT_TRUE(source->flag_fb);

    auto& dock = bgui::get_layout().add<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    editor::ui::game_view panel(context);
    panel.build(dock);
    const auto find_image = [](auto&& self, bgui::element& element) -> bgui::image* {
        if (auto* image = dynamic_cast<bgui::image*>(&element);
            image && image->has_class("editor-viewport-surface")) return image;
        if (auto* layout = element.as_layout())
            for (auto& [layer, children] : layout->get_elements())
                for (auto& child : children)
                    if (auto* image = self(self, *child)) return image;
        return nullptr;
    };
    auto* image = find_image(find_image, dock);
    ASSERT_NE(image, nullptr);
    for (const auto size : {bgui::vec2i{900, 650}, bgui::vec2i{740, 500}}) {
        bgui::get_context().m_size = size;
        for (int i = 0; i < 3; ++i) bgui::on_update();
        const auto texture = image->get_texture();
        ASSERT_NE(texture.m_id, 0u);
        EXPECT_NE(texture.m_id, source->framebuffer_texture());
        EXPECT_EQ(source->viewportFBO, (bgui::vec2i{180, 100}));
        glBindTexture(GL_TEXTURE_2D, texture.m_id);
        GLint width = 0, height = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        EXPECT_EQ(width, image->processed_width());
        EXPECT_EQ(height, image->processed_height());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, ModelModesCacheDenseGeometryAndUseGpuPreview) {
    bgui::scoped_interface scope;
    bgui::get_context().m_size={1000,700};
    auto registry=std::make_shared<commons::ecs>();
    auto context=std::make_shared<editor::ui::editor_context>(registry);
    auto entity=registry->create();
    auto parent = registry->create();
    auto parent_transform = registry->get<commons::transform>(parent.id);
    parent_transform->set_scale({2.f, 1.f, .5f});
    ASSERT_TRUE(registry->get<commons::transform>(entity.id)->set_parent(parent.id, false));
    auto model=std::make_unique<commons::model>();
    std::vector<commons::vertex> vertices;
    std::vector<unsigned> indices;
    constexpr unsigned side=100;
    for(unsigned y=0;y<=side;++y)for(unsigned x=0;x<=side;++x)
        vertices.push_back({{float(x)/side*2-1,float(y)/side*2-1,0},{0,0,1},{float(x)/side,float(y)/side}});
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
        const auto a=y*(side+1)+x,b=a+1,c=a+side+1,d=c+1;
        indices.insert(indices.end(),{a,b,d,a,d,c});
    }
    model->meshes.emplace_back(vertices,indices,commons::material{});
    registry->add<commons::renderer>(entity,model.release());
    context->selected_entity=entity.id;
    auto& dock=bgui::get_layout().add<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::match_parent);
    editor::ui::model_editor_view panel(context);
    panel.build(dock);
    for(int i=0;i<3;++i)bgui::on_update();
    const auto uploads=panel.geometry_upload_count(),projections=panel.projection_update_count();
    EXPECT_GT(uploads,0u);
    for(int i=0;i<10;++i)bgui::on_update();
    EXPECT_EQ(panel.geometry_upload_count(),uploads);
    EXPECT_EQ(panel.projection_update_count(),projections);
    for(int mode=0;mode<3;++mode) {
        context->model_view_mode=mode;
        bgui::on_update();glFinish();
        std::cout << "Model preview (20,000 triangles), mode " << mode << ": " << panel.last_preview_time_ms() << " ms CPU\n";
        EXPECT_EQ(panel.geometry_upload_count(),uploads);
        EXPECT_EQ(panel.projection_update_count(),projections);
        EXPECT_NE(panel.preview_camera().framebuffer_texture(),0u);
        EXPECT_EQ(glGetError(),GL_NO_ERROR);
    }
    context->model_view_mode = 0;
    parent_transform->move({3.f, 0.f, 0.f});
    bgui::on_update();
    EXPECT_EQ(panel.geometry_upload_count(), uploads);
    EXPECT_GT(panel.projection_update_count(), projections);
    const auto original_size = panel.preview_camera().viewportFBO;
    bgui::get_context().m_size = {750, 510};
    for (int i = 0; i < 3; ++i) bgui::on_update();
    const auto& camera = panel.preview_camera();
    EXPECT_NE(camera.viewportFBO, original_size);
    glBindTexture(GL_TEXTURE_2D, camera.framebuffer_texture());
    GLint width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    EXPECT_EQ(width, camera.viewportFBO.x);
    EXPECT_EQ(height, camera.viewportFBO.y);
    glBindTexture(GL_TEXTURE_2D, 0);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, CameraReleaseClearsHandlesAndAllowsRecreation) {
    commons::camera camera;
    camera.viewportFBO = {16, 16};
    camera.createFB();
    ASSERT_TRUE(camera.flag_fb);
    ASSERT_NE(camera.framebuffer_texture(), 0u);
    camera.deleteFB();
    EXPECT_FALSE(camera.flag_fb);
    EXPECT_EQ(camera.fbo, 0u);
    EXPECT_EQ(camera.rbo, 0u);
    EXPECT_EQ(camera.framebuffer_texture(), 0u);
    camera.deleteFB();
    camera.createFB();
    EXPECT_TRUE(camera.flag_fb);
    EXPECT_NE(camera.framebuffer_texture(), 0u);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, MaterialThumbnailRendersSphereAndReflectsEdits) {
    editor::ui::material_thumbnail_renderer preview;
    commons::material material;
    material.set_uniform("material.albedo", commons::color{.8f, .05f, .03f, 1.f});
    const auto red = preview.render("paint.bmat", material);
    material.set_uniform("material.albedo", commons::color{.03f, .05f, .8f, 1.f});
    const auto blue = preview.render("paint.bmat", material);

    ASSERT_EQ(red.m_buffer.size(), 96u * 96u * 3u);
    ASSERT_EQ(blue.m_buffer.size(), red.m_buffer.size());
    const auto center = (48u * 96u + 48u) * 3u;
    EXPECT_GT(red.m_buffer[center], red.m_buffer[center + 2]);
    EXPECT_GT(blue.m_buffer[center + 2], blue.m_buffer[center]);
    EXPECT_TRUE(std::equal(red.m_buffer.begin(), red.m_buffer.begin() + 3, blue.m_buffer.begin()));
    EXPECT_NE(red.m_buffer[center], red.m_buffer[0]);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(EditorRenderTest, ConsecutiveEntitiesReuseMaterialUniformsUntilConfigurationChanges) {
    auto registry = std::make_shared<commons::ecs>();
    const auto add_triangle = [&](float x, float red) {
        auto object = registry->create();
        registry->get<commons::transform>(object.id)->set_position({x, 0, 0});
        auto model = std::make_unique<commons::model>();
        commons::material material;
        material.set_uniform("material.albedo", commons::color{red, 0.1f, 0.1f, 1.f});
        model->meshes.emplace_back(std::vector<commons::vertex>{
            {{-.2f,-.2f,0},{0,0,1},{}}, {{.2f,-.2f,0},{0,0,1},{}}, {{0,.2f,0},{0,0,1},{}}},
            std::vector<unsigned>{0,1,2}, material);
        registry->add<commons::renderer>(object, model.release());
    };
    add_triangle(-.5f, 1.f);
    add_triangle(0.f, 1.f);
    add_triangle(.5f, .5f);

    commons::camera view;
    view.position = {0,0,3};
    view.viewMatrix = glm::lookAt(glm::vec3{0,0,3}, glm::vec3{}, glm::vec3{0,1,0});
    view.request_framebuffer_size({64,64});
    commons::render_system rendering;

    struct uniform_probe {
        static PFNGLUNIFORM4FPROC& original() {
            static PFNGLUNIFORM4FPROC function;
            return function;
        }
        static int& uploads() {
            static int count;
            return count;
        }
        static void APIENTRY record(GLint location, GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
            if (location >= 0 && g == 0.1f && b == 0.1f && a == 1.f)
                ++uploads();
            original()(location, r, g, b, a);
        }
        uniform_probe() {
            original() = glad_glUniform4f;
            uploads() = 0;
            glad_glUniform4f = record;
        }
        ~uniform_probe() { glad_glUniform4f = original(); }
    } probe;

    rendering.render_view(registry, view);
    EXPECT_EQ(rendering.last_rendered_entity_count(), 3u);
    EXPECT_EQ(probe.uploads(), 2);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}
