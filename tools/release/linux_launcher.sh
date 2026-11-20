#!/bin/sh
root=$(CDPATH= cd -- "$(dirname -- "$0")/../lib/bubble-engine" && pwd) || exit 1
export BENGINE_NATIVE_STATE="${XDG_DATA_HOME:-"$HOME/.local/share"}/bubble-engine"
exec "$root/engine_updater" "$@"
