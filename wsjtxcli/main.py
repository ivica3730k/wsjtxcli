#!/usr/bin/env python3

import argparse
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
from pathlib import Path

CALLSIGN_RE = re.compile(r"^[A-Z0-9/]+$")
GRID_RE = re.compile(r"^[A-R]{2}\d{2}(?:[A-X]{2})?$")
BAND_RE = re.compile(r"^(\d+)\s*M$")

# Virtual display geometry used for headless (--nogui) operation.
NOGUI_SCREEN = "1024x768x24"

# Maps normalized (uppercased) mode input to the value WSJT-X writes to disk.
MODE_CANONICAL = {
    "FT4": "FT4",
    "FT8": "FT8",
    "JT4": "JT4",
    "JT9": "JT9",
    "JT65": "JT65",
    "QRA64": "QRA64",
    "MSK144": "MSK144",
    "WSPR": "WSPR",
    "ECHO": "Echo",
    "FST4": "FST4",
    "FST4W": "FST4W",
    "Q65": "Q65",
}

# Default RF dial frequency (Hz) per mode per band, mirroring WSJT-X defaults.
# Bands keyed as normalized "<n>m" tokens.
DEFAULT_DIAL_FREQS = {
    "FT8": {
        "160m": 1840000,
        "80m": 3573000,
        "60m": 5357000,
        "40m": 7074000,
        "30m": 10136000,
        "20m": 14074000,
        "17m": 18100000,
        "15m": 21074000,
        "12m": 24915000,
        "10m": 28074000,
        "6m": 50313000,
        "2m": 144174000,
    },
    "FT4": {
        "80m": 3575000,
        "40m": 7047500,
        "30m": 10140000,
        "20m": 14080000,
        "17m": 18104000,
        "15m": 21140000,
        "12m": 24919000,
        "10m": 28180000,
        "6m": 50318000,
    },
    "WSPR": {
        "160m": 1836600,
        "80m": 3568600,
        "60m": 5364700,
        "40m": 7038600,
        "30m": 10138700,
        "20m": 14095600,
        "17m": 18104600,
        "15m": 21094600,
        "12m": 24924600,
        "10m": 28124600,
        "6m": 50293000,
        "2m": 144489000,
    },
    "JT65": {
        "160m": 1838000,
        "80m": 3570000,
        "40m": 7076000,
        "30m": 10138000,
        "20m": 14076000,
        "17m": 18102000,
        "15m": 21076000,
        "12m": 24917000,
        "10m": 28076000,
    },
    "JT9": {
        "160m": 1838000,
        "80m": 3572000,
        "40m": 7078000,
        "30m": 10140000,
        "20m": 14078000,
        "17m": 18104000,
        "15m": 21078000,
        "12m": 24919000,
        "10m": 28078000,
    },
}


def parse_callsign(value: str) -> str:
    callsign = value.strip().upper()
    if not callsign or not CALLSIGN_RE.fullmatch(callsign):
        raise argparse.ArgumentTypeError(f"invalid callsign: {value!r}")
    return callsign


def parse_grid(value: str) -> str:
    grid = value.strip().upper()
    if not GRID_RE.fullmatch(grid):
        raise argparse.ArgumentTypeError(
            "grid must be a 4- or 6-character Maidenhead locator"
        )
    return grid


def parse_mode(value: str) -> str:
    mode = value.strip().upper()
    canonical = MODE_CANONICAL.get(mode)
    if canonical is None:
        raise argparse.ArgumentTypeError(
            f"unknown mode: {value!r} (choices: {', '.join(sorted(MODE_CANONICAL))})"
        )
    return canonical


def parse_band(value: str) -> str:
    match = BAND_RE.fullmatch(value.strip().upper())
    if match is None:
        raise argparse.ArgumentTypeError(
            f"band must look like '40m', '20m', etc.: {value!r}"
        )
    return f"{int(match.group(1))}m"


def parse_frequency(value: str) -> int:
    try:
        freq = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError(
            f"rx frequency must be an integer number of Hz: {value!r}"
        )
    if not 0 < freq <= 2_000_000_000:
        raise argparse.ArgumentTypeError("rx frequency must be between 1 Hz and 2 GHz")
    return freq


def parse_audio_device(value: str) -> str:
    return value.strip()


def resolve_dial_frequency(mode: str, band: str) -> int:
    band_table = DEFAULT_DIAL_FREQS.get(mode)
    if band_table is None:
        raise SystemExit(
            f"no default dial frequencies known for mode {mode}; "
            "pass --rx-frequency explicitly"
        )
    dial = band_table.get(band)
    if dial is None:
        known = ", ".join(band_table)
        raise SystemExit(
            f"no default {mode} dial frequency for band {band} "
            f"(known: {known}); pass --rx-frequency explicitly"
        )
    return dial


def xdg_path(env_var: str, fallback: str) -> Path:
    raw_value = os.environ.get(env_var)
    return Path(raw_value).expanduser() if raw_value else Path.home() / fallback


def write_instance_config(
    config_path: Path,
    callsign: str,
    grid: str,
    mode: str,
    dial_frequency: int,
    *,
    psk_reporter: bool = False,
    wsprnet_reporter: bool = False,
    audio_device: str | None = None,
) -> None:
    def boolstr(flag: bool) -> str:
        return "true" if flag else "false"

    lines = [
        "# WSJT-X station identity settings.",
        "[Configuration]",
        "# The callsign this temporary WSJT-X instance should use.",
        f"MyCall={callsign}",
        "# The Maidenhead grid locator this temporary WSJT-X instance should use.",
        f"MyGrid={grid}",
        "# Whether to report decodes to PSK Reporter.",
        f"PSKReporter={boolstr(psk_reporter)}",
        "# Receive-only lock: no rig means no CAT/RTS/DTR keying path exists.",
        "Rig=None",
        "# Backstop: force any transmission to stop after one minute.",
        "TxWatchdog=1",
    ]
    if audio_device:
        lines += [
            "# Audio input device this instance should capture from.",
            f"SoundInName={audio_device}",
        ]

    lines += [
        "# WSJT-X operating settings.",
        "[Common]",
        "# Whether to upload WSPR spots to wsprnet.org.",
        f"UploadSpots={boolstr(wsprnet_reporter)}",
        "# The operating mode this instance should start in.",
        f"Mode={mode}",
        "# The RF dial frequency (Hz) this instance should tune to.",
        f"DialFreq={dial_frequency}",
        "# WSJT-X multi-instance behavior settings.",
        "[MultiSettings]",
        "# Keep the current settings profile on the default name.",
        "CurrentName=Default",
        "# Disable the startup splash and release-notes prompt.",
        "Splash_v1.7=false",
    ]

    content = "\n".join(lines) + "\n"
    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(content, encoding="utf-8")


def terminate_child(process: subprocess.Popen[bytes]) -> int:
    process.send_signal(signal.SIGINT)
    return process.wait()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Launch WSJT-X with a temporary per-instance rig name."
    )
    parser.add_argument("--callsign", required=True, type=parse_callsign)
    parser.add_argument("--grid", required=True, type=parse_grid)
    parser.add_argument("--mode", required=True, type=parse_mode)

    freq_group = parser.add_mutually_exclusive_group(required=True)
    freq_group.add_argument("--rx-band", type=parse_band, default=None)
    freq_group.add_argument("--rx-frequency", type=parse_frequency, default=None)

    parser.add_argument("--audio-device", type=parse_audio_device, default=None)
    parser.add_argument("--psk-reporter", action="store_true")
    parser.add_argument("--wsprnet-reporter", action="store_true")
    parser.add_argument(
        "--nogui",
        action="store_true",
        help="run headless on a virtual X display via xvfb-run",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if args.psk_reporter and args.mode == "WSPR":
        parser.error("--psk-reporter is not allowed with --mode WSPR")
    if args.wsprnet_reporter and args.mode != "WSPR":
        parser.error("--wsprnet-reporter is only allowed with --mode WSPR")

    if args.rx_frequency is not None:
        dial_frequency = args.rx_frequency
    else:
        dial_frequency = resolve_dial_frequency(args.mode, args.rx_band)

    wsjtx_bin = shutil.which("wsjtx")
    if wsjtx_bin is None:
        print("wsjtx binary not found in PATH", file=sys.stderr)
        return 1

    xvfb_run_bin = None
    if args.nogui:
        xvfb_run_bin = shutil.which("xvfb-run")
        if xvfb_run_bin is None:
            print("xvfb-run not found in PATH (install xvfb)", file=sys.stderr)
            return 1

    config_home = xdg_path("XDG_CONFIG_HOME", ".config")
    data_home = xdg_path("XDG_DATA_HOME", ".local/share")

    with tempfile.NamedTemporaryFile(prefix="wsjtx-", delete=True) as handle:
        rig_name = Path(handle.name).name
        instance_config_path = config_home / f"WSJT-X - {rig_name}.ini"
        instance_data_path = data_home / f"WSJT-X - {rig_name}"

        write_instance_config(
            instance_config_path,
            args.callsign,
            args.grid,
            args.mode,
            dial_frequency,
            psk_reporter=args.psk_reporter,
            wsprnet_reporter=args.wsprnet_reporter,
            audio_device=args.audio_device,
        )

        command = [wsjtx_bin, f"--rig-name={rig_name}"]
        if xvfb_run_bin is not None:
            command = [
                xvfb_run_bin,
                "-a",
                "-s",
                f"-screen 0 {NOGUI_SCREEN}",
                *command,
            ]

        try:
            process = subprocess.Popen(command)
            try:
                return process.wait()
            except KeyboardInterrupt:
                return terminate_child(process)
        finally:
            instance_config_path.unlink(missing_ok=True)
            shutil.rmtree(instance_data_path, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
