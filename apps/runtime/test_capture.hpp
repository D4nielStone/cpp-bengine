#pragma once

#include "editor/systems/project_preview.hpp"
#include "project/project_file_transaction.hpp"
#include "components/camera.hpp"
#include "os/window.hpp"
#include "systems/system.hpp"
#include <cstdio>
#include <iostream>

namespace Bubble {
struct runtime_test_spec {
    unsigned width{}, height{}, frames{}, timeout{};
    rapidjson::Document document;
    std::filesystem::path path;

    void load(const std::filesystem::path& source) {
        path = source;
        const auto bytes = editor::ui::project_files::read(path);
        document.Parse(bytes.data(), bytes.size());
        if (document.HasParseError() || !document.IsObject())
            throw std::runtime_error("Invalid runtime test specification.");
        auto number = [&](const char* key, unsigned low, unsigned high) {
            if (!document.HasMember(key) || !document[key].IsUint() ||
                document[key].GetUint() < low || document[key].GetUint() > high)
                throw std::runtime_error("Invalid runtime test field: " + std::string(key));
            return document[key].GetUint();
        };
        width = number("width", 64, 2048);
        height = number("height", 64, 2048);
        frames = number("frames", 1, 3600);
        timeout = number("timeoutSeconds", 1, 120);
        if (!document.HasMember("specification") || !document["specification"].IsString())
            throw std::runtime_error("Missing expected visual specification.");
    }
};

class runtime_test_capture final : public commons::system {
    commons::window& m_window;
    std::shared_ptr<commons::camera>& m_camera;
    runtime_test_spec& m_spec;
    unsigned m_frame{};
    bool& m_failed;
public:
    runtime_test_capture(commons::window& window, std::shared_ptr<commons::camera>& camera,
        runtime_test_spec& spec, bool& failed)
        : m_window(window), m_camera(camera), m_spec(spec), m_failed(failed) {}
    void setup(const std::shared_ptr<commons::ecs>&) override {}
    void update(const std::shared_ptr<commons::ecs>&) override {
        if (++m_frame != m_spec.frames) return;
        std::string error;
        try {
            if (!m_camera || m_camera->viewportFBO.x != static_cast<int>(m_spec.width) ||
                m_camera->viewportFBO.y != static_cast<int>(m_spec.height))
                throw std::runtime_error("Capture resolution does not match the specification.");
            if (!editor::ui::save_project_preview(m_spec.path.string(), m_camera, error))
                throw std::runtime_error(error);
            auto& a = m_spec.document.GetAllocator();
            m_spec.document.AddMember("capturedFrame", m_frame, a);
            m_spec.document.AddMember("visualVerdict", "requires_agent_review", a);
            const auto report = std::filesystem::path(m_spec.path.string() + ".report.json");
            if (!editor::ui::project_files::save(report,
                {{report, editor::ui::project_files::encode(m_spec.document)}}, error))
                throw std::runtime_error(error);
        } catch (const std::exception& failure) {
            m_failed = true;
            std::cerr << "[runtime test] " << failure.what() << '\n';
        }
        m_window.close();
    }
};
}
