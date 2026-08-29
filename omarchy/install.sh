#!/bin/sh
set -eu

omarchy_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$omarchy_dir/.." && pwd)
config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}
plugin_source=$omarchy_dir/plugin/leecher.media
plugin_destination=$config_dir/omarchy/plugins/leecher.media
shell_config=$config_dir/omarchy/shell.json
unit_destination=$config_dir/systemd/user/leecher-headless.service
timestamp=$(date +%Y%m%d%H%M%S)

command -v jq >/dev/null 2>&1 || {
    printf '%s\n' 'jq is required to update the Omarchy bar layout.' >&2
    exit 1
}

printf '%s\n' 'Building Leecher Music Player…'
make -C "$project_dir" app

printf '%s\n' 'Creating default library if missing…'
if [ ! -f "$project_dir/library.json" ]; then
    printf '%s\n' '{"version":1,"tracks":[]}' > "$project_dir/library.json"
    printf '%s\n' 'Created an empty library at library.json.'
fi

printf '%s\n' 'Installing Omarchy widget…'
install -d "$plugin_destination"
install -m 0644 "$plugin_source/manifest.json" "$plugin_destination/manifest.json"
install -m 0644 "$plugin_source/BarWidget.qml" "$plugin_destination/BarWidget.qml"

if [ -f "$shell_config" ]; then
    if ! jq -e 'any(.bar.layout.center[]?; .id == "leecher.media")' "$shell_config" >/dev/null; then
        backup=$shell_config.bak.$timestamp
        cp "$shell_config" "$backup"
        temporary=$shell_config.tmp.$$
        jq '.bar.layout.center += [{"id":"leecher.media"}]' "$shell_config" >"$temporary"
        mv "$temporary" "$shell_config"
        printf 'Added leecher.media to the Omarchy bar (backup: %s).\n' "$backup"
    fi
else
    install -Dm 0644 "$omarchy_dir/shell.json" "$shell_config"
    printf '%s\n' 'Installed the bundled Omarchy shell layout.'
fi

printf '%s\n' 'Installing and enabling headless backend…'
install -d "$(dirname -- "$unit_destination")"
temporary_unit=$unit_destination.tmp.$$
escaped_project_dir=$(printf '%s' "$project_dir" | sed 's/[\\&|]/\\&/g')
sed "s|%h/Work/leecher-music-player|$escaped_project_dir|g" \
    "$omarchy_dir/systemd/leecher-headless.service" >"$temporary_unit"
install -m 0644 "$temporary_unit" "$unit_destination"
rm -f "$temporary_unit"
systemctl --user daemon-reload
systemctl --user enable --now leecher-headless.service

if command -v omarchy-shell >/dev/null 2>&1; then
    omarchy-shell shell rescanPlugins || true
fi

printf '%s\n' 'Leecher Omarchy integration installed.'
