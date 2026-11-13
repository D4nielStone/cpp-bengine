#pragma once
#include "components/renderer.hpp"
#include "editor_context.hpp"
#include "systems/animation_system.hpp"
#include "animation_assets.hpp"

namespace editor::ui {
class animation_editor {
public:
    explicit animation_editor(std::shared_ptr<editor_context> context) : m_context(std::move(context)) {}
    std::shared_ptr<commons::animation> player(uint32_t id) const { return m_context->registry->get<commons::animation>(id); }
    std::shared_ptr<commons::animation_clip> clip(uint32_t id) const {
        auto value = player(id);
        if (!value) return {};
        m_error.clear();
        if (!commons::resolve_animation_clip(*m_context->registry, *value, m_context->project_root, m_error)) return {};
        if (value->data && !value->data->source_path.empty()) {
            auto& assets=m_context->animation_assets;
            const auto found=std::find_if(assets.begin(),assets.end(),[&](const auto& asset) { return asset->source_path==value->data->source_path; });
            if (found!=assets.end()) value->data=*found;
            else assets.push_back(value->data);
        }
        return value->data;
    }
    const std::string& error() const { return m_error; }
    bool add_component(uint32_t id) { return player(id) || set_player(id, commons::animation{}); }

    bool assign(uint32_t id, const std::string& path, std::string& error) {
        try {
            if (m_context->project_root.empty()) throw std::runtime_error("Open a project before assigning animation clips.");
            const auto root = std::filesystem::weakly_canonical(m_context->project_root);
            const auto absolute = std::filesystem::weakly_canonical(commons::animation_clip_path(root, path));
            const auto relative = absolute.lexically_relative(root);
            for (const auto& part : relative) if (part == "..") throw std::runtime_error("Import the clip into this project first.");
            commons::animation next = player(id) ? *player(id) : commons::animation{};
            next.clip = relative.generic_string(); next.data.reset();
            const auto cached=std::find_if(m_context->animation_assets.begin(),m_context->animation_assets.end(),
                [&](const auto& asset) { return asset->source_path==absolute; });
            if(cached!=m_context->animation_assets.end()) next.data=*cached;
            else if (!commons::resolve_animation_clip(*m_context->registry, next, root, error)) return false;
            if(std::find(m_context->animation_assets.begin(),m_context->animation_assets.end(),next.data)==m_context->animation_assets.end())
                m_context->animation_assets.push_back(next.data);
            return set_player(id, std::move(next));
        } catch (const std::exception& failure) { error = failure.what(); return false; }
    }
    bool import_clip(uint32_t id, const std::filesystem::path& source, std::string& error) {
        try {
            if (!editable(id) || m_context->project_root.empty()) throw std::runtime_error("Select an editable entity in an open project.");
            auto imported = commons::load_animation_clip(source);
            auto directory = m_context->project_root / "assets/animations";
            std::filesystem::create_directories(directory);
            auto destination = directory / source.filename();
            if (std::filesystem::weakly_canonical(source) == std::filesystem::weakly_canonical(destination))
                return assign(id, destination.string(), error);
            for (unsigned suffix = 1; std::filesystem::exists(destination); ++suffix)
                destination = directory / (source.stem().string() + "-" + std::to_string(suffix) + ".banim");
            imported->source_path = destination;
            imported->source_contents.clear(); imported->saved_definition.clear();
            if (!save_animation_asset(*imported, error)) return false;
            m_context->events.publish(editor_event::assets);
            return assign(id, destination.string(), error);
        } catch (const std::exception& failure) { error = failure.what(); return false; }
    }
    bool create(uint32_t id, std::string name, std::string& error) {
        try {
            if (!editable(id) || m_context->project_root.empty()) throw std::runtime_error("Select an editable entity in an open project.");
            const std::filesystem::path leaf(name);
            if (name.empty() || name.find_first_of("\\/\r\n") != std::string::npos || leaf == "." || leaf == "..")
                throw std::runtime_error("Use a clip name without folder separators.");
            if (leaf.extension() != ".banim") name += ".banim";
            const auto reference = "assets/animations/" + name;
            const auto path = commons::animation_clip_path(m_context->project_root, reference);
            if (std::filesystem::exists(path)) throw std::runtime_error("A clip with this name already exists; assign it or choose another name.");
            auto old = clip(id);
            commons::animation_clip created;
            if (old) created.assign_definition(*old);
            created.source_path = path;
            if (!save_animation_asset(created, error)) return false;
            m_context->events.publish(editor_event::assets);
            return assign(id, reference, error);
        } catch (const std::exception& failure) { error = failure.what(); return false; }
    }
    bool commit(uint32_t id, const commons::animation_clip& next) {
        if (!editable(id)) return false;
        try { (void)next.encode(); } catch (...) { return false; }
        auto value = clip(id);
        if (!value) {
            // Compatibility for legacy callers; the UI requires an assigned asset.
            auto next_player = player(id) ? *player(id) : commons::animation{};
            if (!next_player.clip.empty()) return false;
            next_player.data = std::make_shared<commons::animation_clip>(next);
            return set_player(id, std::move(next_player));
        }
        const auto before = *value;
        value->assign_definition(next);
        m_context->record_action([value, before] { value->assign_definition(before); },
            [value, next] { value->assign_definition(next); });
        return true;
    }
    bool move_key(uint32_t id, std::size_t track_index, std::size_t key_index, double time) {
        auto asset = clip(id);
        if (!asset || !editable(id) || !std::isfinite(time) || time < 0 || time > asset->duration ||
            track_index >= asset->tracks.size() || key_index >= asset->tracks[track_index].keys.size()) return false;
        auto next = *asset;
        auto& track = next.tracks[track_index];
        for (std::size_t i = 0; i < track.keys.size(); ++i)
            if (i != key_index && track.keys[i].time == time) return false;
        if (track.keys[key_index].time == time) return true;
        auto key = track.keys[key_index];
        key.time = time;
        track.keys.erase(track.keys.begin() + key_index);
        track.insert(key);
        const bool result = commit(id, next);
        if (result) begin_recording(id);
        return result;
    }
    bool settings(uint32_t id, bool autoplay, double speed) {
        auto value = player(id);
        if (!value || !std::isfinite(speed) || speed < 0 || speed > 100) return false;
        auto next = *value; next.autoplay = autoplay; next.speed = speed;
        return set_player(id, std::move(next));
    }
    bool save(uint32_t id, std::string& error) {
        auto value = clip(id);
        if (!value) { error = m_error.empty() ? "No animation clip assigned." : m_error; return false; }
        return save_animation_asset(*value, error);
    }
    bool reload(uint32_t id, std::string& error) {
        auto value=clip(id);
        if(!value || value->source_path.empty()) {error="No external animation clip assigned."; return false;}
        try {
            const auto loaded=commons::load_animation_clip(value->source_path);
            const auto before=*value;
            value->assign_definition(*loaded);
            value->source_contents=loaded->source_contents;
            value->saved_definition=loaded->saved_definition;
            m_context->record_action([value,before]{value->assign_definition(before);},
                [value,loaded]{value->assign_definition(*loaded);});
            begin_recording(id);
            return true;
        } catch(const std::exception& failure) {error=failure.what(); return false;}
    }
    void begin_recording(uint32_t id) {
        m_recorded_entity = id; m_baseline = observe(id);
        m_history_revision = m_context->history_revision;
        m_history_size = m_context->undo_stack.size();
    }
    bool record_changes(uint32_t id, double time) {
        const auto current = observe(id);
        if (!editable(id) || id != m_recorded_entity || m_context->history_revision != m_history_revision) {
            begin_recording(id); return false;
        }
        auto asset = clip(id);
        if (!asset || !std::isfinite(time) || time < 0 || time > asset->duration) { begin_recording(id); return false; }
        auto next = *asset;
        bool changed = false;
        for (const auto& now : current) {
            const auto old = std::find_if(m_baseline.begin(), m_baseline.end(), [&](const auto& before) {
                return before.target.lock() == now.target.lock() && before.mask == now.mask && before.property.path == now.property.path;
            });
            if (old == m_baseline.end() || old->value == now.value) continue;
            auto track = std::find_if(next.tracks.begin(), next.tracks.end(), [&](const auto& t) {
                return t.target == now.mask && t.property == now.property.path;
            });
            if (track == next.tracks.end()) {
                next.tracks.push_back({now.mask, now.property.path, now.property.stepped, {}});
                track = std::prev(next.tracks.end());
                if (time > 0) track->insert({0, old->value});
            }
            track->insert({time, now.value}); changed = true;
        }
        const auto previous_size = m_context->undo_stack.size();
        if (changed && commit(id, next) && previous_size == m_history_size + 1) {
            auto keys = std::move(m_context->undo_stack.back()); m_context->undo_stack.pop_back();
            auto property = std::move(m_context->undo_stack.back()); m_context->undo_stack.pop_back();
            m_context->undo_stack.push_back({[property, keys] { keys.undo(); property.undo(); },
                [property, keys] { property.redo(); keys.redo(); }, editor_event::scene});
        }
        begin_recording(id);
        return changed;
    }
    void end_preview() { m_preview_edits.reset(); }
    bool preview(uint32_t id, double time, bool continuous = false) {
        auto registry = m_context->registry;
        auto animation = clip(id);
        if (!animation || !editable(id) || !std::isfinite(time) || time < 0 || time > animation->duration) return false;
        auto edits = continuous ? m_preview_edits.lock() : nullptr;
        const bool extend = edits && m_preview_entity == id && m_preview_registry.lock() == registry &&
            m_preview_player.lock() == player(id) && m_preview_asset.lock() == animation &&
            m_preview_revision == m_context->history_revision && m_preview_history_size == m_context->undo_stack.size();
        if (!extend) edits = std::make_shared<std::vector<preview_edit>>();
        bool sampled = false;
        for (const auto& track : animation->tracks) {
            auto target = commons::animation_target(*registry, id, track.target);
            auto after = track.sample(time);
            auto before = target ? commons::read_animation_property(*target, track.property) : std::nullopt;
            if (before && after && commons::write_animation_property(*target, track.property, *after)) {
                sampled = true;
                auto previous = std::find_if(edits->begin(), edits->end(), [&](const auto& edit) {
                    return edit.target.lock() == target && edit.mask == track.target && edit.path == track.property;
                });
                if (previous == edits->end()) edits->push_back({target, track.target, track.property, *before, *after});
                else previous->after = *after;
            }
        }
        if (!sampled) return false;
        if (auto renderer = registry->get<commons::renderer>(id); renderer && renderer->m_modelo)
            renderer->m_modelo->apply_pose();
        auto apply = [registry, edits, id](bool redo) {
            for (const auto& edit : *edits) {
                auto target = commons::animation_target(*registry, id, edit.mask);
                if (target && target == edit.target.lock()) commons::write_animation_property(*target, edit.path, redo ? edit.after : edit.before);
            }
            if (auto renderer = registry->get<commons::renderer>(id); renderer && renderer->m_modelo) renderer->m_modelo->apply_pose();
        };
        if (!extend) m_context->record_action([apply] { apply(false); }, [apply] { apply(true); }, editor_event::properties);
        else m_context->events.publish(editor_event::properties);
        m_preview_edits = continuous ? edits : nullptr;
        m_preview_entity = id; m_preview_registry = registry;
        m_preview_player = player(id); m_preview_asset = animation;
        m_preview_revision = m_context->history_revision; m_preview_history_size = m_context->undo_stack.size();
        if (!continuous) begin_recording(id);
        return true;
    }
private:
    struct preview_edit {
        std::weak_ptr<commons::component> target;
        commons::component::mask mask;
        std::string path;
        double before, after;
    };
    std::weak_ptr<std::vector<preview_edit>> m_preview_edits;
    std::weak_ptr<commons::ecs> m_preview_registry;
    std::weak_ptr<commons::animation> m_preview_player;
    std::weak_ptr<commons::animation_clip> m_preview_asset;
    uint32_t m_preview_entity{};
    std::size_t m_preview_revision{}, m_preview_history_size{};
    struct observed {
        commons::component::mask mask;
        commons::animation_property property;
        std::weak_ptr<commons::component> target;
        double value;
    };
    std::vector<observed> observe(uint32_t id) const {
        std::vector<observed> result;
        const auto registry = m_context->registry;
        if (!registry->is_alive(id)) return result;
        for (const auto& [mask, target] : registry->entities.at(id)) {
            if (!target || mask == commons::animation::mask) continue;
            for (auto property : commons::animation_properties(*target))
                if (auto value = commons::read_animation_property(*target, property.path); value && std::isfinite(*value))
                    result.push_back({mask, std::move(property), target, *value});
        }
        return result;
    }
    bool editable(uint32_t id) const {
        return m_context->registry->is_alive(id) && !m_context->is_playing && !m_context->locked_entities.contains(id);
    }
    bool set_player(uint32_t id, commons::animation next) {
        if (!editable(id)) return false;
        auto registry = m_context->registry;
        next.my_object = id; next.reg = registry;
        auto current = player(id);
        auto before = current ? std::optional<commons::animation>{*current} : std::nullopt;
        const std::weak_ptr<commons::component> owner = commons::animation_target(*registry, id, commons::component::COMPONENTE_TRANSFORMACAO);
        if (!current) { commons::entity entity{id}; registry->add<commons::animation>(entity); current = player(id); }
        *current = next;
        m_context->record_action([registry, current, owner, id, before] {
            if (commons::animation_target(*registry, id, commons::component::COMPONENTE_TRANSFORMACAO) != owner.lock() || registry->get<commons::animation>(id) != current) return;
            if (before) *current = *before; else registry->remove<commons::animation>(id);
        }, [registry, current, owner, id, next, before] {
            if (!registry->is_alive(id) || commons::animation_target(*registry, id, commons::component::COMPONENTE_TRANSFORMACAO) != owner.lock()) return;
            auto target = registry->get<commons::animation>(id);
            if (!before && !target) { registry->entities[id][commons::animation::mask] = current; registry->mascaras[id] |= commons::animation::mask; target = current; }
            if (target == current) *current = next;
        });
        return true;
    }
    std::shared_ptr<editor_context> m_context;
    mutable std::string m_error;
    uint32_t m_recorded_entity{};
    std::vector<observed> m_baseline;
    std::size_t m_history_revision{}, m_history_size{};
};
}
