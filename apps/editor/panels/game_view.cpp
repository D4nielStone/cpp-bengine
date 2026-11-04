#include "ui/hud_view.hpp"
#define GLFW_INCLUDE_NONE
#include <glad.h>

#include "game_view.hpp"
#include "context_action_button.hpp"
#include "viewport_input_state.hpp"

#include "components/camera.hpp"
#include "render_viewport.hpp"
#include <glm/gtx/euler_angles.hpp>

#include <elem/image.hpp>
#include <elem/text.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <stdexcept>
#include <array>
#include <string>
#include <utility>

namespace editor::ui {

struct game_view_state {
    std::shared_ptr<editor_context> context;
    bgui::window* window{nullptr};
    bgui::image* image{nullptr};
    bgui::text* status{nullptr};
    bgui::element* editor_surface{nullptr};
    std::shared_ptr<commons::camera> camera;
    std::uint32_t camera_entity{0};
    commons::camera render_camera;
    commons::render_system renderer;
};

namespace {

std::shared_ptr<commons::camera> active_game_camera(
    const std::shared_ptr<editor_context>& context,
    std::uint32_t& camera_entity
) {
    std::shared_ptr<commons::camera> camera;
    context->registry->cada<commons::camera>([&](const std::uint32_t id) {
        if (camera || (context->editor_camera_entity &&
                       id == *context->editor_camera_entity))
            return;
        auto candidate = context->registry->get<commons::camera>(id);
        if (candidate) {
            camera_entity = id;
            camera = std::move(candidate);
        }
    });

    if (camera)
        return camera;
    if (!context->editor_camera_entity)
        return {};

    camera_entity = *context->editor_camera_entity;
    return context->registry->get<commons::camera>(camera_entity);
}

void update_game_view(const std::shared_ptr<game_view_state>& state, bool render = false) {
    if (!state || !state->context || !state->window ||
        !state->image || !state->status)
        return;

    if (state->editor_surface) state->editor_surface->set_enable(!state->context->is_playing);
    if (!state->window->is_enabled()) return;

    std::uint32_t camera_entity = 0;
    auto camera = active_game_camera(state->context, camera_entity);
    if (!camera) {
        state->camera.reset();
        state->camera_entity = 0;
        state->image->set_external_texture(0,{1,1});
        state->image->set_enable(true);
        state->status->set_buffer("No game camera.");
        return;
    }

    state->camera = std::move(camera);
    state->camera_entity = camera_entity;
    state->image->set_enable(true);
    const int width = state->image->processed_width();
    const int height = state->image->processed_height();
    if (render && width > 0 && height > 0 && !viewport_input_blocked_by_modal() &&
        glad_glGetString && glGetString(GL_VERSION)) {
        auto& view = state->render_camera;
        const auto& source = *state->camera;
        view.reg = state->context->registry;
        view.my_object = camera_entity;
        view.config = source.config;
        view.skybox_faces = source.skybox_faces;
        view.skybox_asset_root = source.skybox_asset_root;
        view.viewMatrix = source.viewMatrix;
        view.position = source.position;
        const auto frame = render_viewport(state->renderer, state->context->registry,
            view, width, height);
        const bgui::vec2 size{float(frame.width), float(frame.height)};
        if (state->image->get_texture().m_id != frame.texture || state->image->get_texture().m_size != size)
            state->image->set_external_texture(frame.texture, size);
    }

    const auto name = state->context->entity_names.find(camera_entity);
    const std::string camera_name = name != state->context->entity_names.end()
        ? name->second
        : "Camera " + std::to_string(camera_entity);
    state->status->set_buffer(camera_name + (state->context->editor_camera_entity == camera_entity
        ? " | Add a game camera to edit its HUD." : " | Right-click to add HUD widgets."));
}

class hud_editor_surface final : public bgui::linear {
    std::shared_ptr<game_view_state> state;
    commons::hud_view& view;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    bool down=false;
    bool right_down=false;
    int drag=-1;
    bgui::vec4i drag_bounds{0};
    bool moved=false;
    bgui::vec2i start{0};
    commons::hud_element before;
    std::weak_ptr<commons::hud_element> dragging;
    bgui::linear* outline=nullptr;
    std::array<bgui::button*,4> handles{};
public:
    hud_editor_surface(std::shared_ptr<game_view_state> s, commons::hud_view& v)
        : bgui::linear(bgui::orientation::horizontal), state(std::move(s)), view(v) {
        set_flex(false); style.visual.visible=false;
        outline=&add<bgui::linear>(bgui::orientation::horizontal);
        outline->set_flex(false);
        outline->receives_input(false);
        const bgui::color accent{.25f,.65f,1.f,1.f};
        outline->style.visual.background.normal = bgui::color{0.f,0.f,0.f,0.f};
        outline->style.visual.border.normal = accent;
        outline->style.visual.border_size = 2.f;
        outline->style.visual.border_radius = 0.f;
        for(auto*& handle : handles) {
            handle=&add<bgui::button>("",.25f,[]{});
            handle->set_flex(false);
            handle->get_label().set_enable(false);
            handle->style.visual.background.normal = accent;
            handle->style.visual.border_radius = 0.f;
        }
    }
    bool clips_children() const override { return true; }
    bool supports_render_cache() const override { return false; }
    void on_update() override {
        const auto r=state->image->processed_rect();
        set_final_rect(r.x,r.y,r.z,r.w);
        auto ctx=state->context;
        auto e=ctx->selected_hud_widget.lock();
        bool valid=false;
        if(ctx->selected_entity) if(auto h=ctx->registry->get<commons::hud>(*ctx->selected_entity))
            valid=std::find(h->elements.begin(),h->elements.end(),e)!=h->elements.end();
        if(!valid) { e.reset(); ctx->selected_hud_widget.reset(); }
        const bool editing=!ctx->is_playing;
        receives_input(editing);
        const auto bounds=view.element_rect(e);
        const bool show=editing && e && e->visible && bounds.z>0 && bounds.w>0;
        outline->set_enable(show);
        outline->set_final_rect(bounds.x,bounds.y,bounds.z,bounds.w);
        constexpr int handle_size=10;
        for(int i=0;i<4;++i) {
            handles[i]->set_enable(show);
            const int x=bounds.x+((i&1) ? bounds.z : 0);
            const int y=bounds.y+((i&2) ? bounds.w : 0);
            handles[i]->set_final_rect(x-handle_size/2,y-handle_size/2,handle_size,handle_size);
        }
        const bool pressed=bgui::get_pressed(bgui::input_key::mouse_left);
        const auto mouse=bgui::get_mouse_position();
        auto* target=bgui::get_mouse_target();
        if(editing && pressed && !down) {
            for(int i=0;i<4;++i)
                if(show && target==handles[i]) drag=i+1;
            if(drag<0 && target==this) {
                const bool inside=show && mouse.x>=bounds.x && mouse.y>=bounds.y &&
                    mouse.x<bounds.x+bounds.z && mouse.y<bounds.y+bounds.w;
                std::shared_ptr<commons::hud_element> hit=inside ? e : nullptr;
                if(!inside) ctx->registry->cada<commons::hud>([&](uint32_t id) {
                    auto h=ctx->registry->get<commons::hud>(id); if(!h->enabled) return;
                    for(auto it=h->elements.rbegin();it!=h->elements.rend();++it) {
                        const auto b=view.element_rect(*it);
                        if((*it)->visible && mouse.x>=b.x && mouse.y>=b.y && mouse.x<b.x+b.z && mouse.y<b.y+b.w) {
                            ctx->select_entity(id);
                            if(ctx->selected_entity==id) hit=*it;
                            break;
                        }
                    }
                });
                ctx->selected_hud_widget=hit;
                if(hit) {
                    e=hit;
                    drag=0;
                }
            }
            if(drag>=0) {
                start=mouse;
                drag_bounds=view.element_rect(e);
                before=*e;
                dragging=e;
                moved=false;
            }
        }
        const bool right = bgui::get_pressed(bgui::input_key::mouse_right);
        bool over_surface = false;
        for (auto* element = target; element; element = element->get_parent())
            if (element == this) over_surface = true;
        if (editing && right && !right_down && over_surface && state->camera &&
            ctx->editor_camera_entity != state->camera_entity) {
            context_actions actions;
            const auto id = state->camera_entity;
            const auto camera = std::weak_ptr<commons::camera>(state->camera);
            for (const std::string kind : {"text", "button", "row", "column", "text3d"}) {
                actions.emplace_back(kind == "text3d" ? "Add 3D text" : "Add " + kind, [ctx, kind, mouse, r, id, camera] {
                    const auto owner = camera.lock();
                    if (ctx->is_playing || !owner || ctx->registry->get<commons::camera>(id) != owner)
                        return;
                    auto hud = ctx->registry->get<commons::hud>(id);
                    if (!hud) {
                        auto entity = commons::entity(id);
                        ctx->registry->add<commons::hud>(entity);
                        hud = ctx->registry->get<commons::hud>(id);
                    }
                    if (hud->elements.size() >= 1024) return;
                    auto widget = hud->add(kind, kind == "button" ? "Button" : kind == "text" ? "Text" : "");
                    widget->absolute = true;
                    widget->width = 160;
                    widget->height = 40;
                    if (kind == "text3d") {
                        widget->text = "3D Text";
                        widget->depth = .05f;
                        const auto camera_to_world = glm::inverse(owner->getViewMatrix());
                        const auto point = camera_to_world * glm::vec4(-.35f, 0.f, -3.f, 1.f);
                        widget->position = {point.x, point.y, point.z};
                        float x, y, z;
                        glm::extractEulerAngleXYZ(camera_to_world, x, y, z);
                        widget->rotation = {glm::degrees(x), glm::degrees(y), glm::degrees(z)};
                    } else {
                        const float unit = bgui::get_global_scale();
                        widget->position = {(mouse.x-r.x)/unit, (mouse.y-r.y)/unit, 0};
                    }
                    ctx->record_action([hud, widget] { std::erase(hud->elements, widget); }, [hud, widget] {
                        if (hud->elements.size() < 1024 && std::none_of(hud->elements.begin(), hud->elements.end(),
                            [&](const auto& element) { return element->id == widget->id; }))
                            hud->elements.push_back(widget);
                    });
                    ctx->select_entity(id);
                    ctx->selected_hud_widget = widget;
                    ++ctx->history_revision;
                });
            }
            show_action_popup(std::move(actions), alive);
        }
        right_down = right;
        if(drag>=0) {
            auto widget=dragging.lock();
            const bool active=widget && editing && widget==ctx->selected_hud_widget.lock();
            if(active) {
                const float unit=bgui::get_global_scale();
                const float dx=(mouse.x-start.x)/unit, dy=(mouse.y-start.y)/unit;
                moved=moved || dx!=0 || dy!=0;
                if(moved) {
                    float x=before.absolute ? before.position.x : (drag_bounds.x-r.x)/unit;
                    float y=before.absolute ? before.position.y : (drag_bounds.y-r.y)/unit;
                    const float width=before.absolute ? before.width : drag_bounds.z/unit;
                    const float height=before.absolute ? before.height : drag_bounds.w/unit;
                    widget->absolute=true;
                    widget->width=width;
                    widget->height=height;
                    if(drag==0) {
                        x+=dx;
                        y+=dy;
                    } else {
                        const bool left=((drag-1)&1)==0;
                        const bool top=((drag-1)&2)==0;
                        widget->width=std::clamp(width+(left ? -dx : dx),1.f,10000.f);
                        widget->height=std::clamp(height+(top ? -dy : dy),1.f,10000.f);
                        if(left) x+=width-widget->width;
                        if(top) y+=height-widget->height;
                    }
                    widget->position.x=x;
                    widget->position.y=y;
                    if(!pressed) {
                        const auto after=*widget;
                        const auto old=before;
                        ctx->record_action([widget,old]{*widget=old;},[widget,after]{*widget=after;});
                        ++ctx->history_revision;
                    }
                }
            } else if(widget && moved) {
                *widget=before;
            }
            if(!pressed || !active) { drag=-1; dragging.reset(); }
        }
        down=pressed;
        bgui::linear::on_update();
    }
};

class game_view_driver final : public bgui::element {
public:
    explicit game_view_driver(std::shared_ptr<game_view_state> state)
        : m_state(std::move(state)) {
        receives_input(false);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        update_game_view(m_state, true);
    }

private:
    std::shared_ptr<game_view_state> m_state;
};

}

game_view::game_view(std::shared_ptr<editor_context> context)
    : m_state(std::make_shared<game_view_state>()) {
    if (!context || !context->registry)
        throw std::invalid_argument("game_view requires an editor context");
    m_state->context = std::move(context);
}

void game_view::build(bgui::dock& dock) {
    auto& window = dock.add_window("Game", bgui::dock_area::center, 0.3f);
    window.add_class("editor-panel");
    m_state->window = &window;

    auto& content = window.add<bgui::linear>(bgui::orientation::vertical);
    content.add_class("editor-surface");
    content.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );
    content.style.layout.padding = bgui::vec4i{0};

    auto& image = content.add<bgui::image>();
    image.add_class("editor-viewport-surface");
    image.receives_input(false);
    image.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::stretch
    );
    m_state->image = &image;
    m_state->status = &content.add<bgui::text>(
        "Waiting for a game camera.",
        0.27f
    );
    m_state->status->add_class("project-manager-muted");

    const auto weak = std::weak_ptr<game_view_state>(m_state);
    auto& hud = content.add<commons::hud_view, bgui::layer::overlay>(
        m_state->context->registry, nullptr, image,
        [weak] { auto s = weak.lock(); return s && s->window->is_enabled(); },
        [weak] { auto s = weak.lock(); return s && s->camera ?
            std::shared_ptr<commons::camera>(s, &s->render_camera) : nullptr; });

    m_state->editor_surface = &content.add<hud_editor_surface, bgui::layer::overlay>(m_state, hud);

    bgui::get_layout().add<game_view_driver, bgui::layer::overlay>(m_state);
    update_game_view(m_state);
}

void game_view::update() {
    update_game_view(m_state);
}

bool game_view::needs_camera_render(const std::uint32_t entity) const noexcept {
    return m_state && m_state->context &&
        m_state->window && m_state->window->is_enabled() &&
        m_state->camera && m_state->camera_entity == entity;
}

}
