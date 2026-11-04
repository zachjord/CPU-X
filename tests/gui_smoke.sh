#!/usr/bin/env bash
# GTK4 startup smoke test: start the GUI on every tab under a virtual X server
# and fail on early exit, GTK criticals or crashes. Needs Xvfb; no root.
# Usage: gui_smoke.sh <cpu-x binary> <source dir>

set -uo pipefail

BIN="$1"
SRC_DIR="$2"
WORKDIR="$(mktemp -d)"
trap 'kill "${XVFB_PID:-}" 2>/dev/null; rm -rf "$WORKDIR"' EXIT

if ! command -v Xvfb > /dev/null; then
	echo "Xvfb not found: skipping"
	exit 77
fi

DISPLAY_NUM=$((90 + RANDOM % 100))
Xvfb ":$DISPLAY_NUM" -screen 0 1280x1024x24 > /dev/null 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY=":$DISPLAY_NUM"
unset WAYLAND_DISPLAY
export GTK_A11Y=none NO_AT_BRIDGE=1 XDG_CONFIG_HOME="$WORKDIR/config"
export GSETTINGS_BACKEND=memory
# No GPU in CI containers: render with Cairo instead of probing Vulkan/GL
export GSK_RENDERER=cairo

failures=0
run_case() {
	local name="$1" tab="$2"
	local log="$WORKDIR/$name-$tab.log"
	"$BIN" --gtk --tab "$tab" > "$log" 2>&1 &
	local pid=$!
	sleep 3
	if ! kill -0 "$pid" 2> /dev/null; then
		wait "$pid"
		echo "FAIL [$name] tab $tab: exited early with status $?"
		cat "$log"
		failures=$((failures + 1))
		return
	fi
	kill "$pid"; wait "$pid" 2> /dev/null
	if grep -E "CRITICAL|Gtk-ERROR|Gtk-WARNING|X Window System error|Segmentation|Aborted|failed to start GTK GUI" "$log" \
		| grep -v "accessibility bus"; then
		echo "FAIL [$name] tab $tab: GTK diagnostics in output"
		failures=$((failures + 1))
		return
	fi
	echo "ok   [$name] tab $tab"
}

# Without an installed schema the GUI must fall back to defaults
export GSETTINGS_SCHEMA_DIR="$WORKDIR/no-schema"
mkdir -p "$GSETTINGS_SCHEMA_DIR"
run_case no-schema 0

# With the project schema, on every tab
if command -v glib-compile-schemas > /dev/null; then
	export GSETTINGS_SCHEMA_DIR="$WORKDIR/schemas"
	mkdir -p "$GSETTINGS_SCHEMA_DIR"
	cp "$SRC_DIR"/data/*.gschema.xml "$GSETTINGS_SCHEMA_DIR"/
	glib-compile-schemas "$GSETTINGS_SCHEMA_DIR"
	for tab in 0 1 2 3 4 5 6 7; do
		run_case schema "$tab"
	done
	GTK_THEME=Adwaita:dark run_case dark 0
fi

exit $(( failures > 0 ))
