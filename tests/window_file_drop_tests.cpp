#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include "os/window.hpp"
#include "asset_import.hpp"
#include "loaders/model.hpp"
#include "asset_drop_overlay.hpp"
#include "systems/ui_system.hpp"
#include <bgui_backend_gl3.hpp>
#include <array>
#include <chrono>
#include <fstream>

TEST(WindowFileDropTest, QueuesOwnedUtf8PathsAndCursorCoordinatesUntilRefresh) {
    if (!glfwInit()) GTEST_SKIP() << "No display for native window verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("File drop test", 100, 100);
    auto* native = glfwGetCurrentContext();
    ASSERT_NE(native, nullptr);
    std::vector<std::filesystem::path> received;
    int count = 0, received_x = -1, received_y = -1;
    window.set_close_request_callback([]() {});
    window.set_file_drop_callback([&](const auto& paths, int x, int y) {
        received.insert(received.end(), paths.begin(), paths.end());
        ++count;
        received_x = x;
        received_y = y;
    });
    auto callback = glfwSetDropCallback(native, nullptr);
    ASSERT_NE(callback, nullptr);
    glfwSetDropCallback(native, callback);
    glfwSetCursorPos(native, 17, 23);
    double x{}, y{};
    glfwGetCursorPos(native, &x, &y);
    char first[] = "outside/\xc3\xa1.lua";
    const char* paths[]{first, "outside/model.obj"};
    callback(native, 2, paths);
    first[0] = 'X'; // Native paths need to be copied before GLFW frees them.
    EXPECT_EQ(count, 0);
    window.refresh();
    ASSERT_EQ(count, 1);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].u8string(), u8"outside/á.lua");
    EXPECT_EQ(received[1], std::filesystem::path("outside/model.obj"));
    EXPECT_EQ(received_x, static_cast<int>(x));
    EXPECT_EQ(received_y, static_cast<int>(y));
    window.refresh();
    EXPECT_EQ(count, 1);
    window.set_file_drop_callback({});
    EXPECT_FALSE(window.file_drag_hovered());
    EXPECT_EQ(glfwSetDropCallback(native, nullptr), nullptr);
}

TEST(WindowFileDropTest, ImportedModelUsesCopiedNestedTextures) {
    if (!glfwInit()) GTEST_SKIP() << "No display for model upload verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("Imported model test", 100, 100);
    struct temporary_directory {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("bengine-import-model-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~temporary_directory() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } temporary;
    const auto source = temporary.path / "source";
    const auto project = temporary.path / "project";
    std::filesystem::create_directories(source / "textures");
    std::filesystem::create_directories(project);
    std::ofstream(source / "model.obj") << "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n";
    std::ofstream(source / "model.mtl") << "newmtl test\nmap_Kd textures/color.png\n";
    std::ofstream(source / "textures/color.png") << "texture data";
    const auto imported = editor::ui::import_asset(source / "model.obj", project, true);
    std::filesystem::remove_all(source);
    commons::model model(imported.string());
    ASSERT_EQ(model.meshes.size(), 1u);
    const auto texture = model.meshes[0].getMaterial().getTextura("tex_albedo");
    EXPECT_EQ(std::filesystem::path(texture.path), imported.parent_path() / "textures/color.png");
    EXPECT_TRUE(std::filesystem::is_regular_file(texture.path));
}

TEST(WindowFileDropTest, DropHintDarkensTheFramebufferAndDisappearsWithoutLeavingAnOverlay) {
    if (!glfwInit()) GTEST_SKIP() << "No display for overlay rendering verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("Drop overlay test", 200, 200);
    window.add<commons::ui_system>();
    auto context = std::make_shared<editor::ui::editor_context>(window.get_ecs());
    bool hovering = false;
    bgui::get_context().m_size = {200, 200};
    auto& root = bgui::get_layout();
    root.style.visual.background.normal = bgui::color{1, 1, 1, 1};
    root.mark_style_dirty();
    auto& overlay = root.add<editor::ui::asset_drop_overlay, bgui::layer::overlay>(
        [&]() { return hovering; }, context);
    const auto render_pixel = [&]() {
        bgui::on_update();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, 200, 200);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        bgui::gl3_render(bgui::get_draw_data());
        std::array<unsigned char, 4> pixel{};
        glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
        return pixel;
    };
    const auto hidden = render_pixel();
    hovering = true;
    const auto shown = render_pixel();
    EXPECT_GT(hidden[0], 230);
    EXPECT_LT(shown[0], hidden[0] - 50);
    EXPECT_GT(shown[0], 20); // The underlying white surface remains visible.
    EXPECT_EQ(overlay.processed_rect(), (bgui::vec4i{0, 0, 200, 200}));
    hovering = false;
    const auto cleared = render_pixel();
    EXPECT_EQ(cleared, hidden);
}

TEST(WindowFileDropTest, EngineTextureUploadsDecodedJpegPixels) {
    if (!glfwInit()) GTEST_SKIP() << "No display for JPEG texture verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("JPEG texture test", 100, 100);
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "external/cpp-bgui/tests/assets/rgb.jpg";
    commons::texture texture{0, path.string()};
    texture.bind(0);
    int width = 0, height = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    ASSERT_EQ(width, 3);
    ASSERT_EQ(height, 2);
    std::array<unsigned char, 24> pixels{};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    EXPECT_GT(pixels[0], 240);
    EXPECT_LT(pixels[1], 10);
    EXPECT_LT(pixels[2], 10);
    EXPECT_EQ(pixels[3], 255);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}
