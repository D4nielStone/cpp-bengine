#pragma once
#include "loaders/model_pose.hpp"
#include <elem/image.hpp>
#include <os/os.hpp>
#include <utils/texture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace editor::ui {
inline bgui::texture model_import_preview(const aiScene& scene,
    const commons::model_import_options& options, float yaw = .65f,
    float pitch = .4f, float zoom = 1.f) {
    constexpr int width = 480, height = 260;
    bgui::texture result;
    result.m_path = "model-import-preview";
    result.m_size = {width, height};
    result.m_has_alpha = true;
    result.m_generate_mipmap = false;
    result.m_buffer.resize(width * height * 4);
    for (int i = 0; i < width * height; ++i) {
        for (int c = 0; c < 3; ++c) result.m_buffer[i * 4 + c] = 32;
        result.m_buffer[i * 4 + 3] = 255;
    }
    commons::model_pose pose;
    pose.scene = &scene; pose.options = options;
    if (!scene.mRootNode) return result;
    pose.collect(scene.mRootNode); pose.evaluate(0);
    struct triangle { std::array<aiVector3D, 3> points; };
    std::vector<triangle> triangles;
    const float limit = std::numeric_limits<float>::max();
    aiVector3D model_low(limit, limit, limit), model_high(-limit, -limit, -limit);
    for (const auto& instance : pose.instances) {
        auto geometry = pose.vertices(instance);
        const auto* mesh = scene.mMeshes[instance.mesh];
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const auto& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            triangle t;
            bool valid = true;
            for (unsigned j = 0; j < 3; ++j) {
                if (face.mIndices[j] >= geometry.positions.size()) { valid = false; break; }
                t.points[j] = geometry.positions[face.mIndices[j]];
                const auto p = t.points[j];
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) { valid = false; break; }
            }
            if (!valid) continue;
            for (auto p : t.points) {
                model_low.x = std::min(model_low.x, p.x);
                model_low.y = std::min(model_low.y, p.y);
                model_low.z = std::min(model_low.z, p.z);
                model_high.x = std::max(model_high.x, p.x);
                model_high.y = std::max(model_high.y, p.y);
                model_high.z = std::max(model_high.z, p.z);
            }
            triangles.push_back(t);
        }
    }
    if (triangles.empty()) return result;
    const auto center = (model_low + model_high) * .5f;
    const float cos_yaw = std::cos(yaw), sin_yaw = std::sin(yaw);
    const float cos_pitch = std::cos(pitch), sin_pitch = std::sin(pitch);
    aiVector3D low(limit, limit, limit), high(-limit, -limit, -limit);
    for (auto& triangle : triangles) for (auto& point : triangle.points) {
        const auto relative = point - center;
        const float x = cos_yaw * relative.x + sin_yaw * relative.z;
        const float z = -sin_yaw * relative.x + cos_yaw * relative.z;
        point = {x, cos_pitch * relative.y - sin_pitch * z,
            sin_pitch * relative.y + cos_pitch * z};
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y);
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y);
    }
    const float scale = std::min((width-24) / std::max(high.x-low.x, .000001f),
        (height-24) / std::max(high.y-low.y, .000001f)) *
        std::clamp(zoom, .25f, 4.f);
    std::vector<float> depth(width * height, std::numeric_limits<float>::max());
    const auto edge = [](aiVector3D a, aiVector3D b, float x, float y) {
        return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
    };
    for (auto t : triangles) {
        auto normal = (t.points[1]-t.points[0]) ^ (t.points[2]-t.points[0]);
        const float length = normal.Length();
        const unsigned char shade = static_cast<unsigned char>(85 + 150 * (length > 0 ? std::abs(normal.z)/length : 0));
        for (auto& p : t.points) { p.x = p.x*scale+width*.5f; p.y = height*.5f-p.y*scale; }
        const auto a=t.points[0], b=t.points[1], c=t.points[2];
        const float area = edge(a,b,c.x,c.y);
        if (std::abs(area) < .000001f) continue;
        const int x0=std::max(0,int(std::floor(std::min({a.x,b.x,c.x})))), x1=std::min(width-1,int(std::ceil(std::max({a.x,b.x,c.x}))));
        const int y0=std::max(0,int(std::floor(std::min({a.y,b.y,c.y})))), y1=std::min(height-1,int(std::ceil(std::max({a.y,b.y,c.y}))));
        for (int y=y0; y<=y1; ++y) for (int x=x0; x<=x1; ++x) {
            const float u=edge(b,c,x+.5f,y+.5f)/area, v=edge(c,a,x+.5f,y+.5f)/area, w=1-u-v;
            if (u<0 || v<0 || w<0) continue;
            const float z=u*a.z+v*b.z+w*c.z;
            const int pixel=y*width+x;
            if (z>=depth[pixel]) continue;
            depth[pixel]=z;
            for (int channel=0; channel<3; ++channel) result.m_buffer[pixel*4+channel]=shade;
        }
    }
    return result;
}

class model_import_orbit final : public bgui::element {
public:
    model_import_orbit(std::shared_ptr<Assimp::Importer> importer, const aiScene& scene,
        const commons::model_import_options& options, bgui::image& image)
        : m_importer(std::move(importer)), m_scene(&scene), m_options(&options), m_image(&image) {
        type = "model-import-orbit";
        set_flex(false);
    }

    void set_drag(const bgui::vec2i& delta) override {
        if (delta.x == 0 && delta.y == 0)
            return;
        m_yaw += static_cast<float>(delta.x) * .012f;
        m_pitch = std::clamp(m_pitch + static_cast<float>(delta.y) * .012f,
            -1.45f, 1.45f);
        refresh();
    }

    void on_mouse_hover() override {
        element::on_mouse_hover();
        auto& context = bgui::get_context();
        if (context.m_scroll_delta_y == 0.f)
            return;
        m_zoom = std::clamp(m_zoom * std::exp(context.m_scroll_delta_y * .12f),
            .25f, 4.f);
        context.m_scroll_delta_y = 0.f;
        refresh();
    }

    void get_requires(bgui::draw_data*) override {}

    void on_update() override {
        if (m_image) {
            const auto rect = m_image->processed_rect();
            set_final_rect(rect.x, rect.y, rect.z, rect.w);
        }
    }

    void refresh() {
        m_image->set_texture(model_import_preview(*m_scene, *m_options,
            m_yaw, m_pitch, m_zoom));
    }

    void reset() {
        m_yaw = .65f;
        m_pitch = .4f;
        m_zoom = 1.f;
        refresh();
    }

private:
    std::shared_ptr<Assimp::Importer> m_importer;
    const aiScene* m_scene;
    const commons::model_import_options* m_options;
    bgui::image* m_image;
    float m_yaw{.65f};
    float m_pitch{.4f};
    float m_zoom{1.f};
};
}
