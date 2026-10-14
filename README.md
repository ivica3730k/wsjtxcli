# wsjtxcli

Launch a throwaway, receive-only WSJT-X instance for headless SDR decoding of HF digital modes (FT8, FT4, WSPR, JT65, JT9, …).

Each run mints a unique per-instance rig name, writes a temporary config seeded with your station identity + operating parameters, launches WSJT-X against it, and cleans up the config and data dir on exit. Multiple instances can run side by side without clobbering each other's settings.

This is a C++17 port of the original Python implementation. It writes the same instance config, takes the same arguments and returns the same exit statuses — the configs the Python version produced are checked in under `tests/golden` and the suite compares against them. What changed is the packaging: one static binary with no interpreter, no virtualenv and no runtime dependencies beyond glibc.

## Features

- Temporary, isolated config per launch — never touches your normal WSJT-X profile.
- **Receive-only by design**: `Rig=None` removes every CAT/RTS/DTR keying path and `TxWatchdog=1` is a backstop. Intended for SDR monitor sources with no transmitter in the chain.
- Tune by band (`--rx-band`, uses the mode's default dial frequency) or by explicit RF frequency (`--rx-frequency`).
- Optional PSK Reporter and WSPRnet spot reporting, with mode-aware guards.
- Headless operation on GUI-less servers via `xvfb-run` (`--nogui`).

## Requirements

- `wsjtx` on `PATH`
- `xvfb` (only for `--nogui`): `apt install xvfb`
- Nothing else. The binary links libstdc++ statically and is built against Ubuntu 22.04's glibc, so it runs on anything newer without matching the build machine.

## Installation

### 1. Debian / Ubuntu package (recommended for Debian/Ubuntu)

Download the `.deb` from the [latest release](../../releases/latest) and install it. Pulls in `wsjtx` and `xvfb` automatically.

```bash
sudo apt install ./wsjtxcli_1.0.0_amd64.deb
```

### 2. Standalone binary (recommended for everyone else)

One file per CPU architecture, no packages to install. You still need `wsjtx` (and `xvfb` for `--nogui`) from your package manager.

```bash
curl -L -o wsjtxcli \
  https://github.com/ivica3730k/wsjtxcli/releases/latest/download/wsjtxcli-linux-x86_64-latest
chmod +x wsjtxcli
sudo mv wsjtxcli /usr/local/bin/
```

`wsjtxcli-linux-arm64-latest` is the Raspberry Pi / aarch64 build.

### 3. From source

Needs CMake 3.16+ and a C++17 compiler. Nothing is downloaded during the build: Catch2 and CLI11 are vendored under `third_party/`.

```bash
git clone https://github.com/ivica3730k/wsjtxcli.git
cd wsjtxcli
make build
sudo make install          # or just use ./binaries/host/wsjtxcli
```

## Usage

```bash
wsjtxcli --callsign YOURCALL --grid AA00aa --mode FT8 --rx-band 40m
```

Headless on a server (omit `--audio-device` to use the system default source):

```bash
wsjtxcli --callsign YOURCALL --grid AA00aa --mode WSPR --rx-band 20m \
  --wsprnet-reporter --nogui
```

Explicit dial frequency (e.g. FT8 off its usual sub-band):

```bash
wsjtxcli --callsign YOURCALL --grid AA00aa --mode FT8 --rx-frequency 7100000 \
  --audio-device sdr_usb.monitor
```

## Arguments

| Flag                 | Required | Description                                                                             |
| -------------------- | -------- | --------------------------------------------------------------------------------------- |
| `--callsign`         | yes      | Station callsign.                                                                       |
| `--grid`             | yes      | 4- or 6-char Maidenhead locator.                                                        |
| `--mode`             | yes      | Operating mode (FT8, FT4, WSPR, JT65, JT9, MSK144, Q65, FST4, FST4W, JT4, QRA64, Echo). |
| `--rx-band`          | one of   | Band like `40m`; resolves to the mode's default dial frequency.                         |
| `--rx-frequency`     | one of   | Explicit RF dial frequency in Hz. Mutually exclusive with `--rx-band`.                  |
| `--audio-device`     | no       | Capture source name (e.g. `sdr_usb.monitor`). Omit for system default.                  |
| `--psk-reporter`     | no       | Upload decodes to PSK Reporter. Not allowed with `--mode WSPR`.                         |
| `--wsprnet-reporter` | no       | Upload WSPR spots to wsprnet.org. Only allowed with `--mode WSPR`.                      |
| `--nogui`            | no       | Run on a virtual 1024×768 X display via `xvfb-run`.                                     |
| `--version`          | no       | Print the version and exit.                                                             |

Exactly one of `--rx-band` / `--rx-frequency` is required.

## Finding your audio source if not using system default

```bash
pactl list short sources
```

Use the SDR demodulator's `.monitor` source (e.g. `sdr_usb.monitor`) as `--audio-device`. WSJT-X downmixes stereo→mono at 48 kHz internally.

## Default dial frequencies

`--rx-band` is supported for FT8, FT4, WSPR, JT65, and JT9 across the HF bands (plus 6 m / 2 m where applicable). For any other mode/band combination, pass `--rx-frequency` explicitly.

## Notes

- Config and data live under `$XDG_CONFIG_HOME` (default `~/.config`) and `$XDG_DATA_HOME` (default `~/.local/share`) respectively, created per-instance and removed on exit — including when the run ends on Ctrl-C.
- When launching a non-snap `/usr/bin/wsjtx`, `wsjtxcli` strips snap runtime variables (including `SNAP*`, `LD_*`, and snap Qt/GTK paths) to avoid Qt/glibc symbol lookup conflicts in GUI mode.
- Ctrl-C is passed on to WSJT-X, and its exit status becomes `wsjtxcli`'s.

## Development

```bash
make test                          # configure, build and run the Catch2 suite
./tools/smoke.sh binaries/test/wsjtxcli   # launch a fake wsjtx end to end
./binaries/test/wsjtxcli-tests "[station]"   # one tag
```

The suite covers argument validation, the rendered instance config against the
golden files, the snap environment scrubbing, and a full launch against a fake
`wsjtx` placed on `PATH` — argv, child environment, exit status and cleanup.

CI runs the same suite on x86_64 and arm64, then drops the release binary into
stock Ubuntu, Debian, Arch and Raspberry Pi OS images with nothing installed
and makes it launch there too.

## Differences from the Python implementation

Behaviour is the same where it is observable: identical instance configs,
identical validation messages, identical exit statuses (2 for a bad command
line, 1 for a mode/band with no default dial frequency, otherwise WSJT-X's own).
Three things did change:

1. `--help` is formatted by CLI11 rather than argparse.
2. Long options can no longer be abbreviated. `--call` was accepted by argparse; it is not accepted here.
3. `--version` was added, because a packaged binary with no interpreter behind it has no other way to answer the question.
