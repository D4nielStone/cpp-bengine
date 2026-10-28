#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgui.hpp>
#include <bgui_backend_freetype.hpp>
#include <gtest/gtest.h>
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "project_persistence_system.hpp"
#include "viewport_sync.hpp"
#include <chrono>
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
    auto material = renderer->m_modelo->meshes[0].getMaterial();
    material.set_uniform("material.metallic", 0.73f);
    material.set_uniform("material.roughness", 0.39f);
    material.set_uniform("material.ao", 0.87f);
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
    const auto loaded = registry->get<commons::renderer>(entity.id)->m_modelo->meshes[0].getMaterial();
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.metallic").valor.f, 0.73f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.roughness").valor.f, 0.39f);
    EXPECT_FLOAT_EQ(loaded.getUniforme("material.ao").valor.f, 0.87f);
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
