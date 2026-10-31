#!/usr/bin/env sh
set -eu

target=${1:?target must be linux or windows}
repository_root=${2:?repository root is required}
output_root=${3:?game output directory is required}
progress_file=${4:?progress file is required}
project_file=${5:?saved game project is required}
case "$target" in linux|windows) ;; *) printf 'Unsupported target: %s\n' "$target" >&2; exit 2 ;; esac
workspace=${6:-"${XDG_CACHE_HOME:-"${HOME}/.cache"}/cpp-bengine/game-builds/$(printf '%s\n' "$repository_root" "$project_file" "$output_root" "$target" | cksum | cut -d' ' -f1)"}
staging="${workspace}/package"
output="${output_root}/${target}"
mkdir -p "$workspace" "$(dirname "$progress_file")"
printf '1|Preparing game export...\n' > "$progress_file"
# The packager is a host tool: never compile it with the target cross compiler.
cmake -S "${repository_root}/tools/game_packager" -B "${workspace}/packager" -DCMAKE_BUILD_TYPE=Release
cmake --build "${workspace}/packager" --config Release --parallel 2
packager="${workspace}/packager/game_packager"
"$packager" prepare "$repository_root" "$project_file" "$staging" "$output"
sh "${repository_root}/scripts/compile_runtime.sh" "$target" "$repository_root" "${workspace}/runtime" "$progress_file" packaging
binary="${workspace}/runtime/platform-builds/${target}/apps/runtime_app"
if [ "$target" = windows ]; then binary="${binary}.exe"; fi
printf '96|Packaging game and dependencies...\n' > "$progress_file"
"$packager" stage "$binary" "$staging" "$target"
if [ "$target" = linux ]; then
    cmake -DBINARY="$binary" -DDESTINATION="${staging}/lib" -P "${repository_root}/scripts/bundle_game_libraries.cmake"
fi
"$packager" publish "$staging" "$output"
printf '100|Game exported: %s\n' "$output" > "$progress_file"
