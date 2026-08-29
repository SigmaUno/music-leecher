#!/bin/sh
set -eu

# Build a self-contained Leecher release bundle: prebuilt backend binaries,
# installer/uninstaller, systemd template, Omarchy widget and example library.
#
# The produced tarball is the "release branch" artifact: users unzip it and
# run omarchy/install.sh, which uses the bundled binaries directly — no source
# checkout, no build-deps (SDL2/libsndfile) required.

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_dir=$script_dir
omarchy_dir=$script_dir/omarchy
out_dir=${1:-"$project_dir/dist"}
version=${VERSION:-$(date +%Y%m%d%H%M%S)}
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

printf '%s\n' 'Building release binaries...'
make -C "$project_dir" app library-handler

release_dir=$stage/leecher-$version
mkdir -p "$release_dir/omarchy/systemd" \
         "$release_dir/omarchy/plugin/leecher.media"

# Backend binaries
install -m 0755 "$project_dir/app" "$release_dir/app"
install -m 0755 "$project_dir/library-handler" "$release_dir/library-handler"

# Installer + templates
install -m 0755 "$omarchy_dir/install.sh" "$release_dir/omarchy/install.sh"
install -m 0755 "$omarchy_dir/uninstall.sh" "$release_dir/omarchy/uninstall.sh"
install -m 0644 "$omarchy_dir/systemd/leecher-headless.service" \
                "$release_dir/omarchy/systemd/leecher-headless.service"
install -m 0644 "$project_dir/library.example.json" \
                "$release_dir/library.example.json"

# Omarchy widget
install -m 0644 "$omarchy_dir/plugin/leecher.media/manifest.json" \
                "$release_dir/omarchy/plugin/leecher.media/manifest.json"
install -m 0644 "$omarchy_dir/plugin/leecher.media/BarWidget.qml" \
                "$release_dir/omarchy/plugin/leecher.media/BarWidget.qml"
install -m 0644 "$omarchy_dir/shell.json" "$release_dir/omarchy/shell.json"

archive=$out_dir/leecher-$version.tar.gz
mkdir -p "$out_dir"
tar -C "$stage" -czf "$archive" "leecher-$version"
printf '%s\n' "Release bundle: $archive"
printf '%s\n' 'Unzip it and run omarchy/install.sh to install.'
