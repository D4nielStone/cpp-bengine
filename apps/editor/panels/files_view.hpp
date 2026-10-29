#pragma once

#include "panel.hpp"
#include "../systems/script_tools.hpp"
#include "../systems/project_file_transaction.hpp"
#include "lua_inputbox.hpp"

#include <bgui.hpp>
#include <elem/button.hpp>
#include <elem/image.hpp>
#include <elem/inputbox.hpp>
#include <elem/text.hpp>
#include <elem/tree.hpp>
#include <elem/window.hpp>
#include <os/asset_manager.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>
#include <os/os.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iomanip>
#include <memory>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::ui {

class files_view final : public panel {
public:
    using drop_callback =
        std::function<void(const std::filesystem::path&, const bgui::vec2i&)>;

    static bool supports_model(const std::filesystem::path& path) {
        return is_model_file(path);
    }

    explicit files_view(std::filesystem::path project_file, drop_callback on_drop = {})
        : m_project_root(resolve_project_root(std::move(project_file))),
          m_on_drop(std::move(on_drop)),
          m_current_directory(m_project_root) {
        m_expanded_directories.insert(m_project_root);
        load_main_scene();
    }

    files_view(const files_view&) = delete;
    files_view& operator=(const files_view&) = delete;

    void build(bgui::dock& parent) override {
        if (m_root)
            return;

        auto& window = parent.add_window(
            "Arquivos",
            bgui::dock_area::bottom,
            0.3f
        );
        auto& browser = window.add<frame_layout>(bgui::orientation::horizontal);
        browser.after_update = [this]() { update(); };
        m_root = &browser;
        m_root->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_root->style.layout.padding = bgui::vec4i{0};
        m_root->add_class("editor-panel");
        m_dock = &parent;

        m_folder_sidebar = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_folder_sidebar->style.layout.require_mode(
            bgui::mode::percent,
            bgui::mode::match_parent
        );
        m_folder_sidebar->style.layout.require_width(
            bgui::mode::percent,
            29.f
        );
        m_folder_sidebar->add_class("entities-list");
        m_folder_sidebar->style.layout.padding = bgui::vec4i{6, 5, 6, 5};
        m_folder_sidebar->style.visual.background.normal =
            bgui::color{0.075f, 0.075f, 0.075f, 1.f};
        m_folder_sidebar->set_scrollable(true);

        m_file_area = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_file_area->style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::match_parent
        );
        m_file_area->style.layout.padding = bgui::vec4i{8, 6, 8, 6};
        m_file_area->style.visual.background.normal =
            bgui::color{0.12f, 0.125f, 0.14f, 1.f};
        m_file_area->set_scrollable(true);

        m_refresh_requested = true;
        refresh_if_needed();
    }

    void update() override {
        update_document_editor();
        refresh_if_needed();
    }

    void set_project_file(const std::filesystem::path& project_file) {
        for (auto& [path, doc] : m_documents)
            if (doc.window && m_dock) m_dock->remove_window(doc.window);
        m_documents.clear();
        m_active_document = nullptr;
        m_document_window = nullptr;
        m_document_body = nullptr;
        m_document_status = nullptr;
        m_active_content.clear();
        m_project_root = resolve_project_root(project_file);
        m_current_directory = m_project_root;
        m_selected_file.clear();
        m_main_scene.clear();
        m_expanded_directories.clear();
        m_expanded_directories.insert(m_project_root);
        load_main_scene();
        request_refresh();
    }

    // Documents must exist before BGUI restores their docking and active tabs.
    bool save_session(const std::filesystem::path& path) {
        update_document_editor();
        std::ostringstream data;
        data << "bengine-session-1\n" << bgui::get_global_scale() << '\n' << std::quoted(m_current_directory.string()) << '\n'
             << std::quoted(m_active_document ? m_active_document->path.string() : "") << '\n';
        for (const auto& [file, doc] : m_documents) {
            if (!doc.window) continue;
            const bool dirty = doc.buffer != doc.original;
            data << std::quoted(file.string()) << ' ' << dirty << ' '
                 << std::quoted(dirty ? doc.buffer : "") << '\n';
        }
        try {
            project_files::stage({path, data.str()});
            project_files::replace(path);
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    void restore_session(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::string header, directory, active;
        float scale = 1.f;
        if (!std::getline(input, header) || header != "bengine-session-1" ||
            !(input >> scale >> std::quoted(directory) >> std::quoted(active)) ||
            !std::isfinite(scale) || scale < .5f || scale > 2.f) return;
        struct entry { std::string file; bool dirty; std::string buffer; };
        std::vector<entry> entries;
        while (input >> std::ws && input.peek() != std::char_traits<char>::eof()) {
            entry value;
            if (!(input >> std::quoted(value.file) >> value.dirty >> std::quoted(value.buffer))) return;
            entries.push_back(std::move(value));
        }
        bgui::set_global_scale(scale);
        std::error_code error;
        if (std::filesystem::is_directory(directory, error)) m_current_directory = directory;
        for (const auto& value : entries) {
            if (!std::filesystem::is_regular_file(value.file, error)) continue;
            open_document(value.file);
            auto found = m_documents.find(value.file);
            if (found != m_documents.end() && value.dirty && found->second.editor) {
                found->second.buffer = value.buffer;
                found->second.editor->set_buffer(value.buffer);
                found->second.window->set_title(found->second.path.filename().string() + "*");
            }
        }
        if (auto found = m_documents.find(active); found != m_documents.end() && found->second.window)
            activate_document(active);
        request_refresh();
    }

    void set_asset_status(std::string message) {
        m_asset_status = std::move(message);
        request_refresh();
    }

private:
    class frame_layout final : public bgui::linear {
    public:
        using bgui::linear::linear;
        std::function<void()> after_update;
        void on_update() override {
            bgui::linear::on_update();
            if (after_update) after_update();
        }
    };

    class file_button final : public bgui::button {
    public:
        file_button(
            const std::string& label,
            std::function<void()> on_click,
            std::function<void(const bgui::vec2i&)> on_drop
        )
            : bgui::button(label, 0.28f, on_click),
              m_on_drop(std::move(on_drop)) {}

        void on_pressed() override {
            m_dragged = false;
            m_drag_distance = 0;
            bgui::button::on_pressed();
        }

        void set_drag(const bgui::vec2i& delta) override {
            bgui::button::set_drag(delta);
            if (!m_on_drop)
                return;
            m_drag_distance += std::abs(delta.x) + std::abs(delta.y);
            if (m_drag_distance >= 5)
                m_dragged = true;
        }

        void on_released() override {
            if (!m_dragged) {
                bgui::button::on_released();
                return;
            }

            bgui::element::on_released();
            if (m_on_drop)
                m_on_drop(bgui::get_mouse_position());
        }

    private:
        std::function<void(const bgui::vec2i&)> m_on_drop;
        int m_drag_distance{0};
        bool m_dragged{false};
    };

    std::filesystem::path m_project_root;
    drop_callback m_on_drop;
    std::string m_asset_status;
    std::string m_new_script_name{"NewScript.lua"};
    std::filesystem::path m_current_directory;
    std::filesystem::path m_selected_file;
    std::filesystem::path m_main_scene;
    std::unordered_set<std::filesystem::path> m_expanded_directories;
    bgui::linear* m_root{nullptr};
    bgui::linear* m_folder_sidebar{nullptr};
    bgui::linear* m_file_area{nullptr};
    bgui::dock* m_dock{nullptr};
    bgui::window* m_document_window{nullptr};
    bgui::linear* m_document_body{nullptr};
    bgui::text* m_document_status{nullptr};
    struct document {
        std::filesystem::path path;
        std::string original;
        std::string buffer;
        bgui::window* window{nullptr};
        bgui::linear* body{nullptr};
        bgui::text* status{nullptr};
        bool opened{false};
        bgui::inputbox* editor{nullptr};
        bgui::element* rendered{nullptr};
        bool image{false};
    };
    std::map<std::filesystem::path, document> m_documents;
    document* m_active_document{nullptr};
    std::vector<bgui::element*> m_active_content;
    bool m_refresh_requested{false};
    bool m_save_shortcut_down{false};

    static std::string lower_extension(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    static bool is_image_file(const std::filesystem::path& path) {
        const auto ext = lower_extension(path);
#ifdef _WIN32
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
               ext == ".bmp" || ext == ".gif" || ext == ".tif" || ext == ".tiff";
#else
        return ext == ".png";
#endif
    }

    static bool is_markdown_file(const std::filesystem::path& path) {
        const auto ext = lower_extension(path);
        return ext == ".md" || ext == ".markdown";
    }

    void ensure_document_window() {
        if (m_document_window || !m_dock)
            return;
        m_document_window = &m_dock->add_window("Editor", bgui::dock_area::center, 0.3f);
        m_document_window->set_tabbed(true);
        auto& root = m_document_window->add<frame_layout>(bgui::orientation::vertical);
        root.after_update = [this]() { update_document_editor(); };
        root.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        root.style.layout.padding = bgui::vec4i{0};
        m_document_window->get_context().style.layout.padding = bgui::vec4i{0};
        m_document_body = &root.add<bgui::linear>(bgui::orientation::vertical);
        m_document_body->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        m_document_body->style.layout.padding = bgui::vec4i{0};
        m_document_body->set_scrollable(true);
        auto& status_row = root.add<bgui::linear>(bgui::orientation::horizontal);
        status_row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        status_row.style.layout.padding = bgui::vec4i{6, 2, 6, 2};
        m_document_status = &status_row.add<bgui::text>("", 0.25f);
        m_document_status->style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::wrap_content
        );
        m_document_status->style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::end,
            bgui::alignment::center
        };

    }

    void open_document(const std::filesystem::path& path) {
        if (is_model_file(path)) return;
        if (!m_dock) return;
        auto found = m_documents.find(path);
        if (found == m_documents.end()) {
            document doc;
            doc.path = path;
            doc.image = is_image_file(path);
            if (!doc.image) {
                std::ifstream input(path, std::ios::binary);
                if (!input) return;
                doc.original.assign(std::istreambuf_iterator<char>(input), {});
                if (doc.original.find('\0') != std::string::npos) return;
                doc.buffer = doc.original;
            }
            auto [it, inserted] = m_documents.emplace(path, std::move(doc));
            found = it;
        }
        if (!found->second.window) {
            auto& item = found->second;
            m_document_window = nullptr;
            ensure_document_window();
            item.window = m_document_window;
            item.body = m_document_body;
            item.status = m_document_status;
            item.window->set_title(path.filename().string());
            for (const auto& [key, doc] : m_documents) {
                if (doc.window && doc.window != item.window) {
                    m_dock->merge_window_as_tab(*doc.window, *item.window);
                    break;
                }
            }
        }
        activate_document(path);
    }

    void activate_document(const std::filesystem::path& path) {
        const auto found = m_documents.find(path);
        if (found == m_documents.end()) return;
        if (m_active_document && m_active_document->editor)
            m_active_document->buffer = m_active_document->editor->get_buffer();
        m_active_document = &found->second;
        auto& doc = *m_active_document;
        m_document_window = doc.window;
        m_document_body = doc.body;
        m_document_status = doc.status;
        m_dock->focus_window(doc.window);
        if (doc.opened) return;
        doc.opened = true;
        set_document_status("", false);
        if (doc.image) {
            try {
                auto& image = m_document_body->add<bgui::image>(
                    bgui::asset_manager::get_instance().load_texture(
                        std::filesystem::absolute(doc.path).string()));
                image.set_size_mode(bgui::mode::match_parent, bgui::mode::match_parent);
                image.style.layout.align = bgui::vec<2, bgui::alignment>{bgui::alignment::center, bgui::alignment::center};
                m_active_content.push_back(&image);
            } catch (const std::exception& error) {
                auto& message = m_document_body->add<bgui::text>(
                    "Não foi possível abrir a imagem: " + std::string(error.what()), 0.28f);
                m_active_content.push_back(&message);
            }
        } else if (is_markdown_file(doc.path)) {
            render_markdown(doc);
        } else {
            if (lower_extension(doc.path) == ".lua")
                doc.editor = &m_document_body->add<lua_inputbox>(doc.buffer);
            else
                doc.editor = &m_document_body->add<bgui::inputbox>(
                    doc.buffer, "", 0.28f, nullptr, bgui::input_mode::multiline);
            doc.editor->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
            doc.editor->style.layout.padding = bgui::vec4i{0};
            doc.editor->get_label().style.layout.margin = bgui::vec4i{0};
            doc.editor->set_wrap(false);
            doc.editor->set_editor_guides_enabled(false);
            doc.editor->set_scrollable(true);
            doc.editor->set_highlight_rules(highlight_rules(doc.path));
            m_active_content.push_back(doc.editor);
        }
        m_document_window->set_title(doc.path.filename().string() +
            (doc.buffer != doc.original ? "*" : ""));
    }

    static std::shared_ptr<const std::vector<bgui::syntax_highlight_rule>>
    highlight_rules(const std::filesystem::path& path) {
        using rule = bgui::syntax_highlight_rule;
        const auto ext = lower_extension(path);
        std::vector<rule> rules;
        auto add = [&rules](const std::string& pattern, bgui::vec4 color) {
            rules.push_back({pattern, std::regex(pattern), color});
        };
        add("\\\"([^\\\"\\\\]|\\\\.)*\\\"|'([^'\\\\]|\\\\.)*'", {0.82f, 0.62f, 0.40f, 1.f});
        if (ext == ".json") {
            add("\\b(true|false|null)\\b", {0.72f, 0.53f, 0.88f, 1.f});
            add("-?[0-9]+(\\.[0-9]+)?", {0.55f, 0.72f, 0.91f, 1.f});
            add("//[^\\n]*", {0.38f, 0.68f, 0.42f, 1.f});
        } else if (ext == ".lua") {
            add("--[^\\n]*", {0.38f, 0.68f, 0.42f, 1.f});
            add("\\b(and|break|do|else|elseif|end|false|for|function|if|in|local|nil|not|or|repeat|return|then|true|until|while)\\b", {0.58f, 0.68f, 0.95f, 1.f});
        } else if (ext == ".py" || ext == ".sh" || ext == ".ps1" || ext == ".yaml" || ext == ".yml") {
            add("#[^\\n]*", {0.38f, 0.68f, 0.42f, 1.f});
            add("\\b(and|as|assert|async|await|break|class|def|elif|else|except|False|for|from|if|import|in|is|lambda|None|not|or|pass|raise|return|True|try|while|with|yield)\\b", {0.58f, 0.68f, 0.95f, 1.f});
        } else {
            add("//[^\\n]*|/\\*[\\s\\S]*?\\*/", {0.38f, 0.68f, 0.42f, 1.f});
            add("\\b(if|else|for|while|return|class|struct|namespace|const|static|void|int|float|bool|local|function|then|end|require|include)\\b", {0.58f, 0.68f, 0.95f, 1.f});
        }
        return std::make_shared<const std::vector<rule>>(std::move(rules));
    }

    void render_markdown(document& doc) {
        std::istringstream lines(doc.buffer);
        std::string line;
        bool in_code_block = false;
        while (std::getline(lines, line)) {
            std::string display = line;
            int heading = 0;
            while (heading < static_cast<int>(line.size()) && heading < 6 && line[heading] == '#') ++heading;
            if (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0) {
                in_code_block = !in_code_block;
                continue;
            }
            if (in_code_block) {
                display = "    " + line;
            } else if (heading && heading < static_cast<int>(line.size()) && line[heading] == ' ') {
                display = line.substr(static_cast<size_t>(heading + 1));
            } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
                display = "•  " + line.substr(2);
            } else if (line.rfind("> ", 0) == 0) {
                display = "│  " + line.substr(2);
            } else if (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0]))) {
                const auto marker = line.find(". ");
                if (marker != std::string::npos) display = "•  " + line.substr(marker + 2);
            } else if (line == "---" || line == "***" || line == "___") {
                display = "────────────────────────────────────────";
            }
            if (!in_code_block) {
                display = std::regex_replace(display, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"), "$1 ($2)");
                display = std::regex_replace(display, std::regex(R"(`([^`]+)`|\*\*([^*]+)\*\*|__([^_]+)__|\*([^*]+)\*|_([^_]+)_)"), "$1$2$3$4$5");
                display = std::regex_replace(display, std::regex(R"(~~([^~]+)~~)"), "$1");
            }
            auto& paragraph = m_document_body->add<bgui::text>(display, heading ? 0.28f + (6 - heading) * 0.035f : 0.28f);
            paragraph.set_wrap(true);
            paragraph.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
            if (heading) paragraph.style.visual.text.normal = bgui::color{0.78f, 0.84f, 0.96f, 1.f};
            m_active_content.push_back(&paragraph);
        }
    }

    void update_document_editor() {
        for (auto& [path, doc] : m_documents) {
            bool attached = false;
            if (m_dock)
                for (const auto& [layer, children] : m_dock->get_elements())
                    for (const auto& child : children)
                        attached |= child.get() == doc.window;
            if (!attached) {
                doc.window = nullptr;
                doc.body = nullptr;
                doc.status = nullptr;
                doc.editor = nullptr;
                doc.opened = false;
                if (m_active_document == &doc) {
                    m_active_document = nullptr;
                    m_document_window = nullptr;
                    m_document_body = nullptr;
                    m_document_status = nullptr;
                }
                continue;
            }
            if (doc.editor) {
                doc.buffer = doc.editor->get_buffer();
                if (doc.editor->is_focused() && doc.window->is_enabled()) {
                    m_active_document = &doc;
                    m_document_window = doc.window;
                    m_document_body = doc.body;
                    m_document_status = doc.status;
                }
            }
            if (doc.window)
                doc.window->set_title(doc.path.filename().string() +
                    (doc.buffer != doc.original ? "*" : ""));
        }
        const bool control_down =
            bgui::get_pressed(bgui::input_key::left_control) ||
            bgui::get_pressed(bgui::input_key::right_control);
        const bool save_down = control_down && bgui::get_pressed(bgui::input_key::s);
        const bool should_save = save_down && !m_save_shortcut_down && m_active_document &&
            m_active_document->editor && m_active_document->editor->is_focused();
        m_save_shortcut_down = save_down;
        if (should_save) save_active_document();

        if (!m_active_document) return;
        if (m_active_document->editor)
            m_active_document->buffer = m_active_document->editor->get_buffer();
        const bool dirty = m_active_document->buffer != m_active_document->original;
        if (m_document_window)
            m_document_window->set_title(m_active_document->path.filename().string() + (dirty ? "*" : ""));
    }

    void save_active_document() {
        if (!m_active_document || m_active_document->image ||
            is_markdown_file(m_active_document->path))
            return;
        if (m_active_document->editor)
            m_active_document->buffer = m_active_document->editor->get_buffer();

        const auto target = m_active_document->path;
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto temporary = target.parent_path() /
            (target.filename().string() + ".tmp." + std::to_string(nonce));
        const auto backup = target.parent_path() /
            (target.filename().string() + ".bak." + std::to_string(nonce));
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            set_document_status("Falha ao criar arquivo temporário para salvar.", true);
            return;
        }
        output.write(m_active_document->buffer.data(),
            static_cast<std::streamsize>(m_active_document->buffer.size()));
        output.flush();
        if (!output) {
            output.close();
            remove_file(temporary);
            set_document_status("Falha ao gravar o arquivo; alterações mantidas no editor.", true);
            return;
        }
        output.close();
        if (output.fail()) {
            remove_file(temporary);
            set_document_status("Falha ao finalizar o arquivo; alterações mantidas no editor.", true);
            return;
        }

        std::error_code error;
        std::filesystem::rename(temporary, target, error);
        std::error_code exists_error;
        const bool target_exists = std::filesystem::exists(target, exists_error);
        if (error && target_exists && !exists_error) {
            // On platforms where rename cannot replace an existing file, keep a
            // recoverable copy while installing the new version.
            error.clear();
            std::filesystem::rename(target, backup, error);
            if (!error) {
                std::filesystem::rename(temporary, target, error);
                if (error) {
                    std::error_code restore_error;
                    std::filesystem::rename(backup, target, restore_error);
                    remove_file(temporary);
                    set_document_status(
                        restore_error
                            ? "Falha ao salvar e restaurar a cópia anterior: " + restore_error.message()
                            : "Falha ao salvar; a versão anterior foi restaurada.",
                        true
                    );
                    return;
                }
                remove_file(backup);
            }
        }
        if (error) {
            remove_file(temporary);
            set_document_status("Falha ao salvar: " + error.message(), true);
            return;
        }

        m_active_document->original = m_active_document->buffer;
        update_document_editor();
        set_document_status("Arquivo salvo.", false);
    }

    void set_document_status(const std::string& message, bool is_error) {
        if (!m_document_status)
            return;
        m_document_status->get_parent()->set_enable(!message.empty());
        m_document_status->set_buffer(message);
        m_document_status->style.visual.text.normal = is_error
            ? bgui::color{0.95f, 0.45f, 0.40f, 1.f}
            : bgui::color{0.55f, 0.82f, 0.60f, 1.f};
    }

    static void remove_file(const std::filesystem::path& path) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    static std::filesystem::path resolve_project_root(
        std::filesystem::path project_file
    ) {
        std::error_code error;
        auto absolute_path = std::filesystem::absolute(project_file, error);
        if (error) {
            std::cerr << "[editor] Could not resolve project path: "
                      << error.message() << '\n';
            absolute_path = std::filesystem::current_path();
        }

        if (std::filesystem::is_directory(absolute_path, error) && !error)
            return absolute_path.lexically_normal();

        const auto parent = absolute_path.parent_path();
        return (parent.empty() ? std::filesystem::current_path() : parent)
            .lexically_normal();
    }

    static bool is_scene_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".world" || extension == ".scene";
    }

    static bool is_project_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".bengine";
    }

    static bool is_lua_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".lua";
    }

    static std::string_view file_icon(const std::filesystem::path& path) {
        if (is_project_file(path))
            return "home";
        if (is_scene_file(path))
            return "cube";
        if (is_lua_file(path))
            return "code";
        return "file";
    }

    static bool is_model_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".obj" || extension == ".fbx" ||
            extension == ".gltf" || extension == ".glb" ||
            extension == ".dae" || extension == ".3ds";
    }

    static void clear_layout(bgui::linear& layout) {
        for (auto& [layer, elements] : layout.get_elements()) {
            (void)layer;
            while (!elements.empty())
                layout.remove(elements.back().get());
        }
    }

    static void configure_label(bgui::text& label) {
        label.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        label.style.layout.padding = bgui::vec4i{2, 4, 2, 4};
        label.receives_input(false);
    }

    void load_main_scene() {
        m_main_scene.clear();
        std::ifstream marker(m_project_root / ".bengine-main-scene");
        if (!marker)
            return;

        std::string relative_path;
        if (!std::getline(marker, relative_path))
            return;

        m_main_scene = (m_project_root / relative_path).lexically_normal();
    }

    void refresh_if_needed() {
        if (!m_root || !m_refresh_requested)
            return;

        m_refresh_requested = false;
        clear_layout(*m_folder_sidebar);
        clear_layout(*m_file_area);
        build_folder_tree(m_project_root, 0);
        build_file_list();
    }

    void request_refresh() {
        m_refresh_requested = true;
        refresh_if_needed();
    }

    std::string directory_label(const std::filesystem::path& directory) const {
        if (directory == m_project_root)
            return "Projeto";
        return directory.filename().string();
    }

    void build_folder_tree(
        const std::filesystem::path& directory,
        const std::size_t depth
    ) {
        build_folder_node(directory, *m_folder_sidebar);
    }

    void build_folder_node(const std::filesystem::path& directory, bgui::linear& parent) {
        const bool expanded = m_expanded_directories.contains(directory);
        auto& node = parent.add<bgui::tree>(directory_label(directory), 0.27f, expanded);
        node.set_icon(directory == m_project_root ? "home" : "folder", 0.24f);
        node.header().set_function([this, directory]() {
            m_current_directory = directory;
            m_selected_file.clear();
            request_refresh();
        });
        node.disclosure().set_function([this, directory, node = &node]() {
            const bool expanded = !node->is_expanded();
            node->set_expanded(expanded);
            if (expanded) m_expanded_directories.insert(directory);
            else m_expanded_directories.erase(directory);
        });
        node.header().style.visual.background.normal = directory == m_current_directory
            ? bgui::color{0.19f, 0.21f, 0.24f, 1.f} : bgui::color{0.f, 0.f, 0.f, 0.f};

        std::error_code error;
        std::vector<std::filesystem::path> directories;
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            const auto filename = it->path().filename().string();
            if (filename.empty() || filename.front() == '.')
                continue;
            std::error_code type_error;
            if (it->is_symlink(type_error) || type_error)
                continue;
            type_error.clear();
            if (it->is_directory(type_error) && !type_error)
                directories.push_back(it->path());
        }

        std::sort(directories.begin(), directories.end());
        node.set_leaf(directories.empty());
        for (const auto& child : directories)
            build_folder_node(child, node.children());
    }

    void build_file_list() {
        const auto relative_directory =
            m_current_directory.lexically_relative(m_project_root);
        auto& path_label = m_file_area->add<bgui::text>(
            relative_directory.empty() || relative_directory == "."
                ? "Projeto"
                : relative_directory.generic_string(),
            0.32f
        );
        configure_label(path_label);
        path_label.add_class("editor-header");

        auto& create_row = m_file_area->add<bgui::linear>(bgui::orientation::horizontal);
        create_row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        auto& filename = create_row.add<bgui::inputbox>(m_new_script_name, "Script.lua", 0.27f);
        filename.add_class("new-script-name");
        filename.style.layout.require_width(bgui::mode::stretch);
        auto create = [this, input = &filename]() {
            m_new_script_name = input->get_buffer();
            std::filesystem::path created;
            std::string error;
            if (create_lua_script(m_current_directory, m_new_script_name, created, error)) {
                m_selected_file = created;
                m_asset_status = "Script criado. Arraste-o para uma entidade na Scene.";
                open_document(created);
            } else m_asset_status = error;
            request_refresh();
        };
        filename.m_enter_func = [create](const std::string&) { create(); };
        filename.style.layout.require_mode(bgui::mode::stretch, bgui::mode::wrap_content);
        create_row.add<bgui::button>("Novo script Lua", 0.27f, create).add_class("new-script-button");
        if (!m_asset_status.empty()) add_message(m_asset_status);

        if (!m_selected_file.empty() && is_scene_file(m_selected_file)) {
            auto& action = m_file_area->add<bgui::button>(
                m_selected_file == m_main_scene
                    ? "Cena principal"
                    : "Tornar cena principal",
                0.28f,
                [this]() { set_main_scene(); }
            );
            action.style.layout.require_mode(
                bgui::mode::wrap_content,
                bgui::mode::wrap_content
            );
            action.style.layout.padding = bgui::vec4i{7, 3, 7, 3};
            action.set_enable(m_selected_file != m_main_scene);
        }

        std::error_code error;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(m_current_directory, error), end;
             !error && it != end; it.increment(error)) {
            const auto filename = it->path().filename().string();
            if (filename.empty() || filename.front() == '.')
                continue;
            std::error_code type_error;
            if (it->is_symlink(type_error) || type_error)
                continue;
            type_error.clear();
            if (it->is_regular_file(type_error) && !type_error)
                files.push_back(it->path());
        }

        if (error) {
            add_message("Não foi possível ler esta pasta: " + error.message());
            return;
        }

        std::sort(files.begin(), files.end());
        if (files.empty()) {
            add_message("Esta pasta não contém arquivos.");
            return;
        }
        for (const auto& file : files) {
            const bool selected = file == m_selected_file;
            auto& item = m_file_area->add<file_button>(
                "",
                [this, file]() {
                    m_selected_file = file;
                    open_document(file);
                    request_refresh();
                },
                (is_model_file(file) || is_image_file(file) || is_lua_script(file))
                    ? [this, file](const bgui::vec2i& position) {
                          if (m_on_drop)
                              m_on_drop(file, position);
                      }
                    : std::function<void(const bgui::vec2i&)>{}
            );
            item.get_label().set_enable(false);
            auto& icon = item.add<bgui::icon>(file_icon(file), 0.27f);
            icon.receives_input(false);
            icon.style.layout.require_mode(
                bgui::mode::wrap_content,
                bgui::mode::wrap_content
            );
            icon.style.layout.padding = bgui::vec4i{2, 0, 6, 0};
            auto& label = item.add<bgui::text>(
                file.filename().string(),
                0.28f
            );
            label.receives_input(false);
            label.set_wrap(false);
            label.style.layout.require_mode(
                bgui::mode::stretch,
                bgui::mode::wrap_content
            );
            label.style.layout.align = bgui::vec<2, bgui::alignment>{
                bgui::alignment::start,
                bgui::alignment::center
            };
            item.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
            item.style.layout.padding = bgui::vec4i{5, 3, 5, 3};
            item.style.visual.background.normal = selected
                ? bgui::color{0.20f, 0.31f, 0.43f, 1.f}
                : bgui::color{0.15f, 0.16f, 0.18f, 1.f};
            item.style.visual.background.hover =
                bgui::color{0.22f, 0.24f, 0.27f, 1.f};
            item.style.visual.border_radius = 3.f;
        }
    }

    void add_message(const std::string& message) {
        auto& label = m_file_area->add<bgui::text>(message, 0.28f);
        configure_label(label);
    }

    void set_main_scene() {
        if (m_selected_file.empty() || !is_scene_file(m_selected_file))
            return;

        const auto relative_path =
            m_selected_file.lexically_relative(m_project_root);
        std::ofstream marker(
            m_project_root / ".bengine-main-scene",
            std::ios::trunc
        );
        if (!marker) {
            std::cerr << "[editor] Could not save the main scene selection.\n";
            return;
        }

        marker << relative_path.generic_string() << '\n';
        if (!marker) {
            std::cerr << "[editor] Could not write the main scene selection.\n";
            return;
        }

        m_main_scene = m_selected_file;
        request_refresh();
    }
};

}
