#pragma once

#include "core/ecs.hpp"
#include "loaders/model.hpp"
#include <chrono>
#include <stdexcept>
#include <functional>
#include <future>
#include <unordered_map>

namespace COMMONS_NS {
// Owned by the render thread. Workers own only CPU data; pump must process UI
// without updating gameplay or the staging registry. Cache lifetime is one load.
class scene_asset_loader final {
public:
    enum class phase { idle, importing, decoding, uploading, ready, failed, cancelled };
    struct progress {
        phase state{phase::idle};
        std::string error;
        float fraction{0};
        std::size_t imported{0}, total{0};
        std::string current;
    };
    using pump = std::function<bool(const progress&)>;
    void prepare(const std::vector<std::string>& paths, const pump& update);
    std::unique_ptr<model> instantiate(const std::string& path) const;
    material load_material(const std::filesystem::path& path, const pump& update);
    void finish(const std::shared_ptr<ecs>& staging, const pump& update);
    const progress& status() const noexcept { return m_status; }
private:
    void notify(const pump& update);
    template<class T> T wait(std::future<T> result, const pump& update) {
        bool cancelled = false;
        while (result.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready) {
            if (!update(m_status)) cancelled = true;
        }
        auto value = result.get();
        if (cancelled) {
            m_status.state = phase::cancelled;
            throw std::runtime_error("Scene loading cancelled.");
        }
        notify(update);
        return value;
    }
    std::unordered_map<std::string, material> m_materials;
    progress m_status;
    std::unordered_map<std::string, std::unique_ptr<model>> m_models;
};
}
