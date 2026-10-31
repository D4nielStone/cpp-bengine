#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgui.hpp>
#include <bgui_backend_freetype.hpp>
#include <gtest/gtest.h>
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/directional_light.hpp"
#include "systems/render_system.hpp"
#include "project_persistence_system.hpp"
#include "viewport_sync.hpp"
#include "guizmo/grid_gizmo.hpp"
#include "components/physics.hpp"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

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

TEST_F(EditorRenderTest, UploadsTexturePathsAndRoundTripsPbrMaterials) {
    const auto source = std::filesystem::path(BENGINE_SOURCE_DIR);
    const auto image_path = source / "projects/fpp_demo/project.bengine.preview.png";
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
    std::filesystem::remove_all(folder);
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

TEST_F(EditorRenderTest, DeferredViewportResizePreservesRenderedPixelsUntilNextCameraPass) {
    bgui::scoped_interface interface;
    auto camera = std::make_shared<commons::camera>();
    camera->viewport({32, 24});
    camera->createFB();
    auto& image = bgui::get_layout().add<bgui::image>();
    editor::ui::viewport_sync_system sync(camera, image);
    camera->ceu = {1.f, 0.f, 0.f, 1.f};
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

TEST_F(EditorRenderTest, DirectionalShadowsRespectCasterReceiverAndLightControls) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    registry->get<commons::transform>(camera_entity.id)->set_position({0,6,8});
    registry->get<commons::transform>(camera_entity.id)->set_rotation({-36.8699f,-90,0});
    camera->position = {0, 6, 8};
    camera->viewMatrix = glm::lookAt(glm::vec3{0,6,8}, glm::vec3{0,0,0}, glm::vec3{0,1,0});
    camera->flag_orth = true;
    camera->scale = 5.f;
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
    camera->ceu = {0,0,0,1}; camera->drawFB();
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
    registry->remove(2);registry->remove(1);
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
    EXPECT_FALSE(camera.m_use_skybox);
    rapidjson::Document data(rapidjson::kObjectType);
    camera.m_use_skybox = true;
    camera.skybox_faces = {"right.png", "left.png", "top.png", "bottom.png", "front.png", "back.png"};
    camera.atmosphere_zenith = {0.1f, 0.2f, 0.3f};
    ASSERT_TRUE(camera.serialize(data, data.GetAllocator()));
    commons::camera loaded;
    ASSERT_TRUE(loaded.analyze(data));
    EXPECT_TRUE(loaded.m_use_skybox);
    EXPECT_EQ(loaded.skybox_faces, camera.skybox_faces);
    EXPECT_FLOAT_EQ(loaded.atmosphere_zenith.y, 0.2f);
    EXPECT_EQ(loaded.m_skybox, nullptr); // Loading never allocates GPU resources.
    rapidjson::Document legacy(rapidjson::kObjectType);
    ASSERT_TRUE(loaded.analyze(legacy));
    EXPECT_FALSE(loaded.m_use_skybox);
}

TEST_F(EditorRenderTest, AtmosphereIsGradientAndMissingCubemapFallsBack) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(1);
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    camera->viewportFBO = {64, 64};
    camera->createFB();
    camera->viewMatrix = glm::lookAt(glm::vec3(0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    commons::render_system render;
    render.update(registry);
    std::array<unsigned char, 3> top{}, bottom{}, fallback{};
    glReadPixels(32, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, top.data());
    glReadPixels(32, 3, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, bottom.data());
    EXPECT_NE(top, bottom);
    float depth = 0.f;
    glReadPixels(32, 32, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    EXPECT_FLOAT_EQ(depth, 1.f);
    camera->viewMatrix = glm::lookAt(glm::vec3(10, 20, 30), glm::vec3(10, 20, 29), glm::vec3(0, 1, 0));
    render.update(registry);
    glReadPixels(32, 60, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, fallback.data());
    EXPECT_EQ(top, fallback); // Background depends on rotation, never position.
    camera->m_use_skybox = true;
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
    camera->m_use_skybox = true;
    commons::render_system render;
    const glm::vec3 directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    const std::array<unsigned char, 3> colors[] = {{255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}, {255,0,255}, {0,255,255}};
    std::array<unsigned char, 4> pixel{};
    for (unsigned i = 0; i < 6; ++i) {
        const auto up = (i == 2 || i == 3) ? glm::vec3{0,0,1} : glm::vec3{0,1,0};
        camera->viewMatrix = glm::lookAt(glm::vec3(0), directions[i], up);
        render.update(registry);
        glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        for (unsigned c = 0; c < 3; ++c) EXPECT_NEAR(pixel[c], colors[i][c], 2) << "Face " << i;
    }
    camera->m_use_skybox = false;
    render.update(registry);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    EXPECT_GT(pixel[1], 100);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
    std::filesystem::remove_all(folder);
}
