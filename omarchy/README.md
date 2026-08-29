# Omarchy integration

`plugin/leecher.media/` is the Leecher bar widget, installed to
`~/.config/omarchy/plugins/leecher.media/`. `shell.json` records the current
Omarchy bar layout, including the `leecher.media` entry.

## Installing

```sh
./omarchy/install.sh
```

The installer:

- checks runtime dependencies up front (`ssh`, `ssh-agent`, `curl`, `ffmpeg`,
  `jq`; warns if `zenity` is missing);
- copies the backend binaries into a stable location,
  `~/.local/lib/leecher/` — never the source checkout;
- writes a default `library.json` if none exists;
- installs the widget and adds `leecher.media` to the bar (backing up an
  existing `shell.json`);
- generates `omarchy/systemd/leecher-headless.service` with the real install
  path and enables the `leecher-headless.service` user unit.

### Confirmation

- On an Omarchy system it asks a simple `[Y/n]`.
- On any other system it warns that the widget targets Omarchy (so only the
  headless backend will be installed) and requires you to type `im sure` to
  continue.

### Installing without building

From a source checkout the installer builds the backend (`make`) when no
prebuilt binaries are present, which needs SDL2 and libsndfile. To avoid
building, create a self-contained release bundle:

```sh
./release.sh
# -> dist/leecher-<version>.tar.gz
```

Unzip it and run the bundled `omarchy/install.sh` — it uses the shipped
prebuilt `app` and `library-handler` directly.

## Uninstalling

```sh
./omarchy/uninstall.sh
```

Removes the widget and bar entry (after backing up `shell.json`), disables and
removes the user service, deletes the installed backend at
`~/.local/lib/leecher/`, and cleans the per-user runtime IPC state
(`$XDG_RUNTIME_DIR/leecher`, or `/tmp/leecher-<uid>`) plus any legacy `/tmp`
files.

The single systemd template lives at `omarchy/systemd/leecher-headless.service`.
