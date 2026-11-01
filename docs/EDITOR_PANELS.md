# Editor panels

Extend `editor::ui::panel` from `apps/editor/core/panel.hpp` and implement
`build(bgui::dock&)` and `update()`. Widgets remain owned by their BGUI parent;
helpers return references for additional styling and callbacks.

- `add_float(parent, label, float*)`: compact labeled numeric input.
- `add_vec_field(parent, label, bgui::vec3*)`: compact X/Y/Z inputs.
- `add_color(parent, bgui::color*)`: color picker that writes to the supplied color.
- `add_color(parent, initial_color)`: picker with a caller-defined change callback.
- `add_field`, `add_input`, `add_checkbox`: factories accepting the existing
  BGUI constructor arguments, so specialized layouts can retain their styling.

Numeric helpers also accept an initial value, getter and setter instead of a
pointer. Prefer these overloads for ECS components: capture the component's
existing shared ownership and use its setters. Vector setters take an axis and
a float. Numeric inputs reject nonfinite, overflowing and partially parsed values.

Override `record_edit(undo, redo)` to integrate numeric edits with an existing
history system. The setter runs before the history hook. The default hook does
nothing. Custom color/checkbox callbacks retain responsibility for history.

Pointer overloads do not own storage: it must outlive the widgets and any undo
actions. For transient entities, use getter/setter callbacks with appropriate
ownership and lifecycle checks. Null bindings throw `std::invalid_argument`.
The numeric getter is read for each edit so undo records the current model value.
