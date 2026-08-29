#!/bin/sh
set -eu

# ---------------------------------------------------------------------------
# Leecher Music Player installer
#
# Install location is stable and NEVER the source checkout: binaries go into
# ~/.local/lib/leecher/ and the systemd unit is generated to point there.
#
# The script works two ways:
#   * from a packaged release (app/library-handler shipped alongside this
#     script and the plugin/ + systemd/ templates), where nothing is built,
#   * from a source checkout, where the backend is built on the fly.
#
# On non-Omarchy systems we warn that the bar widget targets Omarchy and ask
# for the "im sure" catchphrase before downloading/building anything.
# ---------------------------------------------------------------------------

config_dir=${XDG_CONFIG_HOME:-"$HOME/.config"}
install_dir=${LEECHER_INSTALL_DIR:-"$HOME/.local/lib/leecher"}
unit_destination=$config_dir/systemd/user/leecher-headless.service
plugin_destination=$config_dir/omarchy/plugins/leecher.media
shell_config=$config_dir/omarchy/shell.json
timestamp=$(date +%Y%m%d%H%M%S)

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

# --- Omarchy detection ------------------------------------------------------
on_omarchy=0
if command -v omarchy-shell >/dev/null 2>&1 || [ -d "$config_dir/omarchy" ]; then
    on_omarchy=1
fi

# --- Dependency checks (fail fast, per Issue #20) ---------------------------
fail_dep() {
    printf 'Missing required dependency: %s\n' "$*" >&2
    printf 'Install it for your distribution, then re-run this installer.\n' >&2
    exit 1
}

for tool in ssh ssh-agent curl ffmpeg; do
    command -v "$tool" >/dev/null 2>&1 || fail_dep "$tool"
done

# zenity is only needed for the GUI file pickers; warn rather than fail.
if ! command -v zenity >/dev/null 2>&1; then
    printf 'Note: zenity was not found. GUI file selection will be unavailable;\n' >&2
    printf '      the backend will still run headless.\n' >&2
fi

# jq is only required to edit the Omarchy shell layout.
if [ "$on_omarchy" -eq 1 ]; then
    command -v jq >/dev/null 2>&1 || fail_dep "jq (to update the Omarchy bar layout)"
fi

# --- Confirmation gate ------------------------------------------------------
# On Omarchy: a simple yes/enter. Elsewhere: warn + require "im sure".
printf '%s\n' 'Leecher Music Player installer'
printf '%s\n' '---------------------------'
printf 'This will install the Leecher hardware UI into %s.\n' "$install_dir"
printf 'It may download/build binaries and start a user service (headless backend).\n'

if [ "$on_omarchy" -eq 1 ]; then
    printf 'Proceed with install? [Y/n] '
    read -r answer
    case "$answer" in
        n|N|no|NO) printf 'Aborted.\n'; exit 0 ;;
        *) printf 'Installing...\n' ;;
    esac
else
    printf '%s\n' ''
    printf '%s\n' 'WARNING: This does not appear to be an Omarchy system.' >&2
    printf '%s\n' 'The Leecher bar widget is written for Omarchy. Without Omarchy,' >&2
    printf '%s\n' 'only the headless backend will be installed and no widget will show.' >&2
    printf '%s\n' '' >&2
    while :; do
        printf 'Type "im sure" to confirm you still want to install: '
        read -r answer
        if [ "$answer" = "im sure" ]; then
            printf 'Installing...\n'
            break
        fi
        printf '(not recognized)\n'
    done
fi

# --- Obtain the backend binaries -------------------------------------------
# Prefer a packaged release bundled with this script; fall back to building.
app_source=""
library_handler_source=""
if [ -f "$script_dir/app" ] && [ -f "$script_dir/library-handler" ]; then
    printf '%s\n' 'Using prebuilt binaries from the release bundle.'
    app_source=$script_dir/app
    library_handler_source=$script_dir/library-handler
elif [ -f "$project_dir/app" ] && [ -f "$project_dir/library-handler" ]; then
    printf '%s\n' 'Using built binaries already present in the checkout.'
    app_source=$project_dir/app
    library_handler_source=$project_dir/library-handler
else
    printf '%s\n' 'Building Leecher Music Player backend...'
    if ! command -v make >/dev/null 2>&1; then
        fail_dep 'make (to build the backend from source)'
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
        fail_dep 'pkg-config (to build the backend from source)'
    fi
    if ! pkg-config --exists sdl2 sndfile; then
        printf 'Build dependencies missing (SDL2 or libsndfile development packages).\n' >&2
        printf 'Either install them or obtain a packaged Leecher release with prebuilt binaries.\n' >&2
        exit 1
    fi
    make -C "$project_dir" app library-handler
    app_source=$project_dir/app
    library_handler_source=$project_dir/library-handler
fi

# --- Copy backend into the stable install dir ------------------------------
printf 'Installing backend binaries to %s...\n' "$install_dir"
install -d "$install_dir"
install -m 0755 "$app_source" "$install_dir/app"
install -m 0755 "$library_handler_source" "$install_dir/library-handler"

printf '%s\n' 'Creating default library if missing...'
if [ ! -f "$install_dir/library.json" ]; then
    if [ -f "$script_dir/library.example.json" ]; then
        install -m 0644 "$script_dir/library.example.json" "$install_dir/library.json"
    elif [ -f "$project_dir/library.example.json" ]; then
        install -m 0644 "$project_dir/library.example.json" "$install_dir/library.json"
    else
        printf '%s\n' '{"version":1,"tracks":[]}' > "$install_dir/library.json"
    fi
    printf '%s\n' 'Created a default library at library.json.'
fi

# --- Omarchy widget + bar layout (only makes sense on Omarchy) -------------
if [ "$on_omarchy" -eq 1 ]; then
    plugin_source=$script_dir/plugin/leecher.media
    [ -d "$plugin_source" ] || plugin_source=$project_dir/omarchy/plugin/leecher.media

    printf '%s\n' 'Installing Omarchy widget...'
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
        bundled_shell=$script_dir/shell.json
        [ -f "$bundled_shell" ] || bundled_shell=$project_dir/omarchy/shell.json
        install -Dm 0644 "$bundled_shell" "$shell_config"
        printf '%s\n' 'Installed the bundled Omarchy shell layout.'
    fi
else
    printf '%s\n' 'Skipping Omarchy widget/bar setup (not on Omarchy).'
fi

# --- Headless systemd service ----------------------------------------------
printf '%s\n' 'Installing and enabling headless backend...'
install -d "$(dirname -- "$unit_destination")"
temporary_unit=$unit_destination.tmp.$$

source_unit=$script_dir/systemd/leecher-headless.service
[ -f "$source_unit" ] || source_unit=$project_dir/omarchy/systemd/leecher-headless.service

escaped_install_dir=$(printf '%s' "$install_dir" | sed 's/[\\&|]/\\&/g')
sed "s|__LEECHER_DIR__|$escaped_install_dir|g" "$source_unit" >"$temporary_unit"
install -m 0644 "$temporary_unit" "$unit_destination"
rm -f "$temporary_unit"
systemctl --user daemon-reload
systemctl --user enable --now leecher-headless.service

if [ "$on_omarchy" -eq 1 ] && command -v omarchy-shell >/dev/null 2>&1; then
    omarchy-shell shell rescanPlugins || true
fi

printf '%s\n' 'Leecher installed. Backend: headless.service'
