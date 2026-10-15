# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

C++17 port of the Python `wsjtxcli`. It mints a unique rig name, writes a
temporary WSJT-X instance config where WSJT-X reads one, launches
`wsjtx --rig-name=<name>` (optionally under `xvfb-run`), forwards Ctrl-C to it,
and removes the config and its data directory however the run ends.

Linux and macOS. The Python implementation was Linux only, so macOS is an
addition rather than a divergence: `--nogui` is refused there (no X display for
Xvfb to start) and the instance directories are the Cocoa ones.

Behaviour compatibility with the Python original is the constraint: the
instance config, the validation messages and the exit statuses are what users
and their scripts depend on.

## Common commands

```bash
make test                                    # configure, build, run the suite
make build                                   # -> binaries/host/wsjtxcli
./binaries/test/wsjtxcli-tests "[station]"   # one tag
./tools/smoke.sh binaries/test/wsjtxcli      # end-to-end against a fake wsjtx
make install PREFIX=/usr/local               # the binary, nothing else

cmake -S . -B build/dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON  # for clangd
```

## Architecture

One CMake target per concern under `libs/`, each with its public headers in
`libs/<name>/include/wsjtxcli/`:

- **`wx_core`** — the two exception types: `UsageError` (exit 2) and `RuntimeFailure` (exit 1).
- **`wx_station`** — callsign/grid/mode/band/frequency validation and the default dial frequency tables.
- **`wx_config`** — the instance `.ini` and the directories it goes in.
- **`wx_process`** — snap environment scrubbing, `$PATH` lookup, fork/exec with SIGINT forwarding.
- **`wx_cli`** — argument wiring and the launch sequence.

`apps/wsjtxcli` is a `main` that hands `argv` to `wx_cli`. CLI11 and Catch2 come
from `cmake/ThirdParty.cmake`: a system package when one is installed, a pinned
fetch otherwise. `third_party/` keeps only the licence texts those two ask to
travel with binaries that include them.

## Constraints and conventions

- **The instance config is the wire format.** `tests/golden/*.ini` came out of
  the Python implementation before it was deleted; they are the tripwire. Do
  not regenerate them to match new behaviour.
- **Dial frequency tables keep their table order**, because the "known bands"
  error message lists them in it. That is why they are vectors, not maps.
- **The snap scrubbing decides on context before it removes anything.**
  Dropping the `SNAP*` variables first would hide the context they establish.
- **The instance directories are Qt's, not ours.** WSJT-X resolves them with
  `QStandardPaths::ConfigLocation` and `DataLocation`, so `config_home()` and
  `data_home()` have to return what Qt would: the XDG directories on Linux,
  `~/Library/Preferences` and `~/Library/Application Support` on macOS, where
  Qt ignores the XDG variables entirely.
- **Exit statuses are part of the contract**: 2 for a bad command line, 1 for a
  refusal, otherwise WSJT-X's own status (negated signal number if it was
  killed, the convention Python's `subprocess` handed back).
- **Scope discipline**: mirror the Python source, answer questions literally,
  don't add features / aliases / tests unless asked.
- **Comments are for non-obvious *why*** (a hidden constraint, a workaround, a
  surprising behaviour). Don't restate what the code does or reference tickets.
- Warnings are errors in spirit: the build is clean under `-Wall -Wextra
  -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast`. Keep it
  that way; a fetched dependency's headers are marked SYSTEM so their warnings
  do not leak in.

## Deliberate divergences from Python

Only these three, all documented in the README:

1. `--help` is formatted by CLI11 rather than argparse.
2. Long options cannot be abbreviated; argparse accepted `--call` for `--callsign`.
3. `--version` was added — a packaged binary has no other way to answer it.

## Verification

```bash
make test                                 # the suite, including the launch tests
./tools/smoke.sh binaries/test/wsjtxcli   # what CI runs inside stock distro images
```

The launch tests drive the shipped binary with a fake `wsjtx` on `PATH` and
check the argv, the child environment, the exit status and that nothing is left
behind in either XDG directory.
