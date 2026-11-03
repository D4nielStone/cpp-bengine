#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace commons {
struct material;
bool material_asset_dependencies(const std::filesystem::path&, std::vector<std::filesystem::path>&, std::string& error);
// Files are immutable and content-addressed so publishing a new scene/model
// reference never invalidates material files referenced by an older save.
std::string encode_material_asset(const material&, const std::filesystem::path& directory);
std::filesystem::path material_asset_path(const std::filesystem::path& directory, const std::string& contents);
bool store_material_asset(const std::filesystem::path& directory, const material&,
    std::filesystem::path& result, std::string& error);
bool load_material_asset(const std::filesystem::path&, material& result, std::string& error);
}
