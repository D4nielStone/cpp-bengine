#!/usr/bin/env sh
set -eu

target=${1:?target must be linux or windows}
repository_root=${2:?repository root is required}
output_root=${3:?game output directory is required}
progress_file=${4:?progress file is required}
project_file=${5:?saved game project is required}
case "$target" in linux|windows) ;; *) printf 'Unsupported target: %s\n' "$target" >&2; exit 2 ;; esac
workspace=${6:-"${XDG_CACHE_HOME:-"${HOME}/.cache"}/cpp-bengine/game-builds/$(printf '%s\n' "$repository_root" "$project_file" "$output_root" "$target" | cksum | cut -d' ' -f1)"}
format=${7:-folder}
provided_template=${8:-}
packager=${9:-${BENGINE_PACKAGER:-}}
case "$format" in folder|single) ;; *) printf 'Unsupported game format: %s\n' "$format" >&2; exit 2 ;; esac
staging="${workspace}/package"
output="${output_root}/${target}"
mkdir -p "$workspace" "$(dirname "$progress_file")"
printf '1|Preparing game export...\n' > "$progress_file"
if [ ! "$packager" ]; then
    cmake -S "${repository_root}/tools/game_packager" -B "${workspace}/packager" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${workspace}/packager" --config Release --parallel 2
    packager="${workspace}/packager/game_packager"
fi
"$packager" prepare "$repository_root" "$project_file" "$staging" "$output"
key=$("$packager" template-key "$repository_root" "$target" "$format")
cache=${BENGINE_TEMPLATE_CACHE:-"${XDG_CACHE_HOME:-"${HOME}/.cache"}/cpp-bengine/export-templates"}
template="${cache}/${target}/${format}/${key}"
if [ "$provided_template" ]; then
    template=$provided_template
    "$packager" template-validate "$template" "$key" "$target" "$format"
elif ! "$packager" template-validate "$template" "$key" "$target" "$format" >/dev/null 2>&1; then
    shipped="${BENGINE_EXPORT_TEMPLATES:-"${repository_root}/export_templates"}/${target}/${format}"
    if [ -f "${shipped}/${key}/template.json" ]; then shipped="${shipped}/${key}"; fi
    if "$packager" template-validate "$shipped" "$key" "$target" "$format" >/dev/null 2>&1; then
        template=$shipped
    else
        printf 'No compatible precompiled template; building it once.\n'
        runtime_root="${workspace}/runtime-${format}-${key}"
        sh "${repository_root}/scripts/compile_runtime.sh" "$target" "$repository_root" "$runtime_root" "$progress_file" packaging "$format"
        binary="${runtime_root}/platform-builds/${target}/apps/runtime_app"
        if [ "$target" = windows ]; then binary="${binary}.exe"; fi
        if [ "$target" = linux ] && [ "$format" = single ]; then
            cmake -DBINARY="$binary" -DDESTINATION="${workspace}/unused" -DCHECK_SINGLE=ON -P "${repository_root}/scripts/bundle_game_libraries.cmake"
        fi
        if [ "$target" = linux ] && [ "$format" = folder ]; then
            cmake -DBINARY="$binary" -DDESTINATION="${template}/lib" -P "${repository_root}/scripts/bundle_game_libraries.cmake"
        fi
        current_key=$("$packager" template-key "$repository_root" "$target" "$format")
        if [ "$key" != "$current_key" ]; then printf 'Engine sources changed during template build; retry export.\n' >&2; exit 6; fi
        "$packager" template-create "$binary" "$template" "$key" "$target" "$format"
    fi
fi
printf 'Using precompiled runtime template: %s\n' "$template"
binary="${template}/runtime_app"
if [ "$target" = windows ]; then binary="${binary}.exe"; fi
printf '96|Packaging game and dependencies...\n' > "$progress_file"
"$packager" stage "$binary" "$staging" "$target"
if [ -d "${template}/lib" ]; then cp -R "${template}/lib" "$staging/lib"; fi
"$packager" publish "$staging" "$output" "$format"
printf '100|Game exported: %s\n' "$output" > "$progress_file"
