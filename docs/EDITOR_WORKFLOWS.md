# Editor workflows

## Project Manager

Settings → Ativar programação por blocos controls the optional visual Lua workspace. It is off when no preference exists. Existing Lua sources and visual-script drafts remain intact. Changes apply when returning to the editor.

## Text and viewport input

Text fields support Home/End, Ctrl+Home/End, Page Up/Down and Shift selection. With Num Lock off, keypad navigation aliases apply while a text field owns keyboard focus. Click and drag selects text; Shift+click extends the selection.

Over a viewport, keypad 1/3/7 select front/right/top, Ctrl selects the opposite view, 4/6 and 2/8 orbit by 15 degrees, 9 reverses the view, 5 switches perspective/orthographic, and +/− zoom. Decimal frames the selection in Scene and the model in Modeling. Keyboard navigation is blocked while editing text or using a modal.

Modeling separates geometry, Rig e Weight Paint and model animations into dock windows. Reset UI restores default tabs and dock areas for the editor, modeling and scene-management workspaces. Existing saved layouts remain loadable. Scene management includes the same Hierarchy and Inspector implementations used by the main editor, including entity reparenting by drag and drop.

## Prefabs

Select a root entity and use File → Salvar seleção como prefab. The editor writes `assets/prefabs/<name>.bobject`, containing the root and its descendants, serialized components and material assets. Drag it from Files into Scene to instantiate independent entities with remapped IDs and an undoable creation.

The file uses the existing version-1 scene schema with a `prefab_root` field. Assets are referenced, not bundled: keep the referenced models, textures, scripts and animations available. Instances are independent snapshots; later edits to the prefab do not propagate to existing instances. Export refuses an existing filename instead of silently overwriting it.

## GLSL

Files → Novo shader GLSL creates an editable fragment shader using a material or post-processing template. Open a `.frag`, `.vert` or `.glsl` file in the text editor and save normally.

Materials expose Vertex GLSL and Fragment GLSL paths; press Enter to apply. An empty vertex path uses `pbr.vert`; an empty fragment path restores the ordinary material shader. Custom vertex shaders must follow the engine's mesh attribute and model/view/projection conventions. The material still supplies its uniforms and textures.

Camera Post-processing accepts a primary fragment and an ordered list of additional project-relative fragment paths. The renderer draws opaque geometry, computes ambient occlusion from depth, filters the occlusion mask across similar depths, and composites it before the additive particle layer. The camera fragment passes run after particles. An empty path disables that pass.

Each fragment receives `in vec2 uv`, `sampler2D sceneColor` and `vec2 viewportSize`. Every pass sees the preceding pass's color and the same opaque `sampler2D sceneDepth`. Check `bool hasSceneDepth` before using depth. `mat4 inverseViewProjection` reconstructs world positions from UV and device depth. `mat4 viewProjection` projects world positions. The directional light supplies `sunDirection` (the direction light travels), `sunColor`, `sunIntensity`, `directionalLightSpace`, `directionalShadowMap` and `hasDirectionalShadow`. The fragment writes its output to color attachment 0.

Under each renderer mesh, **Instâncias** shows the count and lets you select a record with Previous, Next or its index. You can add, duplicate or remove an instance and edit its position, rotation and scale. These edits use editor undo and redo. **Local Instances** makes the records relative to the renderer entity and its hierarchy. Existing world-space records keep their original interpretation by default. Changing the toggle reinterprets values in the chosen space. With an empty list the base mesh renders at the entity pose. **Additive Particles** draws the renderer after ambient occlusion, depth-tests and fades against opaque surfaces, and leaves scene depth intact. Use the particle vertex and fragment shaders for animated billboards. Set Shader Bounds Padding when a shader moves vertices beyond the mesh bounds.

Saved GLSL changes are checked at most twice per second per program. Compilation/link errors go to the existing diagnostics output and retain the previous valid program; an initially invalid material falls back to its regular shader, and an initially invalid post-processing pass is skipped. Material shader paths are persisted in `.bmat`; camera paths are persisted in scene components.

Ordinary panel labels, inputs and buttons use scale 0.30. Larger titles and icon sizes retain their roles. Global UI scaling remains available.
