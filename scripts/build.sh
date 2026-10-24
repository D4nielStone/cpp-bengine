#!/usr/bin/env sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$repo_root/build"}
run_tests=OFF

if [ "$#" -gt 0 ]; then
    shift
fi

for option in "$@"; do
    case "$option" in
        --test)
            run_tests=ON
            ;;
        *)
            printf 'Unknown option: %s\nUsage: %s [build-dir] [--test]\n' \
                "$option" "$0" >&2
            exit 2
            ;;
    esac
done

case "$build_dir" in
    /*) ;;
    *) build_dir="$repo_root/$build_dir" ;;
esac

cmake -S "$repo_root" -B "$build_dir" \
    -DBUILD_TESTING="$run_tests" \
    -DBUILD_RUNTIME=ON \
    -DBUILD_EDITOR=ON
cmake --build "$build_dir" --config Release --parallel 2

if [ "$run_tests" = ON ]; then
    ctest --test-dir "$build_dir" --output-on-failure -C Release
fi
