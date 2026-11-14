#pragma once
#include <filesystem>
namespace editor::ui {
// Internal asset drags share the same non-interactive hint as native file drags.
inline bool asset_drag_active{false};
inline std::filesystem::path asset_drag_path;
}
