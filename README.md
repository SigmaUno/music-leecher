# leecher-music-player

Leeches music off of local files, local networks, SSH agents, or online, and
turns a JSON "library" file into a streaming player. A Nuklear/SDL desktop UI
manages the library, while a headless backend drives an Omarchy bar widget.

## Features

- A JSON library is the single source of truth: each track can carry multiple
  sources, and the player falls back through them in library order.
- Four source kinds, with credentials left to the system agents:
  - **Local file** — read directly from disk.
  - **SSH** — streamed with `ssh` (BatchMode, key-based auth) into the system
    `ssh-agent`, never password prompts.
  - **HTTPS** — streamed with `curl` over TLS 1.2+.
  - **Local network** — a pluggable transport for network shares.
- Streaming pipeline: transports feed chunks into a bounded `Assembler` queue
  (backpressure-aware), which is decoded to 16-bit PCM via libsndfile and
  played through SDL audio.
- Autoplay with gapless-style prefetch: the next track is fetched and decoded
  before the current one ends.
- Metadata extraction from local and SSH files (`mediainfo`, falling back to
  `ffprobe`) auto-fills title/artist/album.
- Push-and-pull library scraping: import local directories, drag-and-drop audio
  files into the window, or pull every unique song under a local/SSH path.
- Headless mode writes state to `/tmp/leecher-status.json`, reads control
  commands from `/tmp/leecher-control`, and renders covers to
  `/tmp/leecher-cover.jpg`.

## Building

Dependencies:

- A C11 compiler, `make`
- SDL2 (`sdl2`), libsndfile (`sndfile`) — `pkg-config` must find them
- Runtime tools used by the transports: `ssh`, `curl`
- Optional, for metadata extraction: `mediainfo` or `ffprobe`
- Optional, for file pickers: `zenity`

```sh
make          # builds ./app and ./library-handler
make test     # builds and runs all unit + script tests
make clean
```

`./app` is the GUI player; `./library-handler` is a standalone CLI for
resolving/mutating library JSON (used by the test scripts).

## Usage

```sh
./app                          # GUI, default library.json
./app my-library.json          # GUI with a custom library
./app --headless my-library.json  # no window, Omarchy backend
```

In the GUI, pick a track from the LIBRARY pane to play it. The SOURCE SCRAPER
pane builds a new source entry (title/artist/album plus PATH, USERNAME, URL, or
IP depending on the selected kind) and saves it to the library atomically.

In headless mode the app runs the same player loop, forwards audio to SDL
(which goes to the desktop audio graph via PipeWire/PulseAudio), and serves the
widget protocol described below.

### Control protocol

Headless state is a JSON object written to `/tmp/leecher-status.json`:

```json
{"title":"Airbag","artist":"Radiohead","album":"OK Computer",
 "position_ms":12000,"duration_ms":232000,"is_playing":true,
 "track_index":1,"library":"/abs/path/library.json","autoplay":true,
 "cover":"/tmp/leecher-cover-1.jpg"}
```

Drop a single command line into `/tmp/leecher-control` and it is consumed on
the next frame:

```
play_pause      play 16          next            previous
seek 45000      autoplay on/off
set_title 3 …   set_artist 3 …   set_album 3 …   remove 4
```

## Library format

`library.json` (created by `omarchy/install.sh` when missing) shows the schema:

```json
{
  "version": 1,
  "tracks": [
    {
      "id": "mbid:8c98e68d-…",
      "title": "Teardrop",
      "artist": "Massive Attack",
      "album": "Mezzanine",
      "duration_ms": 330000,
      "sources": [
        { "kind": "local",  "PATH": "/music/…01 Teardrop.flac", "USERNAME": null, "URL": null, "IP": null },
        { "kind": "ssh",    "PATH": "/srv/music/…",              "USERNAME": "music", "URL": null, "IP": "192.0.2.10" },
        { "kind": "https",  "PATH": null,                         "USERNAME": null,   "URL": "https://…/teardrop.flac", "IP": null },
        { "kind": "network","PATH": "/share/…",                   "USERNAME": "guest", "URL": null, "IP": "192.0.2.20" }
      ]
    }
  ]
}
```

`id` is optional. Library edits are written atomically; sources are appended to
a track that already matches on title and artist, or create a new track
otherwise.

## Requirements: a kernel and a library, not passwords

Authentication belongs to the desktop's system agents, not to this app:

- SSH uses `ssh-agent` (the app spawns one when needed) with `BatchMode=yes`;
  you unlock it with the **Unlock SSH agent** button, which calls `ssh-add`.
- Network credentials are owned by the desktop keyring /
  `nm-connection-editor`, opened via **Network settings**.

The app only ever stores PATH, USERNAME, URL, and IP.

## Architecture

| File                  | Role                                                |
| --------------------- | --------------------------------------------------- |
| `app.c`               | GUI (Nuklear/SDL), headless loop, transports, cover |
| `library_handler.c`   | Load, resolve, and atomically mutate the JSON library |
| `music_ripper.c`      | Chooses the first usable source and streams it      |
| `assembler.c`         | Bounded, backpressure-aware byte queue              |
| `decoder.c`           | Decodes queued bytes into 16-bit PCM (libsndfile)   |
| `metadata.c`          | `mediainfo`/`ffprobe` metadata extraction           |
| `omarchy/`            | Omarchy bar widget, systemd unit, install scripts   |

See `METADATA_EXTRACTION.md` for the metadata feature and
`omarchy/README.md` for the widget integration.

## Omarchy widget backend

See [omarchy/README.md](omarchy/README.md). In short:

```sh
./app --headless /absolute/path/to/library.json
```

plus `./omarchy/install.sh` to build, install the `leecher.media` bar widget,
add it to `shell.json`, and enable the `leecher-headless.service` user unit —
or `./omarchy/uninstall.sh` to remove it.

## Testing

```sh
make test
```

Covers library resolution and mutation, assembler backpressure, PCM decoding,
metadata extraction, and music ripper source selection. See `tests/`.