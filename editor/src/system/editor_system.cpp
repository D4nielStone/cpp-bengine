#include "system/editor_system.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "elem/menu_bar.hpp"

#include <bgui.hpp>
#include <elem/input_area.hpp>
#include <rapidjson/prettywriter.h>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

namespace {
    std::string choose_file(const bool save, const bool model, const bool project = false) {
        const char* command = model
            ? "zenity --file-selection --title='Importar modelo 3D' --file-filter='Modelos 3D | *.obj *.dae *.fbx *.gltf *.glb *.stl *.ply' 2>/dev/null"
            : project
                ? "zenity --file-selection --title='Abrir projeto' --file-filter='Projeto Bubble | *.bproject' 2>/dev/null"
            : save
                ? "zenity --file-selection --save --confirm-overwrite --title='Salvar cena' --file-filter='Cenas | *.bscene' 2>/dev/null"
                : "zenity --file-selection --title='Importar cena' --file-filter='Cenas | *.bscene' 2>/dev/null";
        std::FILE* dialog = popen(command, "r");
        if (!dialog)
            return {};

        std::string path;
        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), dialog))
            path += buffer.data();
        const int result = pclose(dialog);
        if (result != 0)
            return {};
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
        return path;
    }

    std::string choose_project_directory() {
        std::FILE* dialog = popen("zenity --file-selection --directory --title='Criar projeto Bubble' 2>/dev/null", "r");
        if (!dialog)
            return {};
        std::string path;
        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), dialog))
            path += buffer.data();
        const int result = pclose(dialog);
        if (result != 0)
            return {};
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
        return path;
    }

    std::string normalized_key(const std::string& key) {
        std::string normalized;
        for (const unsigned char character : key) {
            if (std::isalnum(character))
                normalized.push_back(static_cast<char>(std::tolower(character)));
        }
        return normalized;
    }

    const rapidjson::Value* member_value(
        const rapidjson::Value& object,
        const std::string& normalized_name)
    {
        if (!object.IsObject())
            return nullptr;
        for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member) {
            if (normalized_key(member->name.GetString()) == normalized_name)
                return &member->value;
        }
        return nullptr;
    }

    bool read_vector(const rapidjson::Value& object, const char* key, COMMONS_NS::fvec3& result) {
        const auto* value = member_value(object, key);
        if (!value)
            return false;
        if (value->IsArray() && value->Size() >= 3 &&
            (*value)[0].IsNumber() && (*value)[1].IsNumber() && (*value)[2].IsNumber()) {
            result = {(*value)[0].GetFloat(), (*value)[1].GetFloat(), (*value)[2].GetFloat()};
            return true;
        }
        if (value->IsObject()) {
            const auto* x = member_value(*value, "x");
            const auto* y = member_value(*value, "y");
            const auto* z = member_value(*value, "z");
            if (x && y && z && x->IsNumber() && y->IsNumber() && z->IsNumber()) {
                result = {x->GetFloat(), y->GetFloat(), z->GetFloat()};
                return true;
            }
        }
        return false;
    }

    const rapidjson::Value* scene_transform(
        const rapidjson::Value& object,
        const rapidjson::Value* inherited)
    {
        if (const auto* components = member_value(object, "components")) {
            if (const auto* transform = member_value(*components, "transform"))
                return transform;
        }
        if (const auto* transform = member_value(object, "transform"))
            return transform;
        if (member_value(object, "position") || member_value(object, "rotation") || member_value(object, "scale"))
            return &object;
        return inherited;
    }

    bool is_ignored_gameplay_object(const rapidjson::Value& object) {
        for (const char* key : {"type", "kind", "category", "class"}) {
            const auto* value = member_value(object, key);
            if (!value || !value->IsString())
                continue;
            const auto type = normalized_key(value->GetString());
            if (type == "player" || type == "enemy" || type == "enemyarea" || type == "area")
                return true;
        }
        return false;
    }

    std::string model_path(const rapidjson::Value& object, const bool model_context) {
        for (const char* key : {"model", "modelpath", "mesh", "meshpath", "modelfile"}) {
            const auto* value = member_value(object, key);
            if (value && value->IsString())
                return value->GetString();
        }
        if (const auto* renderer = member_value(object, "renderer")) {
            if (renderer->IsString())
                return renderer->GetString();
            if (renderer->IsObject()) {
                for (const char* key : {"model", "modelpath", "mesh", "meshpath", "path", "file", "directory"}) {
                    const auto* value = member_value(*renderer, key);
                    if (value && value->IsString())
                        return value->GetString();
                }
            }
        }
        if (model_context) {
            for (const char* key : {"path", "file", "directory", "source"}) {
                const auto* value = member_value(object, key);
                if (value && value->IsString())
                    return value->GetString();
            }
        }
        return {};
    }

    std::string relative_asset_path(const std::filesystem::path& path) {
        return path.lexically_relative(std::filesystem::path(COMMONS_ASSET_DIR)).generic_string();
    }

    void import_models(
        const rapidjson::Value& value,
        const rapidjson::Value* inherited_transform,
        const bool model_context,
        const std::filesystem::path& scene_directory,
        const std::filesystem::path& project_assets,
        const std::shared_ptr<COMMONS_NS::ecs>& registry,
        std::size_t& imported)
    {
        if (value.IsArray()) {
            for (const auto& child : value.GetArray())
                import_models(child, inherited_transform, model_context, scene_directory, project_assets, registry, imported);
            return;
        }
        if (!value.IsObject() || is_ignored_gameplay_object(value))
            return;

        const auto* transform = scene_transform(value, inherited_transform);
        const auto path = model_path(value, model_context);
        if (!path.empty()) {
            std::filesystem::path resolved_model(path);
            std::error_code error;
            if (resolved_model.is_relative()) {
                const auto relative_to_assets = std::filesystem::path(COMMONS_ASSET_DIR) / resolved_model;
                if (std::filesystem::is_regular_file(relative_to_assets, error)) {
                    resolved_model = relative_to_assets;
                } else if (!project_assets.empty() && std::filesystem::is_regular_file(project_assets / resolved_model, error)) {
                    resolved_model = project_assets / resolved_model;
                } else if (!std::filesystem::is_regular_file(resolved_model, error)) {
                    const auto relative_to_scene = scene_directory / resolved_model;
                    if (std::filesystem::is_regular_file(relative_to_scene, error))
                        resolved_model = relative_to_scene;
                }
            }

            auto entity = registry->create();
            registry->add<COMMONS_NS::renderer>(entity, resolved_model.string().c_str());
            auto renderer = registry->get<COMMONS_NS::renderer>(entity.id);
            if (renderer && renderer->m_modelo && !renderer->m_modelo->meshes.empty()) {
                if (transform) {
                    auto entity_transform = registry->get<COMMONS_NS::transform>(entity.id);
                    auto position = entity_transform->get_position();
                    auto rotation = entity_transform->get_rotation();
                    auto scale = entity_transform->get_scale();
                    read_vector(*transform, "position", position);
                    read_vector(*transform, "rotation", rotation);
                    read_vector(*transform, "scale", scale);
                    entity_transform->set_position(position);
                    entity_transform->set_rotation(rotation);
                    entity_transform->set_scale(scale);
                }
                ++imported;
            } else {
                registry->remove(entity.id);
            }
        }

        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const auto key = normalized_key(member->name.GetString());
            const bool child_model_context = model_context || key == "models" || key == "model" ||
                key == "meshes" || key == "mesh" || key == "renderer";
            import_models(member->value, transform, child_model_context, scene_directory, project_assets, registry, imported);
        }
    }
}

editor::editor_system::~editor_system() {
    bgui::save_configuration("editor.cfg");
}

void editor::editor_system::setup(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());
    bgui::set_global_scale(0.8f);

    auto& root = bgui::set_layout<bgui::linear>(bgui::orientation::vertical);
    auto& menu_bar = root.add_persistent<bgui::menu_bar>(root);
    auto& config = menu_bar.add_button("[ Config ]");
    config.add_button("Editor Camera", [this]() {
        open_editor_camera_settings();
    });
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = registry;
    menu_bar.add_menu("[ Salvar cena ]", [this, weak_registry]() {
        open_scene_file_dialog(true, weak_registry.lock());
    });
    menu_bar.add_menu("[ Novo Projeto ]", [this]() {
        const auto directory = choose_project_directory();
        if (!directory.empty())
            create_project(directory);
    });
    menu_bar.add_menu("[ Abrir Projeto ]", [this]() {
        const auto path = choose_file(false, false, true);
        if (!path.empty())
            open_project(path);
    });
    menu_bar.add_menu("[ Importar cena]", [this, weak_registry]() {
        open_scene_file_dialog(false, weak_registry.lock());
    });

    auto& dock = root.add_persistent<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    auto& scene_window = dock.add_window("Editor View Window", bgui::dock_area::center);
    auto& entities_window = dock.add_window("Entities", bgui::dock_area::left);
    auto& components_window = dock.add_window("Components", bgui::dock_area::right);
    auto& assets_window = dock.add_window("Assets Window", bgui::dock_area::right);
    bgui::load_configuration("editor.cfg");

    if (registry) {
        auto editor_camera_entity = registry->create();
        m_editor_camera_entity = editor_camera_entity.id;
        registry->add<COMMONS_NS::camera>(editor_camera_entity);
        if (auto camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity))
            camera_transform->set_rotation(COMMONS_NS::fvec3{0.f, 90.f, 0.f});
        m_camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
        if (auto camera = m_camera.lock())
            camera->createFB();
    }

    setup_scene_view_panel(scene_window);
    setup_entities_panel(entities_window);
    setup_components_panel(components_window);
    setup_assets_panel(assets_window, registry);

    refresh_scene(registry);
    bgui::cascade_style();
    bgui::load_font_queue();
}

void editor::editor_system::update(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry)
        return;

    refresh_scene(registry);
    if (!m_framebuffer_image)
        return;

    if (m_editor_settings) {
        const auto& elements = bgui::get_layout().get_elements();
        const auto overlays = elements.find(bgui::layer::overlay);
        const bool settings_attached = overlays != elements.end() &&
            std::any_of(overlays->second.begin(), overlays->second.end(), [this](const auto& element) {
                return element.get() == m_editor_settings;
            });
        if (!settings_attached)
            m_editor_settings = nullptr;
    }

    if (m_camera.expired()) {
        registry->cada<COMMONS_NS::camera>([&](const uint32_t entity) {
            if (entity == m_editor_camera_entity || !m_camera.expired())
                return;
            if (auto camera = registry->get<COMMONS_NS::camera>(entity))
                m_camera = camera;
        });
    }

    update_scene_view_panel(registry);
}

void editor::editor_system::refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry || !m_entities_list || !m_components_list)
        return;

    std::vector<std::pair<uint32_t, uint32_t>> signature;
    signature.reserve(registry->entities.size());
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
        if (entity_id == m_editor_camera_entity)
            continue;
        signature.emplace_back(entity_id, static_cast<uint32_t>(registry->get_components(entity_id)));
    }
    if (m_scene_initialized && signature == m_scene_signature)
        return;

    m_scene_signature = std::move(signature);
    m_scene_initialized = true;
    const bool selected_exists = std::any_of(
        m_scene_signature.begin(), m_scene_signature.end(),
        [this](const auto& entry) { return entry.first == m_selected_entity; }
    );
    if (!selected_exists)
        m_selected_entity = m_scene_signature.empty() ? 0 : m_scene_signature.front().first;

    rebuild_entities(registry);
    rebuild_components(registry);
}

void editor::editor_system::open_scene_file_dialog(
    const bool save,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry)
        return;
    if (!m_scene_file_dialog)
        create_scene_file_dialog();

    m_scene_file_save = save;
    m_scene_file_registry = registry;
    m_scene_file_dialog->set_title(save ? "Salvar .bscene" : "Importar .bscene");
    m_scene_file_input->set_buffer(save ? "scene.bscene" : "");
    m_scene_file_status->set_buffer("Informe o caminho do arquivo .bscene.");
    const auto size = bgui::get_context_size();
    m_scene_file_dialog->set_position(
        std::max(0, (size.x - m_scene_file_dialog->processed_width()) / 2),
        std::max(0, (size.y - m_scene_file_dialog->processed_height()) / 2)
    );
    m_scene_file_dialog->set_enable(true);
    m_scene_file_dialog->set_flex(false);
}

void editor::editor_system::create_scene_file_dialog() {
    auto& dialog = bgui::get_layout().add_persistent<bgui::window, bgui::layer::overlay>("Arquivo de cena");
    m_scene_file_dialog = &dialog;
    dialog.style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
    dialog.style.layout.require_size(460.f, 190.f);

    auto& input = dialog.add_persistent<bgui::input_area>("", 0.35f, [this](const std::string) {
        apply_scene_file_path(m_scene_file_input->get_buffer());
    }, "Caminho do arquivo .bscene");
    input.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    m_scene_file_input = &input;

    auto& browse = dialog.add_persistent<bgui::button>("Procurar...", 0.35f, [this]() {
        const auto path = choose_file(m_scene_file_save, false);
        if (!path.empty() && m_scene_file_input)
            m_scene_file_input->set_buffer(path);
    });
    browse.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);

    m_scene_file_status = &dialog.add_persistent<bgui::text>("", 0.32f);
    m_scene_file_status->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);

    auto& actions = dialog.add_persistent<bgui::linear>(bgui::orientation::horizontal);
    actions.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& confirm = actions.add_persistent<bgui::button>("Confirmar", 0.35f, [this]() {
        if (m_scene_file_input)
            apply_scene_file_path(m_scene_file_input->get_buffer());
    });
    confirm.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
    auto& cancel = actions.add_persistent<bgui::button>("Cancelar", 0.35f, [this]() {
        if (m_scene_file_dialog)
            m_scene_file_dialog->set_enable(false);
    });
    cancel.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);

    dialog.set_enable(false);
}

void editor::editor_system::apply_scene_file_path(const std::string& path) {
    const auto registry = m_scene_file_registry.lock();
    if (!registry) {
        m_scene_file_status->set_buffer("A cena não está mais disponível.");
        return;
    }
    if (path.empty()) {
        m_scene_file_status->set_buffer("Informe um caminho válido.");
        return;
    }

    std::filesystem::path scene_path(path);
    if (scene_path.extension() != ".bscene") {
        if (!m_scene_file_save) {
            m_scene_file_status->set_buffer("Selecione um arquivo com extensão .bscene.");
            return;
        }
        scene_path += ".bscene";
    }

    try {
        if (m_scene_file_save) {
            if (!save_scene_file(scene_path.string(), registry)) {
                m_scene_file_status->set_buffer("Não foi possível salvar o arquivo.");
                return;
            }
            m_scene_file_status->set_buffer("Cena salva: " + scene_path.string());
        } else {
                if (m_project_config_path.empty()) {
                    m_scene_file_status->set_buffer("Crie ou abra um projeto antes de importar cenas.");
                    return;
                }
                const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
                const auto destination = project_root / "Scenes" / scene_path.filename();
                std::error_code error;
                std::filesystem::create_directories(destination.parent_path(), error);
                if (error || !std::filesystem::is_regular_file(scene_path, error)) {
                    m_scene_file_status->set_buffer("Não foi possível acessar a cena de origem.");
                    return;
                }
                std::filesystem::copy_file(scene_path, destination, std::filesystem::copy_options::overwrite_existing, error);
                if (error) {
                    m_scene_file_status->set_buffer("Não foi possível copiar a cena para o projeto.");
                    return;
                }
                const auto relative_scene = destination.lexically_relative(project_root).generic_string();
                if (std::find(m_project_scenes.begin(), m_project_scenes.end(), relative_scene) == m_project_scenes.end())
                    m_project_scenes.push_back(relative_scene);
                refresh_project_scenes();
                if (!save_project_config()) {
                    m_scene_file_status->set_buffer("Cena importada, mas não foi possível atualizar o projeto.");
                    return;
                }
                m_scene_file_status->set_buffer("Cena adicionada ao projeto: " + scene_path.filename().string());
            }
    } catch (const std::exception& error) {
        m_scene_file_status->set_buffer(std::string("Erro: ") + error.what());
    }
}

void editor::editor_system::create_project(const std::string& directory) {
    if (directory.empty())
        return;

    const auto project_root = std::filesystem::path(directory);
    std::error_code error;
    std::filesystem::create_directories(project_root / "Assets", error);
    std::filesystem::create_directories(project_root / "Scenes", error);
    m_project_name = project_root.filename().string();
    m_project_config_path = (project_root / "project.bproject").string();
    m_project_scenes.clear();

    if (m_project_status)
        m_project_status->set_buffer("Projeto aberto: " + m_project_name);

    save_project_config();
    refresh_project_scenes();
}

void editor::editor_system::open_project(const std::string& path) {
    if (path.empty())
        return;

    const auto project_path = std::filesystem::path(path);
    m_project_config_path = project_path.string();
    m_project_name = project_path.parent_path().filename().string();
    m_project_scenes.clear();

    if (std::filesystem::exists(project_path)) {
        std::ifstream input(project_path, std::ios::binary);
        if (input) {
            std::string contents((std::istreambuf_iterator<char>(input)), {});
            rapidjson::Document document;
            document.Parse(contents.c_str());
            if (!document.HasParseError() && document.IsObject()) {
                const auto name = document.FindMember("name");
                if (name != document.MemberEnd() && name->value.IsString())
                    m_project_name = name->value.GetString();

                const auto scenes = document.FindMember("scenes");
                if (scenes != document.MemberEnd() && scenes->value.IsArray()) {
                    for (const auto& scene : scenes->value.GetArray()) {
                        if (scene.IsString())
                            m_project_scenes.emplace_back(scene.GetString());
                    }
                }
            }
        }
    }

    if (m_project_scenes.empty()) {
        const auto project_root = project_path.parent_path();
        const auto scenes_dir = project_root / "Scenes";
        std::error_code error;
        if (std::filesystem::exists(scenes_dir, error)) {
            for (const auto& entry : std::filesystem::directory_iterator(scenes_dir, error)) {
                if (entry.is_regular_file(error) && entry.path().extension() == ".bscene") {
                    const auto relative = entry.path().lexically_relative(project_root).generic_string();
                    m_project_scenes.push_back(relative);
                }
            }
        }
    }

    if (m_project_status)
        m_project_status->set_buffer("Projeto aberto: " + m_project_name);

    refresh_project_scenes();
}

bool editor::editor_system::save_project_config() {
    if (m_project_config_path.empty())
        return false;

    const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
    std::error_code error;
    std::filesystem::create_directories(project_root, error);

    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();
    document.AddMember("name", rapidjson::Value(m_project_name.c_str(), static_cast<rapidjson::SizeType>(m_project_name.size()), allocator), allocator);

    rapidjson::Value scenes(rapidjson::kArrayType);
    for (const auto& scene : m_project_scenes) {
        const auto normalized = std::filesystem::path(scene).generic_string();
        scenes.PushBack(rapidjson::Value(normalized.c_str(), static_cast<rapidjson::SizeType>(normalized.size()), allocator), allocator);
    }
    document.AddMember("scenes", scenes, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream output(m_project_config_path, std::ios::binary);
    if (!output)
        return false;
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    return output.good();
}

void editor::editor_system::refresh_project_scenes() {
    if (m_project_status) {
        if (m_project_config_path.empty())
            m_project_status->set_buffer("Nenhum projeto aberto");
        else
            m_project_status->set_buffer("Projeto: " + m_project_name);
    }

    if (!m_project_scenes_list)
        return;

    std::vector<std::string> scenes = m_project_scenes;
    std::sort(scenes.begin(), scenes.end());

    for (const auto& scene : scenes) {
        auto& item = m_project_scenes_list->add_persistent<bgui::text>(scene, 0.35f);
        item.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}

void editor::editor_system::import_model_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || path.empty())
        return;

    try {
        auto entity = registry->create();
        registry->add<COMMONS_NS::renderer>(entity, path.c_str());
        const auto renderer = registry->get<COMMONS_NS::renderer>(entity.id);
        if (!renderer || !renderer->m_modelo || renderer->m_modelo->meshes.empty()) {
            registry->remove(entity.id);
            if (m_model_import_status)
                m_model_import_status->set_buffer("Não foi possível carregar esse modelo.");
            return;
        }

        m_selected_entity = entity.id;
        m_scene_initialized = false;
        refresh_scene(registry);
        if (m_model_import_status)
            m_model_import_status->set_buffer("Modelo importado: " + std::filesystem::path(path).filename().string());
    } catch (const std::exception& error) {
        if (m_model_import_status)
            m_model_import_status->set_buffer(std::string("Erro: ") + error.what());
    }
}

void editor::editor_system::browse_model_file(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    const auto path = choose_file(false, true);
    if (!path.empty())
        import_model_file(path, registry);
}

bool editor::editor_system::save_scene_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();
    document.AddMember("format", rapidjson::Value("cpp-bengine-scene", allocator), allocator);
    document.AddMember("version", 1, allocator);
    rapidjson::Value entities(rapidjson::kArrayType);

    for (const auto& [entity_id, components] : registry->entities) {
        if (entity_id == m_editor_camera_entity)
            continue;
        const auto render_component = registry->get<COMMONS_NS::renderer>(entity_id);
        const auto transform_component = registry->get<COMMONS_NS::transform>(entity_id);
        if (!render_component || !render_component->m_modelo || !transform_component)
            continue;

        rapidjson::Value entity(rapidjson::kObjectType);
        rapidjson::Value serialized_components(rapidjson::kObjectType);
        rapidjson::Value serialized_transform(rapidjson::kObjectType);
        rapidjson::Value serialized_renderer(rapidjson::kObjectType);
        transform_component->serialize(serialized_transform, allocator);
        if (!render_component->serialize(serialized_renderer, allocator))
            continue;
        const auto model_member = serialized_renderer.FindMember("model");
        if (model_member != serialized_renderer.MemberEnd()) {
            const auto relative_path = relative_asset_path(render_component->m_modelo->get_source_path());
            model_member->value.SetString(relative_path.c_str(), static_cast<rapidjson::SizeType>(relative_path.size()), allocator);
        }
        serialized_components.AddMember("transform", serialized_transform, allocator);
        serialized_components.AddMember("renderer", serialized_renderer, allocator);
        entity.AddMember("components", serialized_components, allocator);
        entities.PushBack(entity, allocator);
    }
    document.AddMember("entities", entities, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    std::ofstream output(path, std::ios::binary);
    if (!output)
        return false;
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    return output.good();
}

std::size_t editor::editor_system::import_scene_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("não foi possível abrir " + path);
    const std::string contents(std::istreambuf_iterator<char>(input), {});
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError())
        throw std::runtime_error("o arquivo não contém JSON válido");

    std::size_t imported = 0;
    const auto project_assets = m_project_config_path.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(m_project_config_path).parent_path() / "Assets";
    import_models(document, nullptr, false, std::filesystem::path(path).parent_path(), project_assets, registry, imported);
    return imported;
}