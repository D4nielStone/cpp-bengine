# ECS lifetime

The registry owns its components. `component::reg` is a `std::weak_ptr<ecs>`;
components and Lua handles do not keep a scene alive. C++ callers that previously
used `component->reg->...` must now hold a temporary owner for the operation:

```cpp
if (const auto registry = component->reg.lock()) {
    auto transform = registry->get<commons::transform>(component->my_object);
}
```

Assignment from `shared_ptr<ecs>` and `reg.reset()` remain supported. Scene files
and Lua APIs are unchanged. Stack-allocated registries still leave `reg` empty.

Window destruction makes its OpenGL context current, releases systems, calls
`release_graphics_resources()` on attached components, and clears the registry
before shutting down BGUI and GLFW. Camera and renderer implement idempotent
release so externally retained components can later be destroyed safely.
Custom components owning GPU objects must implement the same hook. Components
removed from the window's registry and standalone scenes remain their owner's
responsibility: release their GPU resources while their context is current.
