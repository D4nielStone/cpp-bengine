#pragma once

#include <bgui.hpp>
#include <lay/dock.hpp>
#include <elem/field.hpp>
#include <elem/inputbox.hpp>
#include <elem/color_picker.hpp>
#include <elem/checkbox.hpp>
#include <charconv>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace editor::ui {

class editor_panel {
public:
    virtual ~editor_panel() = default;

    virtual void build(bgui::dock& parent) = 0;
    virtual void update() = 0;

    template<class Parent, class... Args>
    static bgui::field& add_field(Parent& parent, Args&&... args) {
        return parent.template add<bgui::field>(std::forward<Args>(args)...);
    }

    template<class Parent, class... Args>
    static bgui::inputbox& add_input(Parent& parent, Args&&... args) {
        return parent.template add<bgui::inputbox>(std::forward<Args>(args)...);
    }

    template<class Parent, class... Args>
    static bgui::checkbox& add_checkbox(Parent& parent, Args&&... args) {
        return parent.template add<bgui::checkbox>(std::forward<Args>(args)...);
    }

    template<class Parent>
    static bgui::color_picker& add_color(Parent& parent, const bgui::color& value) {
        auto& picker = parent.template add<bgui::color_picker>(value);
        picker.add_class("editor-input");
        picker.style.layout.require_size(128.f, 92.f);
        return picker;
    }

    // Bound storage must outlive the widget and any history actions.
    template<class Parent>
    static bgui::color_picker& add_color(Parent& parent, bgui::color* value) {
        if (!value) throw std::invalid_argument("panel color binding is null");
        auto& picker = add_color(parent, *value);
        picker.set_on_change([value](const bgui::color& next) { *value = next; });
        return picker;
    }

    void add_float(bgui::linear& parent, const std::string& label, float* value) {
        if (!value) throw std::invalid_argument("panel float binding is null");
        add_float(parent, label, *value, [value]() { return *value; },
            [value](float next) { *value = next; });
    }

    void add_vec_field(bgui::linear& parent, const std::string& label, bgui::vec3* value) {
        if (!value) throw std::invalid_argument("panel vector binding is null");
        add_vec_field(parent, label, *value, [value]() { return *value; },
            [value](std::size_t axis, float next) { (*value)[axis] = next; });
    }

    static std::string format_value(const float value) {
        auto result = std::to_string(value);
        result.erase(result.find_last_not_of('0') + 1);
        if (!result.empty() && result.back() == '.')
            result.pop_back();
        return result;
    }

    static void configure_row(bgui::linear& row) {
        row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        row.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::center
        };
    }

    static void configure_property_label(bgui::text& label) {
        label.add_class("editor-property-label");
        label.style.layout.require_mode(bgui::mode::pixel, bgui::mode::wrap_content);
        label.style.layout.require_width(bgui::mode::pixel, 112.f);
        label.style.layout.margin = bgui::vec4i{0, 0, 4, 0};
    }

    static void configure_property_row(bgui::linear& row) {
        configure_row(row);
        row.style.layout.padding = bgui::vec4i{0, 0, 0, 0};
        row.style.layout.margin = bgui::vec4i{0, 2, 0, 2};
    }

    static void configure_property_input(bgui::inputbox& input) {
        input.add_class("editor-input");
        input.style.layout.require_mode(bgui::mode::stretch, bgui::mode::pixel);
        input.style.layout.require_height(bgui::mode::pixel, 22.f);
        input.style.layout.margin = bgui::vec4i{0, 0, 0, 0};
        input.style.layout.padding = bgui::vec4i{4, 2, 4, 2};
    }

    static void configure_property_field(bgui::field& field) {
        configure_property_row(field);
        configure_property_label(field.get_label());
        configure_property_input(field.get_inputbox());
    }

    void add_vec_field(
        bgui::linear& parent,
        const std::string& label,
        const bgui::vec3& value,
        const std::function<bgui::vec3()>& get_value,
        const std::function<void(std::size_t, float)>& on_change
    ) {
        auto& row = parent.add<bgui::linear>(bgui::orientation::horizontal);
        configure_property_row(row);
        row.style.layout.require_height(bgui::mode::pixel, 22.f);
        row.style.layout.margin = bgui::vec4i{0, 1, 0, 1};
        auto& name = row.add<bgui::text>(label, 0.27f);
        configure_property_label(name);
        auto& values = row.add<bgui::linear>(bgui::orientation::horizontal);
        values.add_class("editor-property-row");
        configure_row(values);
        values.style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);
        values.style.layout.padding = bgui::vec4i{0, 0, 0, 0};
        values.style.layout.margin = bgui::vec4i{0, 0, 0, 0};

        constexpr const char* axes[]{"X", "Y", "Z"};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto& axis_label = values.add<bgui::text>(axes[axis], 0.27f);
            axis_label.add_class("editor-property-label");
            axis_label.style.layout.require_width(bgui::mode::pixel, 12.f);

            auto& input = values.add<bgui::inputbox>(
                format_value(value[axis]),
                "",
                0.27f,
                [this, on_change, get_value, axis, label](
                    const std::string& buffer
                ) {
                    float parsed{};
                    const auto result = std::from_chars(
                        buffer.data(),
                        buffer.data() + buffer.size(),
                        parsed,
                        std::chars_format::general
                    );
                    if (result.ec != std::errc{} ||
                        result.ptr != buffer.data() + buffer.size() ||
                        !std::isfinite(parsed)) {
                        std::cerr << "[editor] Invalid number for "
                                  << label << ": " << buffer << '\n';
                        return;
                    }
                    const float previous = get_value()[axis];
                    if (previous == parsed)
                        return;
                    auto apply_value = [on_change, axis](
                        const float next
                    ) {
                        on_change(axis, next);
                    };
                    apply_value(parsed);
                    record_edit(
                        [apply_value, previous]() { apply_value(previous); },
                        [apply_value, parsed]() { apply_value(parsed); }
                    );
                },
                bgui::input_mode::inputbox
            );
            configure_property_input(input);
            track_property_input(input, [get_value, axis]() {
                return format_value(get_value()[axis]);
            });
        }
    }

    void add_float(
        bgui::linear& parent,
        const std::string& label,
        const float value,
        const std::function<float()>& get_value,
        const std::function<void(float)>& on_change,
        bool record_history = true
    ) {
        auto& field = parent.add<bgui::field>(
            label,
            format_value(value),
            0.27f,
            [this, on_change, get_value, label, record_history](const std::string& buffer) {
                float parsed{};
                const auto result = std::from_chars(
                    buffer.data(),
                    buffer.data() + buffer.size(),
                    parsed,
                    std::chars_format::general
                );
                if (result.ec != std::errc{} ||
                    result.ptr != buffer.data() + buffer.size() ||
                    !std::isfinite(parsed)) {
                    std::cerr << "[editor] Invalid number for "
                              << label << ": " << buffer << '\n';
                    return;
                }
                const float previous = get_value();
                if (previous == parsed)
                    return;
                auto apply_value = [on_change](float next) {
                    on_change(next);
                };
                apply_value(parsed);
                if (!record_history) return;
                record_edit(
                    [apply_value, previous]() { apply_value(previous); },
                    [apply_value, parsed]() { apply_value(parsed); }
                );
            }
        );
        configure_property_field(field);
        track_property_input(field.get_inputbox(), [get_value]() {
            return format_value(get_value());
        });
    }


protected:
    virtual void record_edit(std::function<void()>, std::function<void()>) {}
    virtual void track_property_input(
        bgui::inputbox&,
        std::function<std::string()>
    ) {}
};

using panel = editor_panel;

}
