#include "ui/hud_view.hpp"
#include <cmath>

namespace commons {
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
    auto& parents = m_parents; parents.clear();
    if (auto registry = m_registry.lock(); registry && (!m_active || m_active())) {
        registry->cada<hud>([&](uint32_t id) {
            auto h = registry->get<hud>(id);
            if (!h->enabled) return;
            std::vector<int> indices(h->elements.size(), -1);
            for (std::size_t index = 0; index < h->elements.size(); ++index) {
                const auto& e = h->elements[index];
                if (e->kind == "text3d") continue;
                indices[index] = static_cast<int>(nodes.size());
                nodes.push_back(e);
                parents.push_back(e->parent >= 0 && std::size_t(e->parent) < index ? indices[e->parent] : -1);
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
            bgui::element* widget;
            const float scale=std::isfinite(e->scale) ? std::clamp(e->scale,0.01f,10.f) : .35f;
            if (e->kind=="row" || e->kind=="column") widget=&parent->add<bgui::linear>(e->kind=="row" ? bgui::orientation::horizontal : bgui::orientation::vertical);
            else if (e->kind=="button") widget=&parent->add<bgui::button>(e->text,scale,[weak=std::weak_ptr<hud_element>(e)] { if(auto node=weak.lock()) node->clicked=true; });
            else { auto& text=parent->add<bgui::text>(e->text,scale); text.receives_input(false); widget=&text; }
            if(e->absolute) widget->set_flex(false);
            m_bindings.push_back({e,widget,e->scale,e->kind,parents[i]});
        }
        mark_layout_dirty();
    }
    for (auto& b : m_bindings) {
        auto& e=*b.data;
        bool visible=e.visible;
        b.widget->set_flex(!e.absolute);
        b.widget->style.visual.text.normal = e.color;
        b.widget->style.layout.padding = bgui::vec4i{int(e.padding)};
        b.widget->style.layout.margin = bgui::vec4i{int(e.margin)};
        const auto align=e.alignment==1 ? bgui::alignment::center : e.alignment==2 ? bgui::alignment::end : bgui::alignment::start;
        b.widget->style.layout.align = bgui::vec<2,bgui::alignment>{align,align};
        if (auto button=dynamic_cast<bgui::button*>(b.widget)) button->get_label().style.visual.text.normal=e.color;
        if(e.absolute) {
            const float unit=bgui::get_global_scale();
            b.widget->set_final_rect(rect.x+int(e.position.x*unit),rect.y+int(e.position.y*unit),int(std::max(1.f,e.width)*unit),int(std::max(1.f,e.height)*unit));
        }
        b.widget->set_enable(visible);
        if (auto text=dynamic_cast<bgui::text*>(b.widget)) text->set_buffer(e.text);
        if (auto button=dynamic_cast<bgui::button*>(b.widget)) button->get_label().set_buffer(e.text);
        b.widget->style.layout.require_width(std::isfinite(e.width) && e.width>0 ? bgui::mode::pixel : bgui::mode::wrap_content, std::isfinite(e.width) ? std::clamp(e.width,0.f,10000.f) : 0.f);
        b.widget->style.layout.require_height(std::isfinite(e.height) && e.height>0 ? bgui::mode::pixel : bgui::mode::wrap_content, std::isfinite(e.height) ? std::clamp(e.height,0.f,10000.f) : 0.f);
    }
    bgui::linear::on_update();
}
}
