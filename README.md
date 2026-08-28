# music-leecher
leeches music off of local files, local netwokrs, SSH agents, or online

## Omarchy widget backend

Build the player with `make`, then run the backend without its SDL window:

```sh
./app --headless /absolute/path/to/library.json
```

It keeps the existing Omarchy widget protocol: state is written to
`/tmp/leecher-status.json` and controls are read from `/tmp/leecher-control`.
The Omarchy integration is collected in `omarchy/`: the widget is in
`omarchy/plugin/leecher.media/`, and the systemd user unit is at
`omarchy/systemd/leecher-headless.service`. Run `./omarchy/install.sh` to
build and install the complete integration, or `./omarchy/uninstall.sh` to
remove it.
