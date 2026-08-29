#!/bin/sh
set -eu

config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}
install_dir=${LEECHER_INSTALL_DIR:-"$HOME/.local/lib/leecher"}
plugin_destination=$config_dir/omarchy/plugins/leecher.media
shell_config=$config_dir/omarchy/shell.json
unit_destination=$config_dir/systemd/user/leecher-headless.service
timestamp=$(date +%Y%m%d%H%M%S)

if [ -f "$shell_config" ]; then
    command -v jq >/dev/null 2>&1 || {
        printf '%s\n' 'jq is required to remove the Leecher bar entry safely.' >&2
        exit 1
    }
    if jq -e 'any(.bar.layout.center[]?; .id == "leecher.media")' "$shell_config" >/dev/null; then
        backup=$shell_config.bak.$timestamp
        cp "$shell_config" "$backup"
        temporary=$shell_config.tmp.$$
        jq '.bar.layout.center |= map(select(.id != "leecher.media"))' "$shell_config" >"$temporary"
        mv "$temporary" "$shell_config"
        printf 'Removed leecher.media from the Omarchy bar (backup: %s).\n' "$backup"
    fi
fi

if command -v systemctl >/dev/null 2>&1; then
    systemctl --user disable --now leecher-headless.service 2>/dev/null || true
    systemctl --user daemon-reload
fi
rm -f "$unit_destination"

rm -f "$plugin_destination/manifest.json" "$plugin_destination/BarWidget.qml"
if [ -d "$plugin_destination" ] && rmdir "$plugin_destination" 2>/dev/null; then
    printf '%s\n' 'Removed Leecher Omarchy widget.'
elif [ -d "$plugin_destination" ]; then
    printf 'Left %s in place because it contains files not managed by this installer.\n' "$plugin_destination"
fi

# --- Remove the installed backend (binaries, default library) --------------
if [ -d "$install_dir" ]; then
    printf 'Removing installed backend at %s...\n' "$install_dir"
    rm -rf "$install_dir"
fi

# --- Clean runtime IPC state left under /tmp (see Issue #20) ----------------
rm -f /tmp/leecher-status.json /tmp/leecher-control /tmp/leecher-cover.jpg
rm -f /tmp/leecher-cover-*.jpg

if command -v omarchy-shell >/dev/null 2>&1; then
    omarchy-shell shell rescanPlugins || true
fi

printf '%s\n' 'Leecher uninstalled.'
