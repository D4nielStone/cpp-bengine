#include <ranges>
#include "editor_features.hpp"
#include <glad.h>
#include "scene_workflow.hpp"
#include "panel_manager.hpp"
#include "files_view.hpp"
#include "../services/scene_graph.hpp"
#include "../services/visual_lua.hpp"
#include <cmath>
#include <optional>
#include <array>
#include "../systems/viewport_input_state.hpp"
#include <chrono>
#include <cctype>
#include <elem/combo_box.hpp>

namespace editor::ui {
namespace fs=std::filesystem;
namespace vl=visual_lua;
namespace {
struct workflow_state {
    files_view& files;
    std::function<fs::path()> root;
    std::function<void(const fs::path&)> open_scene;
    std::function<bool()> editable;
    std::function<void(const fs::path&)> open_script;
    std::function<bool()> before_open;
    std::function<void(const fs::path&)> open_outline;
    fs::path current_root, script;
    vl::document document;
    std::string buffer, status;
    std::uint64_t selected{};
    std::size_t revision{}, graph_revision{}, scripts_revision{};
    std::shared_ptr<editor_context> context;
    void open(const fs::path& path,bool flush=true) {
        try {
            if(flush && before_open && !before_open()) return;
            const auto source=files.script_source(path);
            current_root=root(); script=fs::canonical(path); buffer=source; document.load(source); selected=0; ++revision;
            status="Arraste blocos para reordenar; use os encaixes para aninhar.";
        } catch(const std::exception& error) { status=error.what(); ++revision; }
    }
    void mutate(const std::function<bool(vl::document&)>& action) {
        if(script.empty() || !editable()) { status="Pare a execução para editar scripts."; ++revision; return; }
        auto previous=document;
        try {
            if(!action(document)) { status="Encaixe inválido: não é possível mover o bloco para esse destino."; ++revision; return; }
            const auto next=document.source();
            files.edit_script_source(script,buffer,next); buffer=next;
            ++revision; ++graph_revision; status="Alterações no documento. Salve para gravar o Lua.";
        } catch(const std::exception& error) { document=std::move(previous); status=error.what(); ++revision; }
    }
};

std::optional<std::size_t> assigned_script(const std::shared_ptr<editor_context>& context,
    editor_context::entity_id id,const fs::path& path) {
    const auto code=context->registry->get<commons::code>(id);
    if(!code) return {};
    auto matches=[&](const auto& script) {
        const fs::path file=script->file;
        return fs::weakly_canonical(file.is_absolute()?file:context->project_root/file)==fs::weakly_canonical(path);
    };
    if(matches(code)) return 0;
    for(std::size_t i=0;i<code->scripts.size();++i) if(matches(code->scripts[i])) return i+1;
    return {};
}
void choose_script_object(const std::shared_ptr<workflow_state>& state,const fs::path& path,bool remove) {
    const auto context=state->context;
    if(!context || !context->registry || !state->editable() || context->project_root!=state->root()) return;
    std::vector<editor_context::entity_id> ids;
    std::vector<std::string> labels;
    std::vector<std::shared_ptr<commons::transform>> identities;
    for(const auto& [id,components]:context->registry->entities) {
        if(!context->registry->is_alive(id) || context->locked_entities.contains(id) || context->editor_camera_entity==id) continue;
        if(remove && !assigned_script(context,id,path)) continue;
        ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());
    for(const auto id:ids) {
        identities.push_back(context->registry->get<commons::transform>(id));
        const auto name=context->registry->entity_names.find(id);
        labels.push_back((name==context->registry->entity_names.end()?"Objeto":name->second)+" (#"+std::to_string(id)+")");
    }
    if(ids.empty()) {state->status=remove?"Nenhum objeto usa este script.":"Nenhum objeto desbloqueado disponível.";++state->revision;return;}
    auto& modal=bgui::get_layout().add<bgui::modal,bgui::layer::overlay>();
    modal.add<bgui::text>(remove?"Remover script do objeto":"Aplicar script ao objeto",.30f);
    modal.add<bgui::text>(path.filename().string(),.30f);
    std::size_t selected=0;
    for(std::size_t i=0;i<ids.size();++i) if(context->selected_entity==ids[i]) selected=i;
    auto& objects=modal.content().add<bgui::combo_box>(labels,.30f,selected);
    objects.add_class("script-object-choice");
    objects.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
    modal.set_confirmation_text(remove?"Remover associação":"Aplicar script");
    modal.set_on_confirm([weak=std::weak_ptr<workflow_state>(state),root=state->root(),path,remove,ids,identities,choice=&objects] {
        const auto state=weak.lock();
        if(!state || state->root()!=root || !state->editable() || !state->context || state->context->project_root!=root) return;
        try {
            const auto index=choice->get_selected_index(); if(index>=ids.size()) return;
            const auto id=ids[index];
            const auto context=state->context;
            if(!context->registry->is_alive(id) || context->registry->get<commons::transform>(id)!=identities[index])
                throw std::runtime_error("O objeto foi removido ou substituído.");
            std::string error;
            if(remove) {
                const auto slot=assigned_script(context,id,path);
                if(!slot || !remove_lua_script(context,id,*slot,error))
                    throw std::runtime_error(error.empty()?"O objeto não usa mais este script.":error);
            } else {
                (void)asset_files::editable_file(path,root);
                if(state->files.has_document_edits(path)) throw std::runtime_error("Salve o Lua antes de aplicar o script ao objeto.");
                if(assigned_script(context,id,path)) throw std::runtime_error("Este script já está associado ao objeto.");
                if(!attach_lua_script(context,id,path,error)) throw std::runtime_error(error);
            }
            state->status=remove?"Associação removida. A ação pode ser desfeita.":"Script aplicado ao objeto. A ação pode ser desfeita.";
            ++state->graph_revision;
        } catch(const std::exception& error) {state->status=error.what();}
        ++state->revision;
    });
    modal.add<bgui::button>("Cancelar",.30f,[surface=&modal]{bgui::add_function([surface]{bgui::get_layout().remove(surface);});});
}
context_actions script_actions(const std::shared_ptr<workflow_state>& state,const fs::path& path) {
    if(!state->editable() || state->root().empty()) return {};
    const auto weak=std::weak_ptr<workflow_state>(state);
    const auto root=state->root();
    context_actions actions{{"Novo script Lua...",[weak,root] {
        const auto s=weak.lock(); if(!s || s->root()!=root || !s->editable()) return;
        if(s->before_open && !s->before_open()) return;
        try {
            const auto folder=fs::is_directory(root/"scripts")?root/"scripts":root;
            s->files.request_new_script(folder,[weak,root](const auto& created) {
                if(auto s=weak.lock();s && s->root()==root) {++s->graph_revision;++s->scripts_revision;if(s->open_script) s->open_script(created);}
            });
        } catch(const std::exception& error) {s->status=error.what();++s->revision;}
    }}};
    if(path.empty()) return actions;
    if(state->context) for(const bool remove:{false,true}) actions.emplace_back(remove?"Remover de objeto...":"Aplicar a objeto...",[weak,root,path,remove] {
        if(auto s=weak.lock();s && s->root()==root && s->editable()) {
            if(s->before_open && !s->before_open()) return;
            try {choose_script_object(s,path,remove);} catch(const std::exception& e) {s->status=e.what();++s->revision;}
        }
    });
    actions.emplace_back("Excluir arquivo de script...",[weak,root,path] {
        const auto s=weak.lock();if(!s || s->root()!=root || !s->editable()) return;
        if(s->before_open && !s->before_open()) return;
        try {
            const auto file=asset_files::editable_file(path,root);
            s->files.request_delete_script(file,[weak,root,file] {
                if(auto s=weak.lock();s && s->root()==root) {
                    if(s->script==file) {
                        s->script.clear();s->buffer.clear();s->document.load("");s->selected=0;
                    }
                    s->status="Arquivo de script excluído.";++s->revision;++s->graph_revision;++s->scripts_revision;
                }
            });
        } catch(const std::exception& e) {s->status=e.what();++s->revision;}
    });
    return actions;
}
class workflow_panel : public panel {
    class refresh_driver final : public bgui::element {
        workflow_panel& owner;
    public:
        explicit refresh_driver(workflow_panel& panel):owner(panel) {
            set_flex(false);
            receives_input(false);
        }
        void on_update() override { owner.context_input(); owner.update(); }
        void get_requires(bgui::draw_data*) override {}
    };
protected:
    std::shared_ptr<workflow_state> state;
    std::shared_ptr<bool> context_lifetime=std::make_shared<bool>(true);
    bool right_down{};
    virtual context_actions context_options() {return {};}
    void context_input() {
        const bool right=bgui::get_pressed(bgui::input_key::mouse_right);
        const bool pressed=right && !right_down; right_down=right;
        if(!pressed || !attached() || !dock->is_enabled() || !window->is_enabled() || viewport_input_blocked_by_modal()) return;
        bool inside=false;
        for(auto* target=bgui::get_mouse_target();target;target=target->get_parent()) {
            if(auto* button=dynamic_cast<context_action_button*>(target);button && button->context_options) return;
            if(target==window) inside=true;
        }
        if(inside) {
            auto actions=context_options();
            if(!actions.empty()) show_action_popup(std::move(actions),context_lifetime);
        }
    }
    bgui::dock* dock{};
    bgui::window* window{};
    bool attached() const {
        if(!dock) return false;
        for(const auto& [layer,children]:dock->get_elements()) for(const auto& c:children) if(c.get()==window) return true;
        return false;
    }
    void init(bgui::dock& d,bgui::panel_facade& facade) {
        context_lifetime=std::make_shared<bool>(true);
        dock=&d; window=&facade.native(); window->add_class("editor-panel");
        facade.content().add<refresh_driver,bgui::layer::overlay>(*this);
    }
public:
    explicit workflow_panel(std::shared_ptr<workflow_state> s):state(std::move(s)){}
    ~workflow_panel() override { if(attached()) dock->remove_window(window); }
    void show() { if(!attached()) build(*dock); window->set_enable(true); }
};
class scripts_panel final : public workflow_panel {
    bgui::linear* list{};
    bgui::inputbox* filter{};
    bgui::text* status{};
    fs::path root;
    std::string search;
    bool refresh{true};
    std::size_t seen_scripts=~std::size_t{};
    context_actions context_options() override {return script_actions(state,{});}
public:
    using workflow_panel::workflow_panel;
    void show() { workflow_panel::show(); refresh=true; }
    void build(bgui::dock& d) override {
        bgui::panel_facade facade(d,"Scripts do projeto",bgui::dock_area::left,.30f); init(d,facade);
        auto body=facade.content(); body.native().set_scrollable(false);
        body.label("Todos os arquivos .lua",.3f);
        filter=&body.add<bgui::inputbox>("","Filtrar caminho ou nome",.30f,[this](const std::string&){refresh=true;});
        filter->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        body.action("Atualizar scripts",[this]{refresh=true; ++state->graph_revision;});
        status=&body.label("",.26f);
        list=&body.add<bgui::linear>(bgui::orientation::vertical);
        list->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch); list->set_scrollable(true);
        refresh=true;
    }
    void update() override {
        if(!attached()) return;
        const auto next=state->root();
        if(root!=next) { root=next; refresh=true; }
        if(search!=filter->get_buffer()) { search=filter->get_buffer(); refresh=true; }
        if(seen_scripts!=state->scripts_revision) {seen_scripts=state->scripts_revision;refresh=true;}
        if(!refresh) return; refresh=false;
        for(auto& [layer,children]:list->get_elements()) while(!children.empty()) list->remove(children.back().get());
        if(root.empty()) return;
        try {
            const auto scripts=project_scripts(root);
            status->set_buffer(std::to_string(scripts.size())+" scripts no projeto");
            for(const auto& path:scripts) {
                const auto relative=path.lexically_relative(root).generic_string();
                if(!search.empty() && relative.find(search)==std::string::npos) continue;
                auto& button=list->add<context_action_button>(path.filename().string(),.33f,[s=state,path]{s->open_script(path);});
                button.context_options=[s=state,path]{return script_actions(s,path);};
                button.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                button.get_label().style.layout.align={bgui::alignment::start,bgui::alignment::center};
                button.get_label().set_wrap(true);
                button.get_label().style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                if (path.parent_path()!=root) {
                    button.get_label().set_buffer(path.filename().string() + "\n" +
                        path.parent_path().lexically_relative(root).generic_string());
                }
            }
        } catch(const std::exception& error) { status->set_buffer(error.what()); }
    }
};
class graph_canvas final : public bgui::layout {
    class node_button final : public bgui::button {
        graph_canvas& canvas;
        std::size_t index;
        bgui::vec2i start;
        bgui::vec2 original;
        bool moved{};
    public:
        node_button(graph_canvas& c,std::size_t i,const std::string& title):bgui::button(title,.34f,[]{}),canvas(c),index(i){}
        void on_pressed() override {
            bgui::button::on_pressed(); start=bgui::get_mouse_position(); moved=false;
            original={canvas.graph.nodes[index].x,canvas.graph.nodes[index].y};
        }
        void set_drag(const bgui::vec2i&) override {
            const auto delta=bgui::get_mouse_position()-start;
            if(std::abs(delta.x)+std::abs(delta.y)<3 && !moved) return;
            moved=true;
            auto& n=canvas.graph.nodes[index]; n.x=original.x+delta.x/canvas.zoom; n.y=original.y-delta.y/canvas.zoom;
            canvas.mark_layout_dirty(); canvas.mark_render_dirty();
        }
        void on_released() override {
            bgui::button::on_released();
            if(canvas.select) canvas.select(index);
            if(moved && canvas.store) canvas.store();
        }
    };
    std::vector<node_button*> buttons;
    bgui::vec2i pan_start;
    bgui::vec2 original_pan;
public:
    scene_graph graph;
    float zoom{1.f};
    bgui::vec2 pan{0,0};
    std::function<void(std::size_t)> select;
    std::function<void()> store;
    graph_canvas() { type="scene-connection-canvas"; receives_input(true); style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch); style.layout.limit_min={240,180}; }
    bool clips_children() const override { return true; }
    bool supports_render_cache() const override { return false; }
    bgui::vec2 screen(float x,float y) const { return {processed_x()+processed_width()/2.f+pan.x+x*zoom,processed_y()+processed_height()/2.f+pan.y-y*zoom}; }
    void fit_view() {
        if(graph.nodes.empty() || processed_width()<=0 || processed_height()<=0) return;
        float minx=graph.nodes[0].x,maxx=minx+190,miny=graph.nodes[0].y-54,maxy=graph.nodes[0].y;
        for(const auto& n:graph.nodes) {minx=std::min(minx,n.x);maxx=std::max(maxx,n.x+190);miny=std::min(miny,n.y-54);maxy=std::max(maxy,n.y);}
        zoom=std::clamp(std::min((processed_width()-40.f)/(maxx-minx),(processed_height()-40.f)/(maxy-miny)),.1f,1.2f);
        pan={-(minx+maxx)*.5f*zoom,(miny+maxy)*.5f*zoom}; mark_layout_dirty();
    }
    void populate(scene_graph next) {
        for(auto& [layer,children]:get_elements()) while(!children.empty()) remove(children.back().get());
        buttons.clear(); graph=std::move(next);
        for(std::size_t i=0;i<graph.nodes.size();++i) {
            auto& node=add<node_button>(*this,i,graph.nodes[i].path.filename().string());
            node.style.visual.background.normal=bgui::color{.16f,.16f,.16f,1};
            node.style.visual.text.normal=bgui::color{.95f,.95f,.95f,1};
            node.set_flex(false); buttons.push_back(&node);
        }
        mark_layout_dirty();
    }
    void arrange() override {
        clear_layout_dirty();
        for(std::size_t i=0;i<buttons.size();++i) {
            const auto p=screen(graph.nodes[i].x,graph.nodes[i].y);
            buttons[i]->set_position(int(p.x),int(p.y)); buttons[i]->set_final_size(int(190*zoom),int(54*zoom)); buttons[i]->arrange();
        }
    }
    void on_pressed() override { bgui::layout::on_pressed(); pan_start=bgui::get_mouse_position(); original_pan=pan; }
    void set_drag(const bgui::vec2i&) override {
        const auto delta=bgui::get_mouse_position()-pan_start;
        pan={original_pan.x+delta.x,original_pan.y+delta.y}; mark_layout_dirty();
    }
    void get_requires(bgui::draw_data* data) override {
        bgui::element::get_requires(data);
        auto& draw=data->m_draw_list;
        const auto clip=data->m_clip_rect;
        data->m_clip_rect=bgui::intersect_rect(clip,processed_rect());
        const auto saved=draw.get_color();
        const auto draw_clip=draw.get_clip_rect(); const bool had_clip=draw.has_clip_rect();
        draw.set_clip_rect(clip.z>0 && clip.w>0?bgui::intersect_rect(clip,processed_rect()):processed_rect());
        draw.set_color({.05f,.05f,.05f,1});
        draw.add_rect_filled({float(processed_x()),float(processed_y())},{float(processed_x()+processed_width()),float(processed_y()+processed_height())});
        const float grid=50*zoom;
        const auto zero=screen(0,0);
        draw.set_color({.09f,.09f,.09f,1});
        for(float x=processed_x()+std::fmod(zero.x-processed_x(),grid);x<processed_x()+processed_width();x+=grid) draw.add_line({x,float(processed_y())},{x,float(processed_y()+processed_height())});
        for(float y=processed_y()+std::fmod(zero.y-processed_y(),grid);y<processed_y()+processed_height();y+=grid) draw.add_line({float(processed_x()),y},{float(processed_x()+processed_width()),y});
        draw.set_color({.18f,.18f,.18f,1});
        draw.add_line({zero.x,float(processed_y())},{zero.x,float(processed_y()+processed_height())},2);
        draw.add_line({float(processed_x()),zero.y},{float(processed_x()+processed_width()),zero.y},2);
        for(const auto& edge:graph.edges) {
            if(edge.unresolved) continue;
            const auto& from=graph.nodes[edge.from]; const auto& to=graph.nodes[edge.to];
            auto a=screen(from.x+190,from.y-27), b=screen(to.x,to.y-27);
            draw.set_color({.50f,.50f,.50f,1});
            if(edge.from==edge.to) {
                draw.add_polyline({a,{a.x+28,a.y},{a.x+28,a.y-65},{b.x-25,b.y-65},{b.x-25,b.y},b},2);
            } else draw.add_line(a,b,2);
            const float angle=edge.from==edge.to?0.f:std::atan2(b.y-a.y,b.x-a.x);
            draw.add_line(b,{b.x-12*std::cos(angle-.4f),b.y-12*std::sin(angle-.4f)},2);
            draw.add_line(b,{b.x-12*std::cos(angle+.4f),b.y-12*std::sin(angle+.4f)},2);
        }
        draw.set_color(saved);
        for(auto* node:buttons) if(node->is_enabled()) node->get_cached_requires(data);
        data->m_clip_rect=clip;
        if(had_clip) draw.set_clip_rect(draw_clip); else draw.clear_clip_rect();
    }
};
class scene_outline_panel final : public workflow_panel {
    bgui::linear* selector{};
    bgui::inputbox* editor{};
    bgui::text* identity{};
    bgui::text* status{};
    bgui::button* create{};
    fs::path root, scene, document;
    std::string buffer;
    std::size_t seen=~std::size_t{};
    bool loaded{};

    bool sync() {
        if(!loaded || root!=state->root()) return true;
        const auto& next=editor->get_buffer();
        if(next==buffer) return true;
        try {
            if(!state->editable()) {
                editor->set_buffer(buffer);
                return false;
            }
            state->files.edit_text_source(document,buffer,next);
            buffer=next;
            status->set_buffer("Rascunho recuperável. Use Salvar roteiro para gravar.");
            return true;
        } catch(const std::exception& error) {
            status->set_buffer(error.what());
            return false;
        }
    }
    void select(const fs::path& path) {
        if(!sync()) return;
        scene=path; document=fs::path(path.string()+".roteiro.txt");
        loaded=false; buffer.clear(); editor->set_buffer("");
        identity->set_buffer(scene.lexically_relative(root).generic_string());
        try {
            (void)scene_files::checked_scene(root,scene);
            if(fs::exists(fs::symlink_status(document))) {
                buffer=state->files.text_source(document);
                loaded=true; editor->set_buffer(buffer);
                status->set_buffer("Objetivos, sequência de eventos e tarefas de desenvolvimento.");
            } else status->set_buffer("Esta cena ainda não tem roteiro. Use Criar roteiro.");
        } catch(const std::exception& error) {status->set_buffer(error.what());}
        create->set_enable(!loaded);
        editor->receives_input(loaded && state->editable());
    }
public:
    using workflow_panel::workflow_panel;
    void build(bgui::dock& d) override {
        bgui::panel_facade facade(d,"Roteiro da cena",bgui::dock_area::right,.30f);
        init(d,facade);
        auto body=facade.content(); body.native().set_scrollable(false);
        body.label("Roteiro de desenvolvimento",.36f);
        selector=&body.add<bgui::linear>(bgui::orientation::vertical);
        selector->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        identity=&body.label("Selecione uma cena no grafo ou na lista acima.",.28f);
        auto actions=body.row();
        create=&actions.action("Criar roteiro",[this]{
            if(scene.empty() || root!=state->root() || !state->editable()) return;
            try {
                (void)scene_files::checked_scene(root,scene);
                if(fs::exists(fs::symlink_status(document)))
                    throw std::runtime_error("O roteiro já existe. Selecione novamente a cena para abri-lo.");
                if(!scene_files::inside(fs::weakly_canonical(document),fs::canonical(root)))
                    throw std::runtime_error("Roteiro fora do projeto.");
                std::string error;
                if(!project_files::save(document,{{document,""}},error)) throw std::runtime_error(error);
                state->files.refresh_files();
                select(scene);
            } catch(const std::exception& error) {status->set_buffer(error.what());}
        });
        actions.action("Salvar roteiro",[this]{
            if(!loaded || root!=state->root() || !state->editable() || !sync()) return;
            try {
                (void)scene_files::checked_scene(root,scene);
                state->files.save_text_source(document,buffer);
                status->set_buffer("Roteiro salvo.");
            } catch(const std::exception& error) {status->set_buffer(error.what());}
        });
        status=&body.label("",.27f);
        editor=&body.add<bgui::inputbox>("","Objetivos da cena\nEventos e diálogos\nTarefas de desenvolvimento",.30f,
            [this](const std::string&){sync();},bgui::input_mode::multiline);
        editor->type="scene-development-outline";
        editor->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch);
        editor->set_scrollable(true); editor->set_wrap(true);
        editor->receives_input(false);
        seen=~std::size_t{}; root.clear(); loaded=false;
        state->open_outline=[this](const fs::path& path){show();update();select(path);};
    }
    ~scene_outline_panel() override {state->open_outline={};}
    void update() override {
        if(!attached()) return;
        const auto next=state->root();
        if(root!=next || seen!=state->graph_revision) {
            if(root==next && !sync()) return;
            const bool same=root==next;
            root=next; seen=state->graph_revision;
            if(!same) {
                loaded=false; scene.clear(); document.clear(); buffer.clear(); editor->set_buffer("");
                identity->set_buffer("Selecione uma cena.");
                status->set_buffer(""); create->set_enable(false);
            }
            for(auto& [layer,children]:selector->get_elements())
                while(!children.empty()) selector->remove(children.back().get());
            if(!root.empty()) try {
                const auto scenes=scene_files::list(root,true);
                std::vector<std::string> labels{"Selecionar cena..."};
                std::size_t index=0;
                for(const auto& p:scenes) {
                    labels.push_back(p.lexically_relative(root).generic_string());
                    if(p==scene) index=labels.size()-1;
                }
                auto& choices=selector->add<bgui::combo_box>(labels,.30f,index);
                choices.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                choices.set_on_change([this,scenes](std::size_t i,const std::string&){
                    if(i>0 && i<=scenes.size()) select(scenes[i-1]);
                });
            } catch(const std::exception& error) {status->set_buffer(error.what());}
        }
        if(!sync()) return;
        if(loaded) try {
            const auto& current=state->files.text_source(document);
            if(current!=buffer) {buffer=current;editor->set_buffer(buffer);}
        } catch(const std::exception& error) {status->set_buffer(error.what());}
        editor->receives_input(loaded && state->editable());
        if(!state->editable()) editor->set_focused(false);
    }
};
class source_control_panel final : public workflow_panel {
    bgui::text* project{};
    fs::path root;
public:
    using workflow_panel::workflow_panel;
    void build(bgui::dock& d) override {
        bgui::panel_facade facade(d,"Source Control",bgui::dock_area::left,.30f); init(d,facade);
        auto body=facade.content();
        body.label("Source Control · Git",.36f);
        project=&body.label("Nenhum projeto aberto",.27f);
        body.label("Integração Git ainda não disponível.",.30f);
        body.label("Branch · indisponível",.28f);
        body.label("Alterações preparadas",.32f);
        body.label("Nenhum status consultado.",.28f);
        body.label("Alterações no projeto",.32f);
        body.label("A lista de arquivos será exibida após a integração Git.",.28f);
        body.label("Commit",.32f);
        auto& message=body.add<bgui::inputbox>("","Mensagem do commit",.30f);
        message.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        message.receives_input(false);
        body.label("Preparar, descartar e criar commits estarão disponíveis em uma próxima etapa.",.27f);
        root.clear();
    }
    void update() override {
        if(!attached()) return;
        const auto next=state->root();
        if(next!=root) {root=next;project->set_buffer(root.empty()?"Nenhum projeto aberto":root.filename().string());}
    }
};
class diff_panel final : public workflow_panel {
public:
    using workflow_panel::workflow_panel;
    void build(bgui::dock& d) override {
        bgui::panel_facade facade(d,"Diff",bgui::dock_area::bottom,.30f); init(d,facade);
        auto body=facade.content(); body.native().set_scrollable(false);
        body.label("Comparação de alterações",.36f);
        body.label("Aguardando integração Git e seleção de um arquivo no Source Control.",.28f);
        auto columns=body.row();
        columns.native().style.layout.require_height(bgui::mode::stretch);
        for(const auto* title:{"Original · HEAD","Versão de trabalho"}) {
            auto column=columns.column();
            column.native().style.layout.require_mode(bgui::mode::stretch,bgui::mode::match_parent);
            column.label(title,.30f);
            auto& content=column.add<bgui::inputbox>("","Nenhum arquivo selecionado",.30f,nullptr,bgui::input_mode::multiline);
            content.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch);
            content.set_scrollable(true); content.receives_input(false);
        }
    }
    void update() override {}
};

class graph_panel final : public workflow_panel {
    graph_canvas* canvas{};
    bgui::text* status{};
    bgui::linear* details{};
    fs::path root;
    std::size_t seen=~std::size_t{};
    std::optional<std::size_t> selected;
    void store() {
        try {
            rapidjson::Document doc; doc.SetObject(); auto& a=doc.GetAllocator();
            doc.AddMember("version",1,a); rapidjson::Value nodes(rapidjson::kObjectType);
            for(const auto& node:canvas->graph.nodes) {
                const auto key=node.path.lexically_relative(root).generic_string();
                rapidjson::Value p(rapidjson::kArrayType); p.PushBack(node.x,a).PushBack(node.y,a);
                nodes.AddMember(rapidjson::Value(key.c_str(),a),p,a);
            }
            doc.AddMember("positions",nodes,a);
            project_files::stage({root/".editor-scene-graph.json",project_files::encode(doc)});
            project_files::replace(root/".editor-scene-graph.json");
        } catch(const std::exception& error) { status->set_buffer(error.what()); }
    }
    void inspect(std::size_t i) {
        if(i>=canvas->graph.nodes.size()) return;
        selected=i;
        for (auto& [layer, children] : canvas->get_elements()) for (std::size_t n=0; n<children.size(); ++n) {
            children[n]->style.visual.border.normal = n == i
                ? bgui::color{.50f,.50f,.50f,1.f} : bgui::color{.28f,.28f,.28f,1.f};
            children[n]->mark_style_dirty();
        }
        for(auto& [layer,children]:details->get_elements()) while(!children.empty()) details->remove(children.back().get());
        const auto& node=canvas->graph.nodes[i];
        details->add<bgui::button>("Editar roteiro",.30f,[s=state,path=node.path]{if(s->open_outline) s->open_outline(path);});
        details->add<bgui::text>(node.path.lexically_relative(root).generic_string()+" | X "+std::to_string(int(node.x))+" / Y "+std::to_string(int(node.y)),.30f);
        details->add<bgui::button>("Abrir cena",.30f,[s=state,path=node.path]{s->open_scene(path);});
        for(const auto& edge:canvas->graph.edges) if(edge.from==i) {
            const auto label=(edge.unresolved?"? ":"-> ")+(edge.target.empty()?"Destino calculado em Lua":edge.target)+" · "+edge.script.filename().string();
            details->add<bgui::button>(label,.30f,[s=state,file=edge.script]{s->open_script(file);});
        }
        for(const auto& file:node.scripts) details->add<bgui::button>("Script: "+file.lexically_relative(root).generic_string(),.30f,[s=state,file]{s->open_script(file);});
    }
public:
    using workflow_panel::workflow_panel;
    void build(bgui::dock& d) override {
        root.clear(); selected.reset();
        bgui::panel_facade facade(d,"Conexões entre cenas",bgui::dock_area::center,.30f); init(d,facade);
        auto body=facade.content(); body.native().set_scrollable(false);
        auto& controls=body.add<bgui::grid>(bgui::vec2i{105,28},bgui::vec2i{4,4});
        controls.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        const auto action=[&](const std::string& text,std::function<void()> f){controls.add<bgui::button>(text,.30f,std::move(f));};
        action("Atualizar grafo",[this]{++state->graph_revision;});
        action("-",[this]{canvas->zoom=std::max(.1f,canvas->zoom/1.2f); canvas->mark_layout_dirty();});
        action("+",[this]{canvas->zoom=std::min(2.f,canvas->zoom*1.2f); canvas->mark_layout_dirty();});
        action("Enquadrar",[this]{canvas->fit_view();});
        action("Origem (0, 0)",[this]{canvas->pan={0,0}; canvas->zoom=1; canvas->mark_layout_dirty();});
        body.label("X → / Y ↑ · arraste cenas; arraste o fundo para navegar",.26f);
        status=&body.label("",.25f);
        canvas=&body.add<graph_canvas>(); canvas->select=[this](std::size_t i){inspect(i);}; canvas->store=[this]{store();};
        details=&body.add<bgui::linear>(bgui::orientation::vertical);
        details->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::pixel); details->style.layout.require_height(bgui::mode::pixel,140); details->set_scrollable(true);
        auto config=d.get_configuration(); config.left_ratio=.18f; config.right_ratio=.44f; d.apply_configuration(config);
        for(const auto& [layer,children]:d.get_elements())
            for(const auto& child:children)
                if(auto* list=dynamic_cast<bgui::window*>(child.get());list && list!=window && list->get_title().get_buffer()==tr("Cenas")) {
                    d.merge_window_as_tab(*list,*window);
                    break;
                }
        seen=~std::size_t{};
    }
    void update() override {
        if(!attached()) return;
        const auto next=state->root();
        if(root==next && seen==state->graph_revision) return;
        const bool same=root==next; root=next; seen=state->graph_revision;
        if(root.empty()) { canvas->populate({}); return; }
        try {
            auto graph=read_scene_graph(root,[s=state](const auto& p){return s->files.document_contents(p);});
            std::map<fs::path,bgui::vec2> positions;
            if(same) for(const auto& n:canvas->graph.nodes) positions[n.path]={n.x,n.y};
            const auto file=root/".editor-scene-graph.json";
            if(!same && fs::is_regular_file(file)) {
                rapidjson::Document doc; const auto text=project_files::read(file); doc.Parse(text.data(),text.size());
                if(!doc.HasParseError() && doc.IsObject() && doc.HasMember("positions") && doc["positions"].IsObject())
                    for(const auto& p:std::ranges::subrange(doc["positions"].MemberBegin(), doc["positions"].MemberEnd()))
                        if(p.value.IsArray() && p.value.Size()==2 && p.value[0].IsNumber() && p.value[1].IsNumber()) {
                            const float x=p.value[0].GetFloat(), y=p.value[1].GetFloat();
                            if(std::isfinite(x) && std::isfinite(y) && std::abs(x)<1.e6f && std::abs(y)<1.e6f) positions[(root/p.name.GetString()).lexically_normal()]={x,y};
                        }
            }
            for(auto& n:graph.nodes) if(positions.contains(n.path)) { n.x=positions[n.path].x; n.y=positions[n.path].y; }
            const auto warnings=graph.diagnostics.size()+std::count_if(graph.edges.begin(),graph.edges.end(),[](const auto& e){return e.unresolved;});
            status->set_buffer(std::to_string(graph.nodes.size())+" cenas · "+std::to_string(graph.edges.size())+" conexões · "+std::to_string(warnings)+" pendências"+(graph.diagnostics.empty()?"":" · "+graph.diagnostics.front()));
            canvas->populate(std::move(graph));
            if(selected && *selected<canvas->graph.nodes.size()) inspect(*selected);
        } catch(const std::exception& error) { status->set_buffer(error.what()); }
    }
};
// Owned by the root overlay; no borrowed block/widget references survive a rebuild.
class block_drag_preview final : public bgui::linear {
    bgui::text* caption;
    bgui::text* values;
    std::function<bool()> active;
    bgui::vec2i last{};
    float angle{}, velocity{};
    std::chrono::steady_clock::time_point tick;
public:
    explicit block_drag_preview(std::function<bool()> visible):bgui::linear(bgui::orientation::vertical),active(std::move(visible)) {
        type="visual-block-drag-preview";
        style.visual.background.normal=bgui::color{.18f,.18f,.18f,.98f};
        style.visual.border.normal=bgui::color{.50f,.50f,.50f,1};style.visual.border_size=1.f;style.visual.border_radius=7.f;
        style.layout.require_mode(bgui::mode::pixel,bgui::mode::pixel);style.layout.require_size(420,80);
        set_flex(false); receives_input(false); set_input_passthrough(true);
        style.visual.visible=false;set_enable(false);
        caption=&add<bgui::text>("",.30f);
        values=&add<bgui::text>("",.30f);
        for(auto* t:{caption,values}) {t->set_flex(false); t->set_wrap(false); t->receives_input(false);}
    }
    void begin(const vl::block& b) {
        caption->set_buffer(vl::title(b));
        style.visual.background.normal=b.type==vl::kind::function
            ? bgui::color{.46f,.29f,.10f,.98f} : bgui::color{.17f,.31f,.48f,.98f};
        std::string text;
        for(const auto& f:vl::slots(b)) {
            if(!text.empty()) text+="  |  ";
            auto value=(f.tail?b.tail:b.head).substr(f.begin,f.end-f.begin);
            if(value.size()>26) value=value.substr(0,23)+"...";
            text+=f.label+": "+value;
        }
        if(!b.children.empty()) text+="  (+"+std::to_string(b.children.size())+" blocos)";
        values->set_buffer(text);
        last=bgui::get_mouse_position(); tick=std::chrono::steady_clock::now(); angle=.12f; velocity=0;
        style.visual.visible=true;set_enable(true);mark_style_dirty();
    }
    void stop() {if(!is_enabled()) return;style.visual.visible=false;set_enable(false);mark_style_dirty();}
    bool supports_render_cache() const override {return false;}
    void on_update() override {
        if(!style.visual.visible.value_or(false)) return;
        if(!active() || !bgui::get_pressed(bgui::input_key::mouse_left)) {stop();return;}
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::clamp(std::chrono::duration<float>(now-tick).count(),0.f,.04f); tick=now;
        const auto mouse=bgui::get_mouse_position();
        velocity-=std::clamp(float(mouse.x-last.x)*.018f,-.5f,.5f); last=mouse;
        velocity+=(-32.f*angle-8.f*velocity)*dt;
        angle=std::clamp(angle+velocity*dt,-.32f,.32f);
        const auto size=bgui::get_context().m_size;
        const float scale=bgui::get_global_scale();
        const int width=std::min(int(420*scale),std::max(1,size.x-16));
        const int height=std::min(int(80*scale),std::max(1,size.y-16));
        const int x=std::clamp(mouse.x+int((-40+std::sin(angle)*45)*scale),8,std::max(8,size.x-width-8));
        const int y=std::clamp(mouse.y+int((20+(1-std::cos(angle))*45)*scale),8,std::max(8,size.y-height-8));
        style.layout.require_size(width/scale,height/scale);compute_style();
        set_final_rect(x,y,width,height);
        caption->set_final_rect(x+int(14*scale),y+int(10*scale),std::max(1,width-int(28*scale)),int(26*scale));
        values->set_final_rect(x+int(14*scale),y+int(42*scale),std::max(1,width-int(28*scale)),int(26*scale));
        mark_render_dirty();
        bgui::linear::on_update();
    }
    void get_requires(bgui::draw_data* data) override {
        if(!style.visual.visible.value_or(false)) return;
        auto& draw=data->m_draw_list; const auto color=draw.get_color();
        const float x=processed_x(),y=processed_y(),w=processed_width(),h=processed_height();
        const auto mouse=bgui::get_mouse_position();
        draw.set_color({.80f,.85f,.92f,.65f});
        draw.add_line({float(mouse.x),float(mouse.y)},{x+40*bgui::get_global_scale(),y},2.f);
        draw.set_color({0,0,0,.28f});draw.add_rect_filled({x+4,y+5},{x+w+4,y+h+5});
        draw.set_color(color);
        bgui::linear::get_requires(data);
    }
};
bgui::color category_color(const std::string& category) {
    if(category=="Eventos") return {.46f,.29f,.10f,1};
    if(category=="Controle") return {.43f,.26f,.12f,1};
    if(category=="Propriedades") return {.34f,.23f,.48f,1};
    if(category=="Física") return {.17f,.36f,.47f,1};
    if(category=="Câmera") return {.19f,.38f,.32f,1};
    if(category=="Animação") return {.42f,.22f,.35f,1};
    return {.17f,.31f,.48f,1};
}
class category_button final : public bgui::button {
    bgui::color accent;
public:
    category_button(const std::string& name,std::function<void()> action):bgui::button(name,.30f,std::move(action)),accent(category_color(name)) {
        style.layout.padding={25,7,5,7};
        get_label().style.layout.align={bgui::alignment::start,bgui::alignment::center};
    }
    void get_requires(bgui::draw_data* data) override {
        bgui::button::get_requires(data);
        auto& draw=data->m_draw_list;const auto color=draw.get_color();draw.set_color(accent);
        const float scale=bgui::get_global_scale();
        draw.add_circle_filled({processed_x()+12*scale,processed_y()+processed_height()/2.f},4*scale,12);
        draw.set_color(color);
    }
};
class block_tree final : public bgui::linear {
public:
    block_tree():bgui::linear(bgui::orientation::vertical) {
        type="visual-script-tree";
        style.layout.padding={24,20,24,30};
        style.visual.background.normal=bgui::color{0,0,0,0};style.visual.border_size=0.f;
    }
    void restore_scroll(int offset) {m_scroll_offset=offset;mark_layout_dirty();}
    void on_pressed() override {bgui::linear::on_pressed();bgui::focus_keyboard(*this);}
    void arrange() override {
        const float scale=bgui::get_global_scale();
        const float width=std::clamp((processed_width()-computed_style.layout.padding.x-computed_style.layout.padding.z)/scale,40.f,580.f);
        for(auto& [layer,children]:get_elements()) for(auto& child:children) {
            if(!child->style.layout.size || child->style.layout.size->x!=width) {
                child->style.layout.require_width(bgui::mode::pixel,width);child->compute_style();
            }
        }
        bgui::linear::arrange();
    }
    void get_requires(bgui::draw_data* data) override {
        auto& draw=data->m_draw_list;const auto color=draw.get_color();const auto clip=draw.get_clip_rect();const bool clipped=draw.has_clip_rect();
        draw.set_clip_rect(data->m_clip_rect.z>0?bgui::intersect_rect(data->m_clip_rect,processed_rect()):processed_rect());
        draw.set_color({.05f,.05f,.05f,1});draw.add_rect_filled({float(processed_x()),float(processed_y())},{float(processed_x()+processed_width()),float(processed_y()+processed_height())});
        const float step=24*bgui::get_global_scale();draw.set_color({.13f,.13f,.13f,1});
        for(float x=processed_x()+12;x<processed_x()+processed_width();x+=step)
            for(float y=processed_y()+12-std::fmod(float(get_scroll_offset()),step);y<processed_y()+processed_height();y+=step)
                draw.add_rect_filled({x,y},{x+1,y+1});
        draw.set_color(color);if(clipped) draw.set_clip_rect(clip);else draw.clear_clip_rect();
        bgui::linear::get_requires(data);
    }
};
class block_group final : public bgui::linear {
    bool selected{};
public:
    block_group():bgui::linear(bgui::orientation::vertical) {type="visual-block-group";}

    void select(bool value) {if(selected!=value){selected=value;mark_render_dirty();}}
    void get_requires(bgui::draw_data* data) override {
        bgui::linear::get_requires(data);
        auto& draw=data->m_draw_list;const auto color=draw.get_color();
        const float scale=bgui::get_global_scale();
        const float x=processed_x(),y=processed_y(),w=processed_width(),h=processed_height();
        // Matching socket and tongue follow layout geometry at every nesting level.
        draw.set_color({.07f,.09f,.12f,1});
        draw.add_convexpolyfilled({{x+28*scale,y},{x+34*scale,y+5*scale},
            {x+54*scale,y+5*scale},{x+60*scale,y}});
        draw.add_convexpolyfilled({{x,y+h-5*scale},{x+28*scale,y+h-5*scale},
            {x+34*scale,y+h},{x,y+h}});
        draw.add_convexpolyfilled({{x+60*scale,y+h-5*scale},{x+w,y+h-5*scale},
            {x+w,y+h},{x+54*scale,y+h}});
        if(!selected) {draw.set_color(color);return;}
        draw.set_color({.95f,.79f,.38f,1.f});
        draw.add_rect({float(processed_x()+1),float(processed_y()+1)},
            {float(processed_x()+processed_width()-1),float(processed_y()+processed_height()-1)},2.f);
        draw.set_color(color);
    }
};
class blocks_panel final : public workflow_panel {
    context_actions context_options() override {return script_actions(state,state->script);}
    class handle final : public bgui::button {
        blocks_panel& owner;
        std::uint64_t id;
        std::optional<std::size_t> command_index;
        std::optional<vl::kind> structure_kind;
        bgui::vec2i start;
        bool dragged{};
    public:
        handle(blocks_panel& p,std::uint64_t block,const std::string& title):bgui::button(title,.34f,[]{}),owner(p),id(block){}
        handle(blocks_panel& p,std::size_t index,const vl::command& c):bgui::button(c.title,.34f,[]{}),owner(p),id(0),command_index(index) {
            m_orientation=bgui::orientation::vertical;
            style.visual.background.normal=category_color(c.category);style.visual.border_radius=6.f;
            style.layout.padding={10,9,10,9};style.layout.margin={0,2,0,6};
            const auto model=vl::parse(c.source);std::string hint;
            if(!model.empty()) for(const auto& f:vl::slots(model.front())) {if(!hint.empty()) hint+="  ";hint+=f.label;}
            if(hint.empty()) hint="Sem argumentos";
            auto& info=add<bgui::text>(hint,.30f);info.receives_input(false);info.set_wrap(false);
            get_label().style.layout.align={bgui::alignment::start,bgui::alignment::start};
            info.style.layout.align={bgui::alignment::start,bgui::alignment::start};
        }
        handle(blocks_panel& p,vl::kind structure):bgui::button(vl::label(structure),.33f,[]{}),owner(p),id(0),structure_kind(structure) {
            style.visual.background.normal=category_color("Controle");style.visual.border_radius=6.f;
            style.layout.padding={10,10,10,10};style.layout.margin={0,2,0,6};
        }
        void on_pressed() override { bgui::button::on_pressed(); owner.sync_fields(); start=bgui::get_mouse_position(); dragged=false; if(!command_index && !structure_kind) owner.select(id); }
        void set_drag(const bgui::vec2i&) override {
            if(!owner.state->editable() || owner.state->script.empty()) return;
            const auto d=bgui::get_mouse_position()-start;
            if(!dragged && std::abs(d.x)+std::abs(d.y)>5) {
                dragged=true;
                if(command_index) {const auto b=vl::parse(vl::commands()[*command_index].source);if(!b.empty()) owner.preview->begin(b.front());}
                else if(structure_kind) owner.preview->begin(vl::make(*structure_kind,0));
                else if(const auto* b=owner.state->document.find(id)) owner.preview->begin(*b);
            }
        }
        void get_requires(bgui::draw_data* data) override {
            bgui::button::get_requires(data);
            auto& draw=data->m_draw_list; const auto color=draw.get_color();
            const float x=processed_x()+16.f, y=processed_y()+processed_height()-1.f;
            draw.set_color({.70f,.70f,.70f,1});
            draw.add_polyline({{x,y},{x+4,y+4},{x+14,y+4},{x+18,y}},1.5f);
            draw.set_color(color);
        }
        void on_released() override {
            bgui::button::on_released();
            owner.preview->stop();
            if(dragged) owner.drop(id,bgui::get_mouse_position(),command_index,structure_kind);
            else if(command_index) owner.add_command(*command_index);
            else if(structure_kind) owner.add(*structure_kind);
            else owner.select(id);
        }
    };
    class tracked_field final : public bgui::field {
    public:
        using bgui::field::field;
        std::function<void()> changed;
        void on_update() override {
            bgui::field::on_update();
            if(get_inputbox().is_focused() && changed) changed();
        }
    };
    struct target { bgui::element* widget; std::uint64_t parent,before; };
    struct field_binding { bgui::inputbox* input; std::uint64_t id; vl::slot slot; std::string original, head, tail, observed; };
    std::vector<field_binding> fields;
    bool sync_fields() {
        if(state->script.empty() || !state->editable()) return true;
        std::set<std::uint64_t> modified;
        for(const auto& field:fields) if(field.input->get_buffer()!=field.observed) modified.insert(field.id);
        if(modified.empty()) return true;
        auto previous=state->document;
        std::map<std::uint64_t,std::vector<const field_binding*>> changed;
        for(const auto& field:fields) if(modified.contains(field.id)) changed[field.id].push_back(&field);
        bool any=false;
        for(auto& [id,bindings]:changed) {
            auto head=bindings.front()->head, tail=bindings.front()->tail;
            std::sort(bindings.begin(),bindings.end(),[](const auto* a,const auto* b){return a->slot.begin>b->slot.begin;});
            for(const auto* f:bindings) {
                auto& text=f->slot.tail?tail:head;
                text.replace(f->slot.begin,f->slot.end-f->slot.begin,f->input->get_buffer());
            }
            const auto* current=state->document.find(id);
            if(current && (current->head!=head || current->tail!=tail)) {state->document.replace(id,head,tail); any=true;}
        }
        if(!any) {for(auto& f:fields) f.observed=f.input->get_buffer(); return true;}
        try {
            state->files.edit_script_source(state->script,state->buffer,state->document.source());
            state->buffer=state->document.source(); ++state->graph_revision;
            for(auto& f:fields) f.observed=f.input->get_buffer();
            state->status="Alterações no documento. Salve para gravar o Lua.";
            return true;
        } catch(const std::exception& error) {state->document=std::move(previous);state->status=error.what();return false;}
    }
    block_drag_preview* preview{};
    bgui::linear* command_palette{};
    std::string category="Objeto", command_query;
    bgui::inputbox* command_filter{};
    bgui::text* category_title{};
    std::vector<std::pair<std::string,category_button*>> category_buttons;
    bool palette_dirty=true;
    void select_category(const std::string& name) {
        category=name;palette_dirty=true;
        for(auto& [title,button]:category_buttons) {
            button->style.visual.background.normal=title==name ? bgui::color{.22f,.22f,.22f,1} : bgui::color{.08f,.08f,.08f,1};
            button->style.visual.border_size=title==name?1.f:0.f;button->mark_style_dirty();
        }
    }
    void populate_palette() {
        for(auto& [layer,children]:command_palette->get_elements()) while(!children.empty()) command_palette->remove(children.back().get());
        category_title->set_buffer(category);
        const auto& catalog=vl::commands();
        for(std::size_t i=0;i<catalog.size();++i) if(catalog[i].category==category) {
            auto name=catalog[i].title+" "+catalog[i].source, query=command_query;
            for(auto* text:{&name,&query}) for(auto& c:*text) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(query.empty() || name.find(query)!=std::string::npos) {
                auto& button=command_palette->add<handle>(*this,i,catalog[i]);
                button.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            }
        }
        if(category=="Controle") {
            for(auto k:{vl::kind::condition,vl::kind::branch,vl::kind::loop,vl::kind::while_loop,vl::kind::repeat,vl::kind::scope,vl::kind::assignment,vl::kind::command}) {
                auto& b=command_palette->add<handle>(*this,k);
                b.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                b.style.layout.margin={0,2,0,6};b.style.visual.background.normal=category_color(category);
            }
        }
        command_palette->cascade_style();palette_dirty=false;
    }
    void add_command(std::size_t index) {
        if(!sync_fields()) return;
        const auto [parent,before]=insertion();
        state->mutate([=](auto& d){return d.insert_command(parent,index,before);});
        bgui::focus_keyboard(*tree);
    }
    bgui::modal* reload_modal{};
    std::vector<target> targets;
    block_tree* tree{};
    struct block_widgets {std::uint64_t id;block_group* group;bgui::linear* content;bgui::button* toggle;};
    std::vector<block_widgets> widgets;
    std::set<std::uint64_t> collapsed;
    fs::path displayed_script;
    std::string clipboard;
    std::array<bool,11> key_down{};
    void select(std::uint64_t id) {
        state->selected=id;
        for(const auto& w:widgets) w.group->select(w.id==id);
        bgui::focus_keyboard(*tree);
    }
    void fold(std::uint64_t id,bool closed) {
        if(closed) collapsed.insert(id);else collapsed.erase(id);
        for(const auto& w:widgets) if(w.id==id) {
            w.content->set_enable(!closed);w.toggle->get_label().set_buffer(closed?"+":"-");break;
        }
        select(id);
    }
    std::pair<std::uint64_t,std::uint64_t> insertion(bool sibling=false) const {
        const auto* selected=state->document.find(state->selected);
        if(!selected) return {0,0};
        if(!sibling && (!selected->tail.empty() || selected->type==vl::kind::branch)) return {selected->id,0};
        std::pair<std::uint64_t,std::uint64_t> result{0,0};
        auto visit=[&](auto&& self,const std::vector<vl::block>& list,std::uint64_t parent)->void {
            for(std::size_t i=0;i<list.size();++i) {
                if(list[i].id==selected->id) result={parent,i+1<list.size()?list[i+1].id:0};
                self(self,list[i].children,list[i].id);
            }
        };visit(visit,state->document.blocks(),0);return result;
    }
    void save() {
        if(!sync_fields()) return;
        try {
            if(!state->editable()) throw std::runtime_error("Pare a execução antes de salvar.");
            std::string error;
            if(!vl::validate(state->buffer,error)) throw std::runtime_error(error);
            state->files.save_script_source(state->script,state->buffer);state->status="Lua salvo.";++state->graph_revision;
        } catch(const std::exception& e) {state->status=e.what();}
    }
    void history(bool redo) {
        if(!sync_fields()) return;
        collapsed.clear();state->mutate([=](auto& d){return redo?d.redo():d.undo();});
        select(0);
    }
    void shortcuts() {
        constexpr std::array keys{bgui::input_key::z,bgui::input_key::y,bgui::input_key::c,bgui::input_key::v,
            bgui::input_key::x,bgui::input_key::d,bgui::input_key::s,bgui::input_key::delete_key,
            bgui::input_key::left,bgui::input_key::right,bgui::input_key::space};
        const bool control=bgui::get_pressed(bgui::input_key::left_control)||bgui::get_pressed(bgui::input_key::right_control);
        const bool shift=bgui::get_pressed(bgui::input_key::left_shift)||bgui::get_pressed(bgui::input_key::right_shift);
        auto* focus=bgui::get_keyboard_focus();bool inside=false;
        for(auto* e=focus;e;e=e->get_parent()) if(e==tree) inside=true;
        auto* input=dynamic_cast<bgui::inputbox*>(focus);
        for(std::size_t i=0;i<keys.size();++i) {
            const bool down=bgui::get_pressed(keys[i]);const bool pressed=down&&!key_down[i];key_down[i]=down;
            if(!pressed || !inside || viewport_input_blocked_by_modal() || !state->editable() || state->script.empty()) continue;
            if(input && !(control && (i==6 || ((i==0 || i==1) && input->get_input_mode()!=bgui::input_mode::multiline)))) continue;
            if(i<7 && !control) continue;
            if(i>=8 && control) continue;
            if(!sync_fields()) continue;
            if(i==0 || i==1) {
                history(i==1||shift);
            } else if(i==6) save();
            else if(i==7) erase_selected();
            else if(i>=8) fold(state->selected,i==8 || (i==10 && !collapsed.contains(state->selected)));
            else {
                auto& io=bgui::get_context();
                try {
                    if(i==2 || i==4) {
                        const auto text=state->document.copy(state->selected);
                        if(text.empty()) continue;
                        if(io.m_set_clipboard) io.m_set_clipboard(text);clipboard=text;
                        if(i==4) state->mutate([&](auto& d){return d.erase(state->selected);});
                    } else {
                        const bool duplicate=i==5;
                        const auto text=duplicate?state->document.copy(state->selected):(io.m_get_clipboard?io.m_get_clipboard():clipboard);
                        const auto [parent,before]=insertion(duplicate);
                        if(text.empty()) continue;
                        std::string error;
                        state->mutate([&](auto& d){return d.paste(parent,text,before,&error);});
                        if(!error.empty()) state->status="Trecho Lua inválido nesse destino: "+error;
                    }
                } catch(const std::exception& e) {state->status=e.what();}
            }
        }
    }
    bgui::text* identity{};
    bgui::text* status{};
    std::size_t seen=~std::size_t{};
    void drop(std::uint64_t id,const bgui::vec2i& position,std::optional<std::size_t> command_index={},std::optional<vl::kind> structure={}) {
        // Body slots are visited after their parent header; the deepest visible
        // target wins. References are consumed before the next UI rebuild.
        if(!sync_fields()) return;
        const auto viewport=tree->processed_rect();
        if(position.x<viewport.x || position.y<viewport.y || position.x>=viewport.x+viewport.z || position.y>=viewport.y+viewport.w) return;
        for(auto it=targets.rbegin();it!=targets.rend();++it) {
            bool enabled=true;
            for(auto* e=it->widget;e && e!=tree;e=e->get_parent()) if(!e->is_enabled()) enabled=false;
            if(!enabled) continue;
            const auto r=it->widget->processed_rect();
            if(position.x<r.x || position.y<r.y || position.x>=r.x+r.z || position.y>=r.y+r.w) continue;
            const auto parent=it->parent,before=it->before;
            state->mutate([=](auto& d){if(structure) return d.insert(parent,*structure,before);return command_index?d.insert_command(parent,*command_index,before):d.move(id,parent,before);});
            bgui::focus_keyboard(*tree);return;
        }
    }
    void render(bgui::linear& parent,const std::vector<vl::block>& values,std::uint64_t parent_id) {
        for(const auto& b:values) {
            if(b.type==vl::kind::raw && std::all_of(b.head.begin(),b.head.end(),
                [](unsigned char c){return std::isspace(c);})) continue;
            auto& group=parent.add<block_group>();
            group.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            group.style.layout.padding={8,7,5,9}; group.style.layout.margin={0,0,0,1};
            bgui::color color=category_color("Objeto");
            if(const auto* command=vl::describe(b)) color=category_color(command->category);
            else if(b.type==vl::kind::function) color=category_color("Eventos");
            else if(!b.tail.empty() || b.type==vl::kind::branch) color=category_color("Controle");
            else if(b.type==vl::kind::assignment) color=category_color("Propriedades");
            else if(b.type==vl::kind::raw) color={.22f,.25f,.29f,1};
            group.style.visual.background.normal=color; group.style.visual.border_radius=5;
            group.select(state->selected==b.id);
            auto& heading=group.add<bgui::linear>(bgui::orientation::horizontal);
            heading.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            auto& toggle=heading.add<bgui::button>(collapsed.contains(b.id)?"+":"-",.30f,[this,id=b.id]{fold(id,!collapsed.contains(id));});
            toggle.style.layout.require_mode(bgui::mode::pixel,bgui::mode::pixel);
            toggle.style.layout.require_size(28,32);
            auto& title=heading.add<handle>(*this,b.id,":: "+vl::title(b));
            title.style.visual.background.normal=color;
            toggle.style.visual.background.normal=color;
            title.style.layout.require_mode(bgui::mode::stretch,bgui::mode::pixel); title.style.layout.require_height(bgui::mode::pixel,32);
            targets.push_back({&title,parent_id,b.id});
            auto& content=group.add<bgui::linear>(bgui::orientation::vertical);
            content.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            content.set_enable(!collapsed.contains(b.id));
            widgets.push_back({b.id,&group,&content,&toggle});
            const auto specs=vl::slots(b);
            bgui::linear* components=nullptr;
            for(std::size_t index=0;index<specs.size();++index) {
                const auto& slot=specs[index]; const auto& source=slot.tail?b.tail:b.head;
                const bool multiline=b.type==vl::kind::raw || source.substr(slot.begin,slot.end-slot.begin).find('\n')!=std::string::npos;
                if(slot.label=="X" || slot.label=="R") {
                    components=&content.add<bgui::linear>(bgui::orientation::horizontal);
                    components->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                } else if(slot.label!="Y" && slot.label!="Z" && slot.label!="G" && slot.label!="B" && slot.label!="A") components=nullptr;
                auto& field=(components?*components:content).add<tracked_field>(slot.label,source.substr(slot.begin,slot.end-slot.begin),.33f,
                    [this](const std::string&){sync_fields();},"",
                    multiline?bgui::input_mode::multiline:bgui::input_mode::inputbox);
                field.changed=[this]{if(seen==state->revision) sync_fields();};
                field.style.layout.require_mode(components?bgui::mode::stretch:bgui::mode::match_parent,bgui::mode::wrap_content);
                field.get_label().style.layout.require_mode(bgui::mode::pixel,bgui::mode::wrap_content);
                field.get_label().style.layout.require_width(bgui::mode::pixel, components ? 18.f : 144.f);
                field.get_label().style.layout.align={bgui::alignment::start,bgui::alignment::center};
                field.style.layout.margin={4,3,4,3};
                field.get_inputbox().style.layout.require_mode(bgui::mode::stretch,bgui::mode::pixel);
                field.get_inputbox().style.layout.require_height(bgui::mode::pixel,multiline?100:30);
                field.get_inputbox().set_scrollable(true);
                field.get_inputbox().set_wrap(false);
                fields.push_back({&field.get_inputbox(),b.id,slot,source.substr(slot.begin,slot.end-slot.begin),b.head,b.tail,source.substr(slot.begin,slot.end-slot.begin)});
            }
            if(!b.tail.empty() || b.type==vl::kind::branch) {
                auto& body=content.add<bgui::linear>(bgui::orientation::vertical);
                body.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                body.type="visual-block-body";
                body.style.layout.margin={18,4,0,3};
                body.style.layout.padding={6,5,4,5};
                body.style.visual.background.normal=bgui::color{.07f,.09f,.12f,1};
                body.style.visual.border_radius=3;
                render(body,b.children,b.id);
                auto& slot=body.add<bgui::button>("+ Encaixar aqui",.30f,[this,id=b.id]{select(id);});
                slot.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::pixel); slot.style.layout.require_height(bgui::mode::pixel,24);
                slot.style.visual.background.normal=bgui::color{.12f,.16f,.20f,1};
                slot.style.visual.border.normal=color; slot.style.visual.border_size=1;
                targets.push_back({&slot,b.id,0});
                auto& closing=content.add<bgui::text>(b.type==vl::kind::function?"Fim da função":b.type==vl::kind::branch?"Fim do ramo":"Fim do bloco",.30f);
                closing.receives_input(false);
            }
        }
    }
    void rebuild() {
        preview->stop();
        const bool same_script=displayed_script==state->script;
        const int scroll=same_script?tree->get_scroll_offset():0;
        if(!same_script) collapsed.clear();
        displayed_script=state->script;
        widgets.clear();targets.clear(); fields.clear();
        for(auto& [layer,children]:tree->get_elements()) while(!children.empty()) tree->remove(children.back().get());
        identity->set_buffer(state->script.empty()?"Selecione um script no painel de scripts":state->script.filename().string()+" · Lua ↔ blocos");
        status->set_buffer(state->status);
        if(!state->script.empty()) render(*tree,state->document.blocks(),0);
        auto& end=tree->add<bgui::button>("+ Nível principal",.30f,[this]{select(0);});
        targets.push_back({&end,0,0});
        tree->cascade_style();
        tree->restore_scroll(scroll);
    }
    void add(vl::kind kind) {
        if(!sync_fields()) return;
        const auto [parent,before]=insertion();
        state->mutate([=](auto& doc){return doc.insert(parent,kind,before);});
        bgui::focus_keyboard(*tree);
    }
    void erase_selected() {
        if(!sync_fields()) return;
        state->mutate([&](auto& d){return d.erase(state->selected);});
        select(0);
    }

public:
    using workflow_panel::workflow_panel;
    ~blocks_panel() override { if(preview) bgui::get_layout().remove(preview); if(reload_modal) bgui::get_layout().remove(reload_modal); state->before_open={}; state->open_script={}; }
    void build(bgui::dock& d) override {
        widgets.clear();category_buttons.clear();fields.clear(); targets.clear();
        if(preview) bgui::get_layout().remove(preview);
        preview=&bgui::get_layout().add<block_drag_preview,bgui::layer::overlay>([this]{return attached() && dock->is_enabled() && window->is_enabled();});
        state->before_open=[this]{return !attached() || sync_fields();};
        bgui::panel_facade facade(d,"Script em blocos",bgui::dock_area::center,.30f); init(d,facade);
        auto body=facade.content(); body.native().set_scrollable(false);
        identity=&body.label("",.36f);
        auto& action_grid=body.add<bgui::grid>(bgui::vec2i{132,32},bgui::vec2i{4,4});
        action_grid.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        const auto action=[&](const std::string& label,std::function<void()> f){auto& button=action_grid.add<bgui::button>(tr(label),.30f,std::move(f));
            if(label=="Salvar Lua") button.add_class("primary");
            if(label=="Excluir bloco") button.add_class("danger");};
        action("Salvar Lua",[this]{save();bgui::focus_keyboard(*tree);});
        action("Reler do disco",[this,s=state]{
            if(reload_modal || s->script.empty() || !s->editable()) return;
            auto& modal=bgui::get_layout().add<bgui::modal,bgui::layer::overlay>(); reload_modal=&modal;
            modal.add<bgui::text>("Descartar as alterações deste script nos editores de texto e blocos e reler o arquivo do disco?",.30f);
            modal.set_confirmation_text("Descartar e reler");
            modal.set_on_confirm([this,s,path=s->script,root=s->root()]{
                reload_modal=nullptr;
                if(s->script!=path || s->root()!=root || !s->editable()) return;
                try {s->files.discard_script_draft_and_reload(path);s->open(path,false);}
                catch(const std::exception& e){s->status=e.what();++s->revision;}
            });
            modal.add<bgui::button>("Cancelar",.30f,[this,p=&modal]{reload_modal=nullptr;bgui::add_function([p]{bgui::get_layout().remove(p);});});
        });
        action("Desfazer",[this]{history(false);});
        action("Refazer",[this]{history(true);});
        action("Excluir bloco",[this]{erase_selected();});
        status=&body.label("",.30f);
        auto& workbench=body.add<bgui::linear>(bgui::orientation::horizontal);
        workbench.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch);
        auto& categories=workbench.add<bgui::linear>(bgui::orientation::vertical);
        categories.type="visual-script-categories";categories.set_scrollable(true);
        categories.style.layout.require_mode(bgui::mode::pixel,bgui::mode::match_parent);categories.style.layout.require_width(bgui::mode::pixel,144);
        categories.style.layout.padding={3,6,3,6};
        for(const std::string name:{"Objeto","Propriedades","Eventos","Controle","Debug","Scenes","Time","Entrada","Física","Câmera","Luzes","Animação","Interface","Matemática","Navegação","Compatibilidade"}) {
            auto& button=categories.add<category_button>(name,[this,name]{select_category(name);});
            button.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            category_buttons.emplace_back(name,&button);
        }
        auto& palette=workbench.add<bgui::linear>(bgui::orientation::vertical);
        palette.type="visual-script-palette";
        palette.style.layout.require_mode(bgui::mode::pixel,bgui::mode::match_parent);palette.style.layout.require_width(bgui::mode::pixel,230);
        palette.style.layout.padding={10,8,10,8};
        category_title=&palette.add<bgui::text>("Objeto",.30f);
        command_query.clear();
        command_filter=&palette.add<bgui::inputbox>("","Buscar comando",.30f,[](const std::string&){});
        command_filter->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        command_palette=&palette.add<bgui::linear>(bgui::orientation::vertical);
        command_palette->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch);command_palette->set_scrollable(true);
        auto& assembly=workbench.add<bgui::linear>(bgui::orientation::vertical);
        assembly.type="visual-script-assembly";
        assembly.style.layout.require_mode(bgui::mode::stretch,bgui::mode::match_parent);
        auto& hint=assembly.add<bgui::text>("Arraste os blocos e encaixe na sequência. Selecione para editar.",.30f);
        hint.style.layout.margin={12,6,6,6};
        tree=&assembly.add<block_tree>();
        tree->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::stretch);tree->set_scrollable(true);
        select_category("Objeto");
        seen=~std::size_t{};
    }
    void update() override {
        if(!attached()) {if(preview) preview->stop();return;}
        if(command_filter->get_buffer()!=command_query) {command_query=command_filter->get_buffer();palette_dirty=true;}
        if(palette_dirty) populate_palette();
        const auto root=state->root();
        if(root!=state->current_root) {
            state->current_root=root; state->script.clear(); state->buffer.clear(); state->document.load(""); state->selected=0; ++state->revision; ++state->graph_revision;
        }
        if(!state->script.empty()) {
            if(seen==state->revision && !sync_fields()) {status->set_buffer(state->status);return;}
            try {
                const auto current=state->files.script_source(state->script);
                if(current!=state->buffer) { state->buffer=current; state->document.load(current); collapsed.clear();state->selected=0; ++state->revision; ++state->graph_revision; }
            } catch(const std::exception& error) { state->status=error.what(); }
        }
        shortcuts();
        if(seen!=state->revision) { seen=state->revision; rebuild(); }
        else status->set_buffer(state->status);
        for(auto& field:fields) {
            field.input->receives_input(state->editable());
            if(!state->editable()) field.input->set_focused(false);
        }
    }
};
}
scene_workflow_actions install_scene_workflow(panel_manager& scene_manager,panel_manager& script_manager,files_view& files,
    std::function<fs::path()> root,std::function<void(const fs::path&)> open_scene,std::function<bool()> editable,
    std::function<void()> activate_scripting,std::shared_ptr<editor_context> context) {
    auto state=std::make_shared<workflow_state>(workflow_state{files,std::move(root),std::move(open_scene),std::move(editable)});
    state->context=std::move(context);
    auto& graph=scene_manager.add<graph_panel>(state);
    auto& outline=scene_manager.add<scene_outline_panel>(state);
    auto& source_control=scene_manager.add<source_control_panel>(state);
    auto& diff=scene_manager.add<diff_panel>(state);
    auto& scripts=script_manager.add<scripts_panel>(state);
    auto& blocks=script_manager.add<blocks_panel>(state);
    state->open_script=[&blocks,s=std::weak_ptr<workflow_state>(state),activate=std::move(activate_scripting)](const fs::path& path){
        if(auto state=s.lock()) {
            if (!visual_scripting_enabled()) { state->files.open_text_document(path); return; }
            if(activate) activate();state->open(path);blocks.show();}
    };
    return {
        [&graph,&outline,&source_control,&diff,s=std::weak_ptr<workflow_state>(state)]{graph.show();outline.show();source_control.show();diff.show();if(auto state=s.lock()) ++state->graph_revision;},
        [&scripts,&blocks]{scripts.show();blocks.show();}
    };
}
std::function<void()> install_scene_workflow(panel_manager& manager,files_view& files,
    std::function<fs::path()> root,std::function<void(const fs::path&)> open_scene,std::function<bool()> editable) {
    auto actions=install_scene_workflow(manager,manager,files,std::move(root),std::move(open_scene),std::move(editable));
    return [actions=std::move(actions)]{actions.scenes();actions.scripting();};
}
}
