#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <gtest/gtest.h>
#include "opengl_test_support.hpp"
#include "loaders/model.hpp"
#include "loaders/model_pose.hpp"
#include "model_import_dialog.hpp"
#include <chrono>
#include <cstring>

namespace {
struct fixture {
    aiScene scene;
    commons::model_pose pose;
    fixture() {
        scene.mRootNode = new aiNode("root");
        scene.mRootNode->mNumChildren = 1;
        auto* child = new aiNode("bone");
        child->mParent = scene.mRootNode;
        scene.mRootNode->mChildren = new aiNode*[1]{child};
        child->mTransformation.a4 = 2;
        child->mNumMeshes = 1; child->mMeshes = new unsigned[1]{0};
        scene.mNumMeshes = 1; scene.mMeshes = new aiMesh*[1]{new aiMesh};
        auto* mesh = scene.mMeshes[0];
        mesh->mNumVertices = 1; mesh->mVertices = new aiVector3D[1]{{1,0,0}};
        mesh->mNormals = new aiVector3D[1]{{0,1,0}};
        pose.scene = &scene;
    }
    void animate() {
        scene.mNumAnimations = 1; scene.mAnimations = new aiAnimation*[1]{new aiAnimation};
        auto* clip = scene.mAnimations[0]; clip->mDuration = 2; clip->mTicksPerSecond = 1;
        clip->mNumChannels = 1; clip->mChannels = new aiNodeAnim*[1]{new aiNodeAnim};
        auto* channel = clip->mChannels[0]; channel->mNodeName = aiString("bone");
        channel->mNumPositionKeys = 2; channel->mPositionKeys = new aiVectorKey[2];
        channel->mPositionKeys[0] = aiVectorKey(0, {0,0,0});
        channel->mPositionKeys[1] = aiVectorKey(2, {4,0,0});
    }
};
}
TEST(ModelImport, AppliesHierarchyAndScaleWithoutGpu) {
    fixture f; f.pose.options.scale = 3; f.pose.collect(f.scene.mRootNode); f.pose.evaluate(0);
    ASSERT_EQ(f.pose.instances.size(), 1u);
    EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 9);
}
TEST(ModelImport, FiltersMeshesAndNodeSubtrees) {
    fixture f;
    f.pose.options.excluded_nodes.insert("0/0");
    EXPECT_THROW(commons::validate_model_selection(f.scene, f.pose.options), std::runtime_error);
    f.pose.collect(f.scene.mRootNode); EXPECT_TRUE(f.pose.instances.empty());
    f.pose.options.excluded_nodes.clear(); f.pose.options.excluded_meshes.insert(0);
    EXPECT_THROW(commons::validate_model_selection(f.scene, f.pose.options), std::runtime_error);
}
TEST(ModelImport, InterpolatesAndLoopsNodeAnimation) {
    fixture f; f.animate(); f.pose.collect(f.scene.mRootNode);
    f.pose.evaluate(.5); EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 2);
    f.pose.evaluate(2.5); EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 2);
    f.pose.options.excluded_animations.insert(0);
    f.pose.evaluate(.5); EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 3);
}
TEST(ModelImport, EvaluatesSkinWeightsAndBoneOffsets) {
    fixture f; f.animate(); auto* mesh = f.scene.mMeshes[0];
    mesh->mNumBones = 1; mesh->mBones = new aiBone*[1]{new aiBone};
    auto* bone = mesh->mBones[0]; bone->mName = aiString("bone");
    bone->mOffsetMatrix.a4 = -1;
    bone->mNumWeights = 1; bone->mWeights = new aiVertexWeight[1]{{0, 1}};
    f.pose.collect(f.scene.mRootNode); f.pose.evaluate(.5);
    EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 1);
    f.pose.options.skins = false;
    EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 2);
    f.pose.options.skeleton = false;
    f.pose.evaluate(.5);
    EXPECT_FLOAT_EQ(f.pose.vertices(f.pose.instances[0]).positions[0].x, 3);
}
TEST(ModelImport, SettingsRoundTripAndRejectCorruption) {
    const auto dir = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) / ("bengine-model-settings-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct cleanup { std::filesystem::path p; ~cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    const auto file = dir / "model.glb";
    commons::model_import_options o; o.scale = 2; o.skins = false; o.skeleton = false;
    o.excluded_nodes.insert("0/1"); o.excluded_meshes.insert(3); o.excluded_animations.insert(2);
    const auto settings = commons::model_import_settings_path(file);
    editor::ui::project_files::stage({settings, commons::serialize_model_import_options(o)});
    editor::ui::project_files::replace(settings);
    auto loaded = commons::read_model_import_options(file);
    EXPECT_EQ(loaded.excluded_nodes, o.excluded_nodes); EXPECT_EQ(loaded.excluded_meshes, o.excluded_meshes);
    EXPECT_EQ(loaded.excluded_animations, o.excluded_animations); EXPECT_DOUBLE_EQ(loaded.scale, 2); EXPECT_FALSE(loaded.skins); EXPECT_FALSE(loaded.skeleton);
    std::ofstream(settings) << "{broken";
    EXPECT_THROW(commons::read_model_import_options(file), std::runtime_error);
    std::ofstream(settings) << "{\"version\":1,\"scale\":-1}";
    EXPECT_THROW(commons::read_model_import_options(file), std::runtime_error);
}
TEST(ModelImport, TextureFilteringSettingsRoundTripAndCompatibility) {
    const auto dir = std::filesystem::temp_directory_path() / ("bengine-filter-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct cleanup { std::filesystem::path p; ~cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    const auto model = dir / "model.obj";
    const auto settings = commons::model_import_settings_path(model);
    EXPECT_EQ(commons::read_model_import_options(model).filtering, commons::texture_filter::original);
    std::ofstream(settings) << R"({"version":1})";
    EXPECT_EQ(commons::read_model_import_options(model).filtering, commons::texture_filter::original);
    for (auto filter : {commons::texture_filter::original, commons::texture_filter::bilinear,
                        commons::texture_filter::trilinear}) {
        commons::model_import_options options;
        options.filtering = filter;
        std::ofstream(settings) << commons::serialize_model_import_options(options);
        EXPECT_EQ(commons::read_model_import_options(model).filtering, filter);
    }
    for (auto invalid : {R"({"version":1,"texture_filter":2})",
                         R"({"version":1,"texture_filter":"invalid"})"}) {
        std::ofstream(settings) << invalid;
        EXPECT_THROW(commons::read_model_import_options(model), std::runtime_error);
    }
}
TEST(ModelImport, DetectsFormatsFromInstalledAssimp) {
    EXPECT_TRUE(commons::supports_model_import("a.GLB"));
    EXPECT_TRUE(commons::supports_model_import("a.fbx"));
    EXPECT_TRUE(commons::supports_model_import("a.obj"));
    EXPECT_TRUE(commons::supports_model_import("a.blend"));
    EXPECT_FALSE(commons::supports_model_import("a.unknownformat"));
}
TEST(ModelImport, DetectsWhenItsSourceAssetIsDeletedAndRestored) {
    const auto dir = std::filesystem::temp_directory_path() / ("bengine-model-source-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    struct cleanup { std::filesystem::path p; ~cleanup() { std::filesystem::remove_all(p); } } cleanup{dir};
    const auto source = dir / "model.obj";
    std::ofstream(source) << "source";

    commons::model model;
    model.relocate_source_path(source);
    EXPECT_TRUE(model.source_available());
    std::filesystem::remove(source);
    EXPECT_FALSE(model.source_available());
    std::ofstream(source) << "restored";
    EXPECT_TRUE(model.source_available());
}

namespace {
template<class T, class Predicate>
T* find_import_element(bgui::element& root, Predicate predicate) {
    if (auto* item = dynamic_cast<T*>(&root); item && predicate(*item)) return item;
    if (auto* layout = root.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* item = find_import_element<T>(*child, predicate)) return item;
    return nullptr;
}
class ImportDialogTest : public testing::Test {
protected:
    bgui::scoped_interface ui_scope;
    std::filesystem::path folder = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) / ("bengine-import-ui-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { std::filesystem::create_directories(folder); bgui::get_context().m_size = {1200,800}; }
    void TearDown() override { std::filesystem::remove_all(folder); }
    void triangle() { std::ofstream(folder / "test.obj") << "o triangle\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"; }
    bgui::button* button(const std::string& label) {
        return find_import_element<bgui::button>(bgui::get_layout(), [&](auto& b) { return b.get_label().get_buffer() == label; });
    }
};
}
TEST_F(ImportDialogTest, ImportsSettingsAndCompletesOutsideClickDispatch) {
    triangle(); editor::ui::model_import_dialog dialog;
    bool complete = false;
    dialog.show(folder / "test.obj", folder, {}, {}, [&](const auto& p) { complete = p == folder / "test.obj"; });
    ASSERT_TRUE(dialog.open());
    auto* import = button("Importar / Reimportar"); ASSERT_NE(import, nullptr);
    import->on_pressed(); import->on_released();
    bgui::on_update();
    EXPECT_TRUE(complete); EXPECT_FALSE(dialog.open());
    EXPECT_TRUE(std::filesystem::exists(commons::model_import_settings_path(folder / "test.obj")));
}
TEST_F(ImportDialogTest, PersistsSelectedTextureFiltering) {
    triangle();
    editor::ui::model_import_dialog dialog;
    dialog.show(folder / "test.obj", folder);
    auto* filter = button("Trilinear");
    ASSERT_NE(filter, nullptr);
    filter->on_pressed(); filter->on_released();
    auto* import = button("Importar / Reimportar");
    ASSERT_NE(import, nullptr);
    import->on_pressed(); import->on_released();
    bgui::on_update();
    EXPECT_EQ(commons::read_model_import_options(folder / "test.obj").filtering,
              commons::texture_filter::trilinear);
}
TEST_F(ImportDialogTest, FailedImportShowsDiagnosticAndAllowsRetry) {
    std::ofstream(folder / "test.obj") << "invalid";
    editor::ui::model_import_dialog dialog;
    dialog.show(folder / "test.obj", folder, {}, "Falha de importação");
    EXPECT_TRUE(dialog.open()); EXPECT_EQ(button("Importar / Reimportar"), nullptr);
    auto* retry = button("Tentar novamente"); ASSERT_NE(retry, nullptr);
    triangle(); retry->on_pressed(); retry->on_released(); bgui::on_update();
    EXPECT_NE(button("Importar / Reimportar"), nullptr);
    EXPECT_FALSE(std::filesystem::exists(commons::model_import_settings_path(folder / "test.obj")));
}
TEST_F(ImportDialogTest, InvalidScaleKeepsDialogOpenAndPreservesSettings) {
    triangle(); editor::ui::model_import_dialog dialog; dialog.show(folder / "test.obj", folder);
    auto* scale = find_import_element<bgui::inputbox>(bgui::get_layout(), [](auto&) { return true; });
    ASSERT_NE(scale, nullptr); scale->set_buffer("nan");
    auto* import = button("Importar / Reimportar"); ASSERT_NE(import, nullptr);
    import->on_pressed(); import->on_released(); bgui::on_update();
    EXPECT_TRUE(dialog.open());
    EXPECT_FALSE(std::filesystem::exists(commons::model_import_settings_path(folder / "test.obj")));
}

#include <assimp/Exporter.hpp>
#include <GLFW/glfw3.h>
#include "os/window.hpp"
#include "loaders/model.hpp"

TEST(ModelImportGpuTest, LoadsEmbeddedGlbTextureAndAnimatesThroughExistingRenderer) {
    const auto folder = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) / ("bengine-glb-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    struct cleanup { std::filesystem::path p; ~cleanup() { std::filesystem::remove_all(p); } } cleanup{folder};
    if (!test_support::supports_opengl33()) GTEST_SKIP() << "No display for GPU upload";
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    commons::window window("Model import", 100, 100);
    fixture f;
    f.animate();
    auto* mesh = f.scene.mMeshes[0];
    delete[] mesh->mVertices; delete[] mesh->mNormals;
    mesh->mNumVertices = 3;
    mesh->mVertices = new aiVector3D[3]{{0,0,0},{1,0,0},{0,1,0}};
    mesh->mNormals = new aiVector3D[3]{{0,0,1},{0,0,1},{0,0,1}};
    mesh->mTextureCoords[0] = new aiVector3D[3]{{0,0,0},{1,0,0},{0,1,0}}; mesh->mNumUVComponents[0] = 2;
    mesh->mNumFaces = 1; mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3; mesh->mFaces[0].mIndices = new unsigned[3]{0,1,2};
    mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
    f.scene.mNumMaterials = 1; f.scene.mMaterials = new aiMaterial*[1]{new aiMaterial};
    aiString reference("*0"); f.scene.mMaterials[0]->AddProperty(&reference, AI_MATKEY_TEXTURE_DIFFUSE(0));
    f.scene.mNumTextures = 1; f.scene.mTextures = new aiTexture*[1]{new aiTexture};
    const auto image = std::filesystem::path(__FILE__).parent_path().parent_path() / "external/cpp-bgui/tests/assets/rgb.jpg";
    std::ifstream input(image, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    ASSERT_FALSE(bytes.empty());
    auto* texture = f.scene.mTextures[0]; texture->mWidth = bytes.size();
    texture->pcData = new aiTexel[(bytes.size() + sizeof(aiTexel)-1) / sizeof(aiTexel)];
    std::memcpy(texture->pcData, bytes.data(), bytes.size());
    std::strcpy(texture->achFormatHint, "jpg");
    const auto path = folder / "animated.glb";
    Assimp::Exporter exporter;
    ASSERT_EQ(exporter.Export(&f.scene, "glb2", path.string()), AI_SUCCESS) << exporter.GetErrorString();
    commons::model_import_options options;
    options.filtering = commons::texture_filter::trilinear;
    std::ofstream(commons::model_import_settings_path(path)) << commons::serialize_model_import_options(options);
    commons::model model(path.string());
    ASSERT_EQ(model.meshes.size(), 1u);
    const auto material = model.meshes[0].getMaterial();
    auto tex = material.getTextura("tex_albedo");
    ASSERT_NE(tex.embedded, nullptr);
    EXPECT_EQ(tex.filtering, commons::texture_filter::trilinear);
    EXPECT_EQ(tex.embedded->m_size, (bgui::vec2{3,2}));
    tex.bind(0);
    GLint width = 0; glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    EXPECT_EQ(width, 3);
    GLint original_min = 0, original_mag = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &original_min);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &original_mag);
    static GLint expected_min;
    static bool draw_called;
    const auto original_draw = glad_glDrawElements;
    struct restore_draw { PFNGLDRAWELEMENTSPROC fn; ~restore_draw() { glad_glDrawElements = fn; } } restore{original_draw};
    glad_glDrawElements = [](GLenum, GLsizei, GLenum, const void*) {
        draw_called = true;
        GLint min_filter = 0, mag_filter = 0, mip_width = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
        EXPECT_EQ(min_filter, expected_min);
        EXPECT_EQ(mag_filter, GL_LINEAR);
        if (expected_min == GL_LINEAR_MIPMAP_LINEAR) {
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &mip_width);
            EXPECT_GT(mip_width, 0);
        }
    };
    for (const auto filter : {commons::texture_filter::bilinear, commons::texture_filter::trilinear}) {
        auto filtered_material = material;
        tex.filtering = filter;
        filtered_material.set_texture("tex_albedo", tex);
        model.meshes[0].setMaterial(filtered_material);
        expected_min = filter == commons::texture_filter::bilinear ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR;
        draw_called = false;
        model.draw();
        EXPECT_TRUE(draw_called);
        GLint restored_min = 0, restored_mag = 0;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &restored_min);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &restored_mag);
        EXPECT_EQ(restored_min, original_min);
        EXPECT_EQ(restored_mag, original_mag);
    }
    model.update_animation(.5);
    EXPECT_NEAR(model.meshes[0].getVertices()[0].position.x, 1, .001);
    EXPECT_THROW(model.set_animation(10), std::out_of_range);
    const auto original = model.get_source_path();
    std::ofstream(commons::model_import_settings_path(path)) << "broken";
    EXPECT_THROW(model.load_model(path.string()), std::runtime_error);
    EXPECT_EQ(model.get_source_path(), original); EXPECT_EQ(model.meshes.size(), 1u);
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ImportDialogTest, CopiesImportSettingsTogetherWithExternalModel) {
    triangle();
    commons::model_import_options options; options.scale = 3; options.skeleton = false;
    const auto source = folder / "test.obj";
    const auto settings = commons::model_import_settings_path(source);
    editor::ui::project_files::stage({settings, commons::serialize_model_import_options(options)});
    editor::ui::project_files::replace(settings);
    const auto project = folder / "project";
    std::filesystem::create_directory(project);
    const auto imported = editor::ui::import_asset(source, project, true);
    EXPECT_NE(imported, source);
    std::filesystem::remove(source); std::filesystem::remove(settings);
    EXPECT_DOUBLE_EQ(commons::read_model_import_options(imported).scale, 3);
    EXPECT_FALSE(commons::read_model_import_options(imported).skeleton);
    Assimp::Importer validation;
    EXPECT_NE(validation.ReadFile(imported.string(), options.flags()), nullptr);
}

TEST(ModelImport, ResolvesSiblingTextureFolderAndPreservesBundle) {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("bengine-sibling-textures-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir / "package/source");
    fs::create_directories(dir / "package/Texture");
    fs::create_directories(dir / "project");
    struct cleanup { fs::path p; ~cleanup() { fs::remove_all(p); } } cleanup{dir};
    const auto source = dir / "package/source/model.obj";
    std::ofstream(source) << "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n";
    std::ofstream(source.parent_path() / "model.mtl") << "newmtl test\nmap_Kd Texture/color.png\n";
    const auto texture = dir / "package/Texture/color.png";
    std::ofstream(texture) << "texture fixture";
    EXPECT_EQ(commons::resolve_model_texture(source, "Texture\\color.png"), texture);
    EXPECT_EQ(commons::resolve_model_texture(source, "C:\\export\\color.png"), texture);
    EXPECT_EQ(commons::resolve_model_texture(source, "../Texture/color.png"), texture);
    const auto imported = editor::ui::import_asset(source, dir / "project", true);
    EXPECT_TRUE(fs::is_regular_file(commons::resolve_model_texture(imported, "Texture/color.png")));
    EXPECT_EQ(editor::ui::import_asset(source, dir / "project", true), imported);
}

TEST(ModelImport, PreviewDrawsGeometryAndRespectsSelection) {
    fixture f;
    auto* mesh = f.scene.mMeshes[0];
    delete[] mesh->mVertices; delete[] mesh->mNormals;
    mesh->mNormals = nullptr;
    mesh->mNumVertices = 3; mesh->mVertices = new aiVector3D[3]{{0,0,0},{1,0,0},{0,1,0}};
    mesh->mNumFaces = 1; mesh->mFaces = new aiFace[1];
    mesh->mFaces[0].mNumIndices = 3; mesh->mFaces[0].mIndices = new unsigned[3]{0,1,2};
    const auto visible = editor::ui::model_import_preview(f.scene, {});
    const auto orbited = editor::ui::model_import_preview(f.scene, {}, 1.2f, .4f);
    const auto zoomed = editor::ui::model_import_preview(f.scene, {}, .65f, .4f, 1.5f);
    commons::model_import_options options; options.excluded_meshes.insert(0);
    const auto hidden = editor::ui::model_import_preview(f.scene, options);
    EXPECT_EQ(visible.m_buffer.size(), 480u * 260u * 4u);
    EXPECT_TRUE(visible.m_has_alpha);
    EXPECT_NE(visible.m_buffer, orbited.m_buffer);
    EXPECT_NE(visible.m_buffer, zoomed.m_buffer);
    EXPECT_NE(visible.m_buffer, hidden.m_buffer);
}
