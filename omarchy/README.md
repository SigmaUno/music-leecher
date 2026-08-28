# Omarchy integration

`plugin/leecher.media/` is the Leecher bar widget. Copy or symlink that
directory to `~/.config/omarchy/plugins/leecher.media/`.

`shell.json` records the current Omarchy bar layout, including the
`leecher.media` entry. `systemd/leecher-headless.service` starts the widget's
headless backend with the user session.

Run `./omarchy/install.sh` from the repository to build the backend, install
the plugin, add the bar entry when needed, and enable the user service. The
installer backs up an existing `shell.json` before changing it.

Run `./omarchy/uninstall.sh` to disable and remove the Leecher service and
widget. It also removes the bar entry after backing up `shell.json`.
