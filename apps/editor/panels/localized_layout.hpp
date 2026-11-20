#pragma once
#include "../core/localization.hpp"
#include <bgui.hpp>
#include <elem/window.hpp>
#include <array>
#include <string_view>

namespace editor::ui {
// BGUI persists titles as identities. Use the legacy titles only while reading
// or writing the layout so a language change cannot orphan dock entries.
class canonical_layout_titles {
public:
    canonical_layout_titles() { visit(bgui::get_layout()); }
    ~canonical_layout_titles() {
        for (auto& [window, title] : m_titles) window->set_title(title);
    }
    canonical_layout_titles(const canonical_layout_titles&) = delete;
    canonical_layout_titles& operator=(const canonical_layout_titles&) = delete;
private:
    std::vector<std::pair<bgui::window*, std::string>> m_titles;
    void visit(bgui::layout& layout) {
        constexpr std::array keys{"Scene", "Hierarchy", "Inspector", "Debug", "Arquivos",
            "Arquivos 3D", "Animação", "Cenas", "Game", "Camera Preview", "Editor Diagnostics",
            "Edição do modelo", "Malhas", "Rig e Weight Paint", "Animações do modelo"};
        for (auto& [layer, children] : layout.get_elements()) {
            for (auto& child : children) {
                if (auto* window = dynamic_cast<bgui::window*>(child.get())) {
                    const auto title = window->get_title().get_buffer();
                    for (const auto* key : keys) {
                        if (title == tr(key) || (std::string_view(key) == "Editor Diagnostics" && title == tr("Profiler"))) {
                            m_titles.emplace_back(window, title);
                            window->set_title(key);
                            break;
                        }
                    }
                }
                if (auto* nested = child->as_layout()) visit(*nested);
            }
        }
    }
};
}
