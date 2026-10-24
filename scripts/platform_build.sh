#!/usr/bin/env sh
set -eu

target=${1:?target must be linux or windows}
repository_root=${2:?repository root is required}
build_root=${3:?build directory is required}
progress_file=${4:?progress file is required}

case "$target" in
    linux|windows) ;;
    *)
        printf 'Unsupported target platform: %s\n' "$target" >&2
        exit 2
        ;;
esac

write_progress() {
    temporary="${progress_file}.tmp"
    printf '%s|%s\n' "$1" "$2" > "$temporary"
    mv "$temporary" "$progress_file"
}

run_vcpkg_install() {
    vcpkg_log="${build_root}/editor-vcpkg-${target}.log"
    if [ "$target" = windows ]; then
        PATH="${pwsh_root}:${windres_root}:$PATH"
        export PATH
    fi
    if [ "$target" = windows ]; then
        packages="assimp bullet3 freetype glfw3 glm lua"
        (
            cd "$vcpkg_root"
            ./vcpkg install \
                $packages --triplet="$target_triplet" \
                --overlay-triplets="$triplet_root"
        ) > "$vcpkg_log" 2>&1 &
    else
        packages="assimp bullet3 freetype glfw3 glm lua libpng"
        (
            cd "$vcpkg_root"
            ./vcpkg install \
                $packages --triplet="$target_triplet" \
                --overlay-triplets="$triplet_root"
        ) > "$vcpkg_log" 2>&1 &
    fi

    install_pid=$!
    if [ "$target" = windows ]; then
        progress_base=50
        progress_span=25
    else
        progress_base=40
        progress_span=35
    fi
    while kill -0 "$install_pid" 2>/dev/null; do
        package_state=$(grep -E '^Installing [0-9]+/[0-9]+' "$vcpkg_log" |
            tail -n 1 || true)
        if [ "$package_state" ]; then
            package_numbers=${package_state#Installing }
            current_package=${package_numbers%%/*}
            total_package_text=${package_numbers#*/}
            total_packages=${total_package_text%% *}
            case "$current_package:$total_packages" in
                *[!0-9:]*|:*) ;;
                *)
                    percent=$((progress_base + current_package * progress_span / total_packages))
                    [ "$percent" -le 75 ] || percent=75
                    write_progress "$percent" \
                        "Preparing target dependencies (${package_state#Installing })"
                    ;;
            esac
        fi
        sleep 0.5
    done
    if ! wait "$install_pid"; then
        cat "$vcpkg_log" >&2
        printf 'vcpkg dependency preparation failed; details: %s\n' \
            "$vcpkg_log" >&2
        exit 5
    fi
    cat "$vcpkg_log"
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'Required command not found: %s\n' "$1" >&2
        exit 3
    fi
}

require_command cmake
require_command c++
require_command curl
require_command git
require_command grep
require_command pkg-config
require_command sha256sum
require_command tail
require_command tar

host_architecture=$(uname -m)
if [ "$host_architecture" != "x86_64" ]; then
    printf 'The current toolchains support x64 hosts only (found %s).\n' \
        "$host_architecture" >&2
    exit 3
fi

if [ "$target" = linux ]; then
    if ! pkg-config --exists gl glfw3 x11 xrandr xinerama xcursor xi; then
        printf '%s\n' \
            'Linux build dependencies are missing. Install them in this Linux environment:' \
            'sudo apt-get update && sudo apt-get install -y cmake build-essential pkg-config libgl1-mesa-dev libglfw3-dev xorg-dev libxinerama-dev libxcursor-dev libxrandr-dev libxi-dev libglu1-mesa-dev libassimp-dev libbullet-dev libfreetype6-dev liblua5.3-dev libpng-dev' >&2
        exit 3
    fi
fi

toolchain_root="${HOME}/.cache/cpp-bengine/toolchains"
zig_root="${toolchain_root}/zig-0.14.1"
pwsh_root="${toolchain_root}/powershell-7.6.8"
vcpkg_root=${VCPKG_ROOT:-"${toolchain_root}/vcpkg"}
triplet_root="${toolchain_root}/triplets"
target_build="${build_root}/platform-builds/${target}"
mkdir -p "$toolchain_root" "$triplet_root" "$target_build"

zig_binary="${zig_root}/zig"
if [ ! -x "$zig_binary" ]; then
    require_command curl
    archive="${toolchain_root}/zig-0.14.1-linux.tar.xz"
    archive_url="https://ziglang.org/download/0.14.1/zig-x86_64-linux-0.14.1.tar.xz"
    archive_sha256="24aeeec8af16c381934a6cd7d95c807a8cb2cf7df9fa40d359aa884195c4716c"
    archive_size=49086504
    write_progress 1 "Downloading Zig x64 toolchain..."
    curl --fail --location --silent --show-error --retry 3 \
        --output "$archive" "$archive_url" &
    download_pid=$!
    while kill -0 "$download_pid" 2>/dev/null; do
        if [ -f "$archive" ]; then
            downloaded=$(wc -c < "$archive")
        else
            downloaded=0
        fi
        percent=$((1 + downloaded * 17 / archive_size))
        [ "$percent" -le 18 ] || percent=18
        write_progress "$percent" "Downloading Zig x64 toolchain..."
        sleep 0.25
    done
    if ! wait "$download_pid"; then
        printf 'Zig download failed: %s\n' "$archive_url" >&2
        exit 4
    fi
    write_progress 19 "Verifying Zig download..."
    if ! printf '%s  %s\n' "$archive_sha256" "$archive" |
        sha256sum --check --status; then
        printf 'Zig archive checksum verification failed: %s\n' "$archive" >&2
        exit 4
    fi
    mkdir -p "$zig_root"
    tar -xJf "$archive" --strip-components=1 -C "$zig_root"
    rm -f "$archive"
    write_progress 24 "Zig toolchain is ready."
fi

if [ "$target" = windows ] && [ ! -x "${pwsh_root}/pwsh" ]; then
    pwsh_archive="${toolchain_root}/powershell-7.6.8-linux-x64.tar.gz"
    pwsh_url="https://github.com/PowerShell/PowerShell/releases/download/v7.6.8/powershell-7.6.8-linux-x64.tar.gz"
    pwsh_sha256="59112e38ad83350e4f6dc0195953c66b5926792f300ca2e7691b8242e7a16e7f"
    pwsh_size=75908143
    write_progress 25 "Downloading PowerShell host support for vcpkg..."
    curl --fail --location --silent --show-error --retry 3 \
        --output "$pwsh_archive" "$pwsh_url" &
    download_pid=$!
    while kill -0 "$download_pid" 2>/dev/null; do
        if [ -f "$pwsh_archive" ]; then
            downloaded=$(wc -c < "$pwsh_archive")
        else
            downloaded=0
        fi
        percent=$((25 + downloaded * 13 / pwsh_size))
        [ "$percent" -le 38 ] || percent=38
        write_progress "$percent" "Downloading PowerShell host support..."
        sleep 0.25
    done
    if ! wait "$download_pid"; then
        printf 'PowerShell host-support download failed: %s\n' "$pwsh_url" >&2
        exit 4
    fi
    if ! printf '%s  %s\n' "$pwsh_sha256" "$pwsh_archive" |
        sha256sum --check --status; then
        printf 'PowerShell archive checksum verification failed: %s\n' \
            "$pwsh_archive" >&2
        exit 4
    fi
    mkdir -p "$pwsh_root"
    tar -xzf "$pwsh_archive" -C "$pwsh_root"
    chmod +x "${pwsh_root}/pwsh"
    rm -f "$pwsh_archive"
    write_progress 40 "PowerShell host support is ready."
fi
if [ "$target" = windows ]; then
    ln -sf "${pwsh_root}/pwsh" "${pwsh_root}/powershell.exe"
fi

target_triplet="x64-zig-${target}"
target_architecture=x86_64-linux-gnu
if [ "$target" = windows ]; then
    target_architecture=x86_64-windows-gnu
fi

toolchain_file="${triplet_root}/zig-${target}-toolchain.cmake"
triplet_file="${triplet_root}/${target_triplet}.cmake"
c_compiler_wrapper="${toolchain_root}/zig-${target}-cc"
cxx_compiler_wrapper="${toolchain_root}/zig-${target}-cxx"
cat > "$c_compiler_wrapper" <<EOF
#!/bin/sh
exec "${zig_binary}" cc -target ${target_architecture} "\$@"
EOF
cat > "$cxx_compiler_wrapper" <<EOF
#!/bin/sh
exec "${zig_binary}" c++ -target ${target_architecture} "\$@"
EOF
chmod +x "$c_compiler_wrapper" "$cxx_compiler_wrapper"
if [ "$target" = windows ]; then
    cat > "$toolchain_file" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${c_compiler_wrapper}")
set(CMAKE_CXX_COMPILER "${cxx_compiler_wrapper}")
EOF
    cat > "$triplet_file" <<EOF
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${toolchain_file}")
EOF
else
    cat > "$toolchain_file" <<EOF
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "${c_compiler_wrapper}")
set(CMAKE_CXX_COMPILER "${cxx_compiler_wrapper}")
EOF
    cat > "$triplet_file" <<EOF
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${toolchain_file}")
EOF
fi

windres_root="${toolchain_root}/mingw-windres"
if [ "$target" = windows ]; then
    windres_cache="${toolchain_root}/mingw-binutils"
    windres_binary=$(command -v x86_64-w64-mingw32-windres || true)
    if [ ! "$windres_binary" ]; then
        windres_binary="${windres_cache}/extracted/usr/bin/x86_64-w64-mingw32ucrt-windres"
        if [ ! -x "$windres_binary" ]; then
            if ! command -v apt-get >/dev/null 2>&1 ||
                ! command -v dpkg-deb >/dev/null 2>&1; then
                printf '%s\n' \
                    'Windows cross-builds require x86_64-w64-mingw32-windres.' \
                    'Install the MinGW-w64 binutils package for this Linux distribution and rerun.' >&2
                exit 3
            fi
            write_progress 47 "Downloading MinGW Windows resource compiler..."
            windres_downloads="${windres_cache}/downloads"
            mkdir -p "$windres_downloads" "${windres_cache}/extracted"
            (
                cd "$windres_downloads"
                apt-get download binutils-mingw-w64-x86-64
            )
            package_found=false
            for package_file in \
                "$windres_downloads"/binutils-mingw-w64-x86-64_*.deb; do
                if [ -f "$package_file" ]; then
                    dpkg-deb -x "$package_file" "${windres_cache}/extracted"
                    rm -f "$package_file"
                    package_found=true
                    break
                fi
            done
            if [ "$package_found" != true ] || [ ! -x "$windres_binary" ]; then
                printf 'MinGW resource compiler was not extracted from apt package.\n' >&2
                exit 4
            fi
            write_progress 49 "MinGW Windows resource compiler is ready."
        fi
    fi
    mkdir -p "$windres_root"
    windres_wrapper="${windres_root}/windres.tmp"
    cat > "$windres_wrapper" <<EOF
#!/bin/sh
exec "${windres_binary}" \
    --preprocessor="${c_compiler_wrapper}" \
    --preprocessor-arg=-E \
    --preprocessor-arg=-xc-header \
    --preprocessor-arg=-DRC_INVOKED \
    -I "${zig_root}/lib/libc/include/any-windows-any" "\$@"
EOF
    chmod +x "$windres_wrapper"
    mv -f "$windres_wrapper" "${windres_root}/windres"
fi

if [ ! -x "${vcpkg_root}/vcpkg" ]; then
    if [ "${VCPKG_ROOT:-}" ]; then
        printf 'VCPKG_ROOT does not contain the vcpkg executable: %s\n' \
            "$vcpkg_root" >&2
        exit 3
    fi
    if [ "$target" = windows ]; then
        write_progress 41 "Downloading vcpkg dependency manager..."
    else
        write_progress 25 "Downloading vcpkg dependency manager..."
    fi
    if [ ! -d "$vcpkg_root/.git" ]; then
        git clone --depth 1 --branch 2024.11.16 \
            https://github.com/microsoft/vcpkg.git "$vcpkg_root"
    fi
    if [ "$target" = windows ]; then
        write_progress 46 "Preparing vcpkg..."
    else
        write_progress 33 "Preparing vcpkg..."
    fi
    (
        cd "$vcpkg_root"
        ./bootstrap-vcpkg.sh -disableMetrics
    )
fi

if [ "$target" = windows ]; then
    write_progress 49 "Preparing ${target} dependencies with vcpkg..."
else
    write_progress 36 "Preparing ${target} dependencies with vcpkg..."
fi
(
    run_vcpkg_install
)

if [ "$target" = windows ]; then
    PATH="${pwsh_root}:${windres_root}:$PATH"
    export PATH
fi

toolchain_file="${vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
host_triplet=x64-linux
write_progress 80 "Configuring ${target} runtime..."
if [ "$target" = windows ]; then
    cmake -S "$repository_root" -B "$target_build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${triplet_root}/zig-${target}-toolchain.cmake" \
        -DVCPKG_TARGET_TRIPLET="$target_triplet" \
        -DVCPKG_HOST_TRIPLET="$host_triplet" \
        -DVCPKG_OVERLAY_TRIPLETS="$triplet_root" \
        -DCOMMONS_USE_RELATIVE_ASSET_PATHS=ON \
        -DBUILD_TESTING=OFF \
        -DBUILD_RUNTIME=ON \
        -DBUILD_EDITOR=OFF \
        -DCMAKE_BUILD_TYPE=Release
else
    cmake -S "$repository_root" -B "$target_build" \
        -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${triplet_root}/zig-${target}-toolchain.cmake" \
        -DVCPKG_TARGET_TRIPLET="$target_triplet" \
        -DVCPKG_HOST_TRIPLET="$host_triplet" \
        -DVCPKG_OVERLAY_TRIPLETS="$triplet_root" \
        -DCOMMONS_USE_RELATIVE_ASSET_PATHS=ON \
        -DBUILD_TESTING=OFF \
        -DBUILD_RUNTIME=ON \
        -DBUILD_EDITOR=OFF \
        -DCMAKE_BUILD_TYPE=Release
fi

write_progress 90 "Compiling ${target} runtime..."
cmake --build "$target_build" --target runtime_app --config Release --parallel 2
if [ "$target" = windows ]; then
    output_binary="${target_build}/apps/runtime_app.exe"
else
    output_binary="${target_build}/apps/runtime_app"
fi
mkdir -p "${target_build}/apps/assets"
cp -R "${repository_root}/commons/assets/." "${target_build}/apps/assets/"
write_progress 100 "Build completed: ${output_binary}"
