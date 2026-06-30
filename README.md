# wsjtxcli

Launch a throwaway, receive-only WSJT-X instance for headless SDR decoding of HF digital modes (FT8, FT4, WSPR, JT65, JT9, …).

Each run mints a unique per-instance rig name, writes a temporary config seeded with your station identity + operating parameters, launches WSJT-X against it, and cleans up the config and data dir on exit. Multiple instances can run side by side without clobbering each other's settings.

## Features

- Temporary, isolated config per launch — never touches your normal WSJT-X profile.
- **Receive-only by design**: `Rig=None` removes every CAT/RTS/DTR keying path and `TxWatchdog=1` is a backstop. Intended for SDR monitor sources with no transmitter in the chain.
- Tune by band (`--rx-band`, uses the mode's default dial frequency) or by explicit RF frequency (`--rx-frequency`).
- Optional PSK Reporter and WSPRnet spot reporting, with mode-aware guards.
- Headless operation on GUI-less servers via `xvfb-run` (`--nogui`).

## Requirements

- `wsjtx` on `PATH`
- `xvfb` (only for `--nogui`): `apt install xvfb` / `apk add xvfb`
- Python 3.10+ (only when running from source — not needed for the `.deb` or the standalone binary)

## Installation

Pick one of the three methods below. The `.deb` and the standalone binary both install a `wsjtxcli` command; substitute it for `python3 -m wsjtxcli.main` in the usage examples.

### 1. Debian / Ubuntu package (recommended for Debian/Ubuntu)

Download the `.deb` from the [latest release](../../releases/latest) and install it. Pulls in `wsjtx` and `xvfb` automatically. No Python required.

```bash
sudo apt install ./wsjtxcli_0.0.1_amd64.deb
```

### 2. Standalone Linux binary (recommended for everyone else)

A single self-contained `x86_64` binary that runs on any Linux distribution regardless of glibc version. No Python required. You still need `wsjtx` (and `xvfb` for `--nogui`) installed via your package manager.

```bash
curl -L -o wsjtxcli \
  https://github.com/ivica3730k/wsjtxcli/releases/latest/download/wsjtxcli-linux-x86_64
chmod +x wsjtxcli
sudo mv wsjtxcli /usr/local/bin/
```

### 3. From source (Python)

Requires Python 3.10+.

```bash
git clone https://github.com/ivica3730k/wsjtxcli.git
cd wsjtxcli
python3 -m wsjtxcli.main --help
```

## Usage

```bash
python3 -m wsjtxcli.main --callsign YOURCALL --grid AA00aa --mode FT8 --rx-band 40m # replace wsjtxcli.main just with wsjtxcli if you installed it
```

Headless on a server (omit `--audio-device` to use the system default source):

```bash
python3 -m wsjtxcli.main --callsign YOURCALL --grid AA00aa --mode WSPR --rx-band 20m \
  --wsprnet-reporter --nogui
```

Explicit dial frequency (e.g. FT8 off its usual sub-band):

```bash
python3 -m wsjtxcli.main --callsign YOURCALL --grid AA00aa --mode FT8 --rx-frequency 7100000 \
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

Exactly one of `--rx-band` / `--rx-frequency` is required.

## Finding your audio source if not using system default

```bash
pactl list short sources
```

Use the SDR demodulator's `.monitor` source (e.g. `sdr_usb.monitor`) as `--audio-device`. WSJT-X downmixes stereo→mono at 48 kHz internally.

## Default dial frequencies

`--rx-band` is supported for FT8, FT4, WSPR, JT65, and JT9 across the HF bands (plus 6 m / 2 m where applicable). For any other mode/band combination, pass `--rx-frequency` explicitly.

## Notes

- Config and data live under `$XDG_CONFIG_HOME` (default `~/.config`) and `$XDG_DATA_HOME` (default `~/.local/share`) respectively, created per-instance and removed on exit.
