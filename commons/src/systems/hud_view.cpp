#include "ui/hud_view.hpp"
#include <cmath>
#include <filesystem>
#include <os/asset_manager.hpp>
#include <utils/logging.hpp>

namespace commons {
namespace {
float bounded(float v, float hi=10000.f) { return std::isfinite(v) ? std::clamp(v,0.f,hi) : 0.f; }
bgui::vec4 faded(bgui::vec4 c,float opacity) {
    for(int i=0;i<4;++i) c[i]=bounded(c[i],1.f);
    c.a*=opacity; return c;
}
class hud_node final : public bgui::linear {
public:
    std::shared_ptr<hud_element> data;
    bgui::element* visual=nullptr;
    std::string image_path;
    hud_node(std::shared_ptr<hud_element> e, float scale) : bgui::linear(e->kind=="row" ? bgui::orientation::horizontal : bgui::orientation::vertical), data(e) {
        style.visual.visible=false;
        style.layout.padding=bgui::vec4i{0};
        if(e->kind=="button") visual=&add<bgui::button>(e->text,scale,[weak=std::weak_ptr<hud_element>(e)]{if(auto node=weak.lock()) node->clicked=true;});
        else if(e->kind=="image") visual=&add<bgui::image>();
        else if(e->kind=="text") visual=&add<bgui::text>(e->text,scale);
        if(visual) { visual->set_flex(false); if(e->kind!="button") visual->receives_input(false); }
    }
    void calc_content_size(const bgui::layer& layer) override {
        bgui::linear::calc_content_size(layer);
        if(visual) {
            const auto children=get_content_size();
            visual->calc_content_size(layer);
            const auto content=visual->get_content_size();
            set_content_size({std::max(children.x,content.x),std::max(children.y,content.y)});
        }
    }
    void arrange() override {
        if(data->absolute) {
            auto* p=get_parent();
            auto origin=p->processed_rect();
            if(!data->relative) while(p->get_parent() && dynamic_cast<hud_node*>(p)) { p=p->get_parent(); origin=p->processed_rect(); }
            const float unit=bgui::get_global_scale();
            const auto x=std::isfinite(data->position.x) ? std::clamp(data->position.x,-100000.f,100000.f) : 0.f;
            const auto y=std::isfinite(data->position.y) ? std::clamp(data->position.y,-100000.f,100000.f) : 0.f;
            set_final_rect(origin.x+int(x*unit),origin.y+int(y*unit),int(std::max(1.f,bounded(data->width))*unit),int(std::max(1.f,bounded(data->height))*unit));
        }
        bgui::linear::arrange();
        if(visual) { const auto r=processed_rect(); visual->set_final_rect(r.x,r.y,r.z,r.w); visual->arrange(); }
    }
};
}

hud_view::hud_view(std::shared_ptr<ecs> registry, std::shared_ptr<camera> view_camera,
    bgui::element& viewport, std::function<bool()> active,
    std::function<std::shared_ptr<camera>()> camera_provider)
    : bgui::linear(bgui::orientation::vertical), m_registry(registry), m_camera(std::move(view_camera)), m_viewport(viewport), m_active(std::move(active)), m_camera_provider(std::move(camera_provider)) {
    set_flex(false); receives_input(false);
    style.visual.visible = false;
    style.layout.padding = bgui::vec4i{0};
}
void hud_view::on_update() {
    if (m_camera_provider) m_camera = m_camera_provider();
    const auto rect = m_viewport.processed_rect();
    set_final_rect(rect.x,rect.y,rect.z,rect.w);
    auto& nodes = m_nodes; nodes.clear();
    m_owners.clear();
    auto& parents = m_parents; parents.clear();
    if (auto registry = m_registry.lock(); registry && (!m_active || m_active())) {
        registry->cada<hud>([&](uint32_t id) {
            auto h = registry->get<hud>(id);
            if (!h->enabled) return;
            const auto first = static_cast<int>(nodes.size());
            for (std::size_t index = 0; index < h->elements.size(); ++index) {
                const auto& e = h->elements[index];
                nodes.push_back(e);
                m_owners.push_back(h);
                parents.push_back(e->parent >= 0 && std::size_t(e->parent) < index ? first + e->parent : -1);
            }
        });
    }
    if (nodes.empty()) set_final_rect(0,0,0,0);
    bool rebuild = nodes.size()!=m_bindings.size();
    if (!rebuild) for (size_t i=0;i<nodes.size();++i)
        rebuild |= nodes[i]!=m_bindings[i].data || nodes[i]->kind!=m_bindings[i].kind || nodes[i]->scale!=m_bindings[i].scale || parents[i]!=m_bindings[i].parent;
    if (rebuild) {
        for (auto& [layer, children] : get_elements()) {
            for (auto& child : children) bgui::cancel_interactions(child.get());
            children.clear();
        }
        m_bindings.clear();
        for (size_t i=0;i<nodes.size();++i) {
            auto e=nodes[i];
            bgui::layout* parent=this;
            if (parents[i]>=0 && parents[i]<static_cast<int>(m_bindings.size())) {
                if (auto layout=m_bindings[parents[i]].widget->as_layout()) parent=layout;
            }
            const float scale=std::isfinite(e->scale) ? std::clamp(e->scale,0.01f,10.f) : .35f;
            auto* widget=&parent->add<hud_node>(e,scale);
            widget->set_flex(!e->absolute);
            m_bindings.push_back({e,widget,e->scale,e->kind,parents[i]});
        }
        mark_layout_dirty();
    }
    m_opacity.resize(m_bindings.size());
    auto& opacity=m_opacity;
    for (size_t i=0;i<m_bindings.size();++i) {
        auto& b=m_bindings[i]; auto& e=*b.data;
        auto& node=static_cast<hud_node&>(*b.widget);
        const int parent=b.parent;
        const float alpha=bounded(e.opacity,1.f)*(parent>=0 ? opacity[parent] : 1.f);
        opacity[i]=alpha;
        b.widget->set_flex(!e.absolute);
        b.widget->set_enable(e.visible && (parent<0 || m_bindings[parent].widget->is_enabled()));
        b.widget->style.layout.padding=bgui::vec4i{int(bounded(e.padding))};
        b.widget->style.layout.margin=bgui::vec4i{int(bounded(e.margin))};
        const auto align=e.alignment==1 ? bgui::alignment::center : e.alignment==2 ? bgui::alignment::end : bgui::alignment::start;
        b.widget->style.layout.align=bgui::vec<2,bgui::alignment>{align,align};
        b.widget->style.layout.require_width(e.width>0 ? bgui::mode::pixel : bgui::mode::wrap_content,bounded(e.width));
        b.widget->style.layout.require_height(e.height>0 ? bgui::mode::pixel : bgui::mode::wrap_content,bounded(e.height));
        auto resolve=[&](const std::string& path) {
            return path.empty() ? std::string{} : (std::filesystem::path(m_owners[i]->asset_root)/path).lexically_normal().string();
        };
        auto* visual=node.visual ? node.visual : b.widget;
        visual->style.visual.text.normal=faded(e.color,alpha);
        visual->style.visual.background.normal=faded(e.background,alpha);
        visual->style.visual.background.hover=faded(e.hover_color,alpha);
        visual->style.visual.background.pressed=faded(e.pressed_color,alpha);
        visual->style.visual.border.normal=faded(e.border_color,alpha);
        visual->style.visual.border_size=bounded(e.border_size);
        visual->style.visual.border_radius=bounded(e.border_radius);
        visual->style.visual.visible=true;
        if(e.kind=="text") {
            b.widget->style.visual.visible=true;
            b.widget->style.visual.background.normal=faded(e.background,alpha);
            b.widget->style.visual.border.normal=faded(e.border_color,alpha);
            b.widget->style.visual.border_size=bounded(e.border_size);
            b.widget->style.visual.border_radius=bounded(e.border_radius);
        }
        bgui::text* text=dynamic_cast<bgui::text*>(visual);
        if(auto* button=dynamic_cast<bgui::button*>(visual)) {
            text=&button->get_label();
            button->style.layout.padding=bgui::vec4i{int(bounded(e.padding))};
            button->style.layout.align=bgui::vec<2,bgui::alignment>{align,align};
        }
        if(text) {
            text->style.layout.align=bgui::vec<2,bgui::alignment>{align,align};
            text->set_buffer(e.text);
            text->style.visual.text.normal=faded(e.color,alpha);
            text->style.visual.font=e.font.empty() ? "default" : resolve(e.font);
            text->mark_style_dirty();
            text->get_material().m_shader_tag=e.shader.empty() ? "ui::text" : "ui::fragment:text:"+resolve(e.shader);
        }
        auto* image=dynamic_cast<bgui::image*>(visual);
        const bool textured=image || (e.kind=="button" && !e.image.empty());
        if((image || e.kind=="button") && node.image_path!=resolve(e.image)) {
            node.image_path=resolve(e.image);
            bgui::texture texture{};
            try { if(!node.image_path.empty()) texture=bgui::asset_manager::get_instance().load_texture(node.image_path); }
            catch(const std::exception& error) { bgui::detail::log_err()<<"[HUD] "<<error.what()<<'\n'; }
            if(image) image->set_texture(texture);
            else visual->get_material().m_texture=texture;
        }
        const auto& texture=visual->get_material().m_texture;
        const bool has_texture=image || (textured && (texture.m_id!=0 || !texture.m_buffer.empty()));
        visual->get_material().m_use_tex=has_texture || dynamic_cast<bgui::text*>(visual);
        if(!dynamic_cast<bgui::text*>(visual)) visual->get_material().m_shader_tag=(!text && !e.shader.empty()) ? std::string("ui::fragment:")+(image ? "image:" : "default:")+resolve(e.shader) : (has_texture ? "ui::hud_image" : "ui::default");
        visual->mark_style_dirty();
        b.widget->mark_style_dirty();
    }
    bgui::linear::on_update();
    arrange();
}
}
