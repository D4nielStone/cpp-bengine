#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bgui.hpp>
#include <os/style_manager.hpp>
#include <engine_version.hpp>
#include <chrono>
#include <algorithm>
#include <utility>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using clock_type = std::chrono::steady_clock;
double milliseconds(clock_type::time_point a, clock_type::time_point b) {
    return std::chrono::duration<double, std::milli>(b-a).count();
}
struct counters { std::size_t measurements{}, generations{}; };
class measured_text final : public bgui::text {
    counters& counts;
public:
    measured_text(const std::string& value, counters& counts)
        : bgui::text(value, .3f), counts(counts) {}
    void calc_content_size(const bgui::layer& layer) override {
        ++counts.measurements;
        bgui::text::calc_content_size(layer);
    }
    void get_requires(bgui::draw_data* data) override {
        ++counts.generations;
        bgui::text::get_requires(data);
    }
};
struct scenario { const char* name; int count; int lines; bool dynamic; bool windows; bool widgets; };
const std::vector<scenario> scenarios{
    {"text_static_100",100,1,false,false,false},
    {"text_static_1000",1000,1,false,false,false},
    {"text_dynamic_100",100,1,true,false,false},
    {"text_dynamic_1000",1000,1,true,false,false},
    {"text_long_static",1,2000,false,false,false},
    {"text_long_dynamic",1,2000,true,false,false},
    {"widgets_100",100,1,false,false,true},
    {"widgets_1000",1000,1,false,false,true},
    {"windows_4",4,1,false,true,false},
    {"windows_32",32,1,false,true,false}
};
struct graphics {
    GLFWwindow* window{};
    graphics() {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed; a display is required");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
        window=glfwCreateWindow(1280,720,"Bengine benchmark",nullptr,nullptr);
        if (!window) { glfwTerminate(); throw std::runtime_error("OpenGL 3.3 context unavailable"); }
        glfwMakeContextCurrent(window);
        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            glfwDestroyWindow(window); glfwTerminate(); throw std::runtime_error("OpenGL loader failed");
        }
        glfwSwapInterval(0);
    }
    ~graphics() { glfwDestroyWindow(window); glfwTerminate(); }
};
struct renderer {
    renderer() { bgui::set_up_gl3(); bgui::set_font_antialiasing(true); }
    ~renderer() { bgui::shutdown_gl3(); }
};
struct query {
    GLuint id{};
    query() { glGenQueries(1,&id); }
    ~query() { glDeleteQueries(1,&id); }
};
void json_string(std::ostream& out,const std::string& value) {
    out << '"';
    for (unsigned char c:value) {
        if (c=='"' || c=='\\') out << '\\' << c;
        else if (c < 32) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
        else out << c;
    }
    out << '"';
}
int bounded(const char* value) {
    std::string text(value); std::size_t used{}; int result=std::stoi(text,&used);
    if (used!=text.size() || result<1 || result>10000) throw std::runtime_error("Frame counts must be 1..10000");
    return result;
}
void run(std::ostream& out,int frames,int warmup,const std::string& selected) {
    graphics context;
    // The bundled loader omits the OpenGL 3.3 timer query entry point.
    using query_result_fn = void (APIENTRY *)(GLuint, GLenum, GLuint64*);
    auto query_result = reinterpret_cast<query_result_fn>(glfwGetProcAddress("glGetQueryObjectui64v"));
    if (!query_result) throw std::runtime_error("GPU timer queries unavailable");
    constexpr GLenum time_elapsed = 0x88BF;
    renderer render;
    out << "{\"schema_version\":1,\"suite_version\":1,\"engine_version\":";
    json_string(out,BUBBLE_VERSION_COMPLETA_STR);
    out << ",\"build_type\":"; json_string(out,BENGINE_BENCHMARK_BUILD);
    out << ",\"compiler\":"; json_string(out,BENGINE_BENCHMARK_COMPILER);
    for (auto [name,token]:std::vector<std::pair<const char*,GLenum>>{{"gpu_vendor",GL_VENDOR},{"gpu_renderer",GL_RENDERER},{"gl_version",GL_VERSION}}) {
        out << ",\"" << name << "\":"; json_string(out,reinterpret_cast<const char*>(glGetString(token)));
    }
    out << ",\"font_antialiasing\":true,\"font_resolution\":40,\"font_scale\":0.3,\"resolution\":[1280,720],\"vsync\":false,\"synchronized\":true,\"warmup_frames\":" << warmup << ",\"scenarios\":[";
    bool first=true;
    for (const auto& spec:scenarios) {
        if (!selected.empty() && selected!=spec.name) continue;
        if (!first) out << ',';
        first=false;
        counters counts;
        bgui::scoped_interface interface;
        bgui::get_context().m_size={1280,720};
        bgui::get_context().m_mouse_position={-100,-100};
        bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());
        const auto& font = bgui::ft_load_font("benchmark",std::string(COMMONS_ASSET_DIR)+"/fonts/HackNerdFont-Regular.ttf",40);
        if (font.chs.find(U'A') == font.chs.end())
            throw std::runtime_error("Benchmark font glyphs were not loaded");
        bgui::font_manager::get_instance().set_default_font("benchmark");
        auto& root=bgui::get_layout();
        root.style.visual.font="benchmark";
        std::vector<measured_text*> labels;
        std::string content;
        for (int i=0;i<spec.lines;++i) content += "Bubble Engine: texto UTF-8 ação, widget 0123456789\n";
        for (int i=0;i<spec.count;++i) {
            if (spec.windows) {
                auto title="Benchmark window "+std::to_string(i);
                auto& window=root.add<bgui::window>(title.c_str(),.3f,true);
                window.set_position((i%8)*145,(i/8)*170);
                window.style.layout.require_size(220,165);
                auto& text=window.add<measured_text>(content,counts);
                labels.push_back(&text);
                window.add<bgui::button>("Action",.3f,[] {});
                window.add<bgui::checkbox>("Enabled",.3f);
            } else {
                // Fixed positions keep the workload independent of automatic wrapping.
                auto& panel=root.add<bgui::linear>(bgui::orientation::vertical);
                panel.set_position((i%10)*128,(i/10)*50);
                panel.style.layout.require_size(spec.lines>1?1260.f:126.f,spec.lines>1?700.f:48.f);
                if (spec.widgets) {
                    panel.add<bgui::button>("Action",.3f,[] {});
                    panel.add<bgui::checkbox>("Enabled",.3f);
                } else {
                    auto& text=panel.add<measured_text>(content,counts);
                    labels.push_back(&text);
                }
            }
        }
        bgui::load_font_queue();
        query timer;
        out << "{\"name\":"; json_string(out,spec.name);
        out << ",\"items\":" << spec.count << ",\"lines_per_text\":" << spec.lines
            << ",\"mutation\":"; json_string(out,spec.dynamic?"one_text_per_frame":"none");
        out << ",\"samples\":[";
        for (int frame=-warmup;frame<frames;++frame) {
            glfwPollEvents();
            counts={};
            const auto start=clock_type::now();
            if (spec.dynamic && !labels.empty()) labels.front()->set_buffer(content+std::to_string(frame));
            const auto mutation_end=clock_type::now();
            bgui::on_update();
            const auto updated=clock_type::now();
            const auto commands=bgui::get_draw_data()->m_quad_requires.size();
            const auto vertices=bgui::get_draw_data()->m_draw_list.get_vertices().size();
            glBeginQuery(time_elapsed,timer.id);
            glBindFramebuffer(GL_FRAMEBUFFER,0);
            glViewport(0,0,1280,720);
            bgui::gl3_clear();
            bgui::gl3_render(bgui::get_draw_data());
            glEndQuery(time_elapsed);
            const auto submitted=clock_type::now();
            glFinish();
            const auto finished=clock_type::now();
            GLuint64 gpu_ns{}; query_result(timer.id,GL_QUERY_RESULT,&gpu_ns);
            if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("OpenGL error during benchmark");
            if (frame>=0) {
                if (frame) out << ',';
                out << "{\"mutation_ms\":" << milliseconds(start,mutation_end)
                    << ",\"ui_ms\":" << milliseconds(mutation_end,updated)
                    << ",\"submit_ms\":" << milliseconds(updated,submitted)
                    << ",\"wait_ms\":" << milliseconds(submitted,finished)
                    << ",\"frame_ms\":" << milliseconds(start,finished)
                    << ",\"gpu_ms\":" << gpu_ns/1000000.0
                    << ",\"quad_commands\":" << commands << ",\"geometry_vertices\":" << vertices
                    << ",\"text_measurements\":" << counts.measurements
                    << ",\"text_generations\":" << counts.generations << '}';
            }
        }
        out << "]}";
    }
    out << "]}\n";
}
}
int main(int argc,char** argv) {
    try {
        int frames=120,warmup=30; std::string output,scenario;
        for (int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if (i+1>=argc) throw std::runtime_error("Each option requires a value");
            if (option=="--output") output=argv[++i];
            else if (option=="--frames") frames=bounded(argv[++i]);
            else if (option=="--warmup") warmup=bounded(argv[++i]);
            else if (option=="--scenario") scenario=argv[++i];
            else throw std::runtime_error("Unknown benchmark option: "+option);
        }
        if (output.empty()) throw std::runtime_error("Usage: bengine_ui_benchmark --output file [--frames N --warmup N --scenario name]");
        if (!scenario.empty() && std::none_of(scenarios.begin(),scenarios.end(),[&](const auto& s){return scenario==s.name;}))
            throw std::runtime_error("Unknown scenario: "+scenario);
        std::ofstream out(output); if (!out) throw std::runtime_error("Cannot open output: "+output);
        out << std::setprecision(9);
        run(out,frames,warmup,scenario);
        out.close(); if (!out) throw std::runtime_error("Failed writing benchmark output");
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
