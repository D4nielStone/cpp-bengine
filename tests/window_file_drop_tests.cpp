#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include "opengl_test_support.hpp"
#include "os/window.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "asset_import.hpp"
#include "loaders/model.hpp"
#include "asset_drop_overlay.hpp"
#include "systems/ui_system.hpp"
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
#include <gl3/gl3_shader.hpp>
#include <array>
#include <chrono>
#include <fstream>

TEST(WindowFileDropTest, QueuesOwnedUtf8PathsAndCursorCoordinatesUntilRefresh) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for native window verification";
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
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for model upload verification";
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
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for overlay rendering verification";
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
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for JPEG texture verification";
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

#include <glfw/bgui_backend_glfw_events.hpp>

TEST(WindowFileDropTest, NativeEventObserverIsChainedAndRestoredWhenTheLoopEndsOrThrows) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for event observer verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("Event observer test", 100, 100);
    auto* native = glfwGetCurrentContext();
    int observations = 0;
    struct restore_callbacks {
        bgui::glfw_event_observer observer;
        std::function<void()> refresh;
        ~restore_callbacks() {
            bgui::set_glfw_event_observer(std::move(observer));
            bgui::get_context().m_refresh_func = std::move(refresh);
        }
    } restore{bgui::set_glfw_event_observer([&] { ++observations; }), bgui::get_context().m_refresh_func};
    auto& refresh = bgui::get_context().m_refresh_func;
    refresh = [native] { glfwSetWindowShouldClose(native, GLFW_TRUE); };
    window.loop();
    EXPECT_EQ(observations, 1);
    // Taking out the restored observer must return the caller's original function.
    auto restored = bgui::set_glfw_event_observer({});
    EXPECT_TRUE(bool(restored));
    if (restored) restored();
    EXPECT_EQ(observations, 2);
    bgui::set_glfw_event_observer(std::move(restored));
    glfwSetWindowShouldClose(native, GLFW_FALSE);
    refresh = [] { throw std::runtime_error("refresh test"); };
    EXPECT_THROW(window.loop(), std::runtime_error);
    restored = bgui::set_glfw_event_observer({});
    EXPECT_TRUE(bool(restored));
    if (restored) restored();
    EXPECT_EQ(observations, 4);
}


TEST(WindowFileDropTest, NativeF3KeyIsDeliveredToTheSharedInputState) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for native key verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("F3 input test", 100,100);
    auto* native = glfwGetCurrentContext(); ASSERT_NE(native,nullptr);
    const auto callback = glfwSetKeyCallback(native,nullptr); ASSERT_NE(callback,nullptr);
    glfwSetKeyCallback(native,callback);
    callback(native,GLFW_KEY_F3,0,GLFW_PRESS,0);
    EXPECT_TRUE(bgui::get_pressed(bgui::input_key::f3));
    callback(native,GLFW_KEY_F3,0,GLFW_RELEASE,0);
    EXPECT_FALSE(bgui::get_pressed(bgui::input_key::f3));
}

TEST(WindowLifetimeTest, ReleasesAttachedGraphicsBeforeContextEvenWithExternalOwners) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 unavailable for graphics shutdown verification";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    std::shared_ptr<commons::ecs> registry;
    std::shared_ptr<commons::camera> camera;
    std::shared_ptr<commons::renderer> renderer;
    bool released_with_context = false;
    struct context_probe : commons::component {
        bool& observed;
        explicit context_probe(bool& value) : observed(value) {}
        void release_graphics_resources() override {
            observed = glfwGetCurrentContext() != nullptr;
        }
    };
    {
        commons::window window("ECS shutdown", 100, 100);
        registry = window.get_ecs();
        auto object = registry->create();
        registry->add<commons::camera>(object);
        camera = registry->get<commons::camera>(object.id);
        camera->createFB();
        ASSERT_TRUE(camera->flag_fb);
        auto geometry = std::make_unique<commons::model>();
        geometry->meshes.emplace_back(std::vector<commons::vertex>{},
            std::vector<unsigned int>{}, commons::material{});
        geometry->meshes.front().load();
        registry->add<commons::renderer>(object, geometry.release());
        renderer = registry->get<commons::renderer>(object.id);
        registry->entities[object.id][commons::component::COMPONENTE_TEXTO] =
            std::make_shared<context_probe>(released_with_context);
        glfwMakeContextCurrent(nullptr);
    }
    EXPECT_TRUE(released_with_context);
    EXPECT_FALSE(camera->flag_fb);
    EXPECT_FALSE(renderer->m_modelo->meshes.front().isLoaded());
    EXPECT_TRUE(registry->entities.empty());
    EXPECT_TRUE(registry->validate_invariants());
    EXPECT_EQ(glfwGetCurrentContext(), nullptr);
    camera.reset();
    renderer.reset();
}

TEST(WindowFileDropTest, RecreatesCursorsAfterWindowShutdown) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for cursor lifetime verification";
    for (int cycle = 0; cycle < 3; ++cycle) {
        commons::window window("Cursor lifetime test", 100, 100);
        auto& io = bgui::get_context();
        for (auto cursor : {bgui::cursor::arrow, bgui::cursor::hand, bgui::cursor::ibeam,
                 bgui::cursor::resize_horizontal, bgui::cursor::resize_vertical,
                 bgui::cursor::resize_nwse, bgui::cursor::resize_nesw}) {
            io.m_actual_cursor = cursor;
            bgui::glfw_update(io);
            EXPECT_EQ(glfwGetError(nullptr), GLFW_NO_ERROR) << "cycle " << cycle;
        }
    }
}

TEST(WindowLifetimeTest, RecreatesEngineAndUiShadersAfterContextShutdown) {
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for shader lifetime verification";
    for (int cycle = 0; cycle < 3; ++cycle) {
        {
            commons::window window("Shader lifetime test", 100, 100);
            window.add<commons::ui_system>();
            EXPECT_TRUE(commons::shaders.empty());
            commons::shader engine_shader("pbr.vert", "pbr.frag");
            ASSERT_TRUE(glIsProgram(engine_shader.ID));
            auto* text_shader = bgl::get_text_gl3_shader();
            text_shader->bind();
            GLint program{};
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            EXPECT_GE(glGetUniformLocation(program, "text_color"), 0);
            text_shader->unbind();
            EXPECT_EQ(glGetError(), GL_NO_ERROR) << "cycle " << cycle;
        }
        EXPECT_TRUE(commons::shaders.empty());
    }
}
