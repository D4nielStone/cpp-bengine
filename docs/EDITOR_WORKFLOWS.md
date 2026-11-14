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

Camera Post-processing accepts a fragment path relative to the project. It runs after ambient occlusion with `in vec2 uv`, `uniform sampler2D sceneColor`, and `uniform vec2 viewportSize`. It writes the final color to location 0. An empty path disables the pass.

Saved GLSL changes are checked at most twice per second per program. Compilation/link errors go to the existing diagnostics output and retain the previous valid program; an initially invalid material falls back to its regular shader, and an initially invalid post-processing pass is skipped. Material shader paths are persisted in `.bmat`; camera paths are persisted in scene components.

Ordinary panel labels, inputs and buttons use scale 0.30. Larger titles and icon sizes retain their roles. Global UI scaling remains available.
