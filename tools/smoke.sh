#!/bin/sh
# Proves a built wsjtxcli works on a machine where nothing has been installed:
# it starts, writes an instance config a fake wsjtx can read back, and leaves
# nothing behind. Run as: tools/smoke.sh path/to/wsjtxcli
#
# POSIX sh on purpose -- this runs inside stock distribution images, several of
# which have no bash.
set -e

BIN=$1
if [ -z "$BIN" ]; then
  echo "usage: smoke.sh <path to wsjtxcli>" >&2
  exit 2
fi

ROOT=$(mktemp -d)
trap 'rm -rf "$ROOT"' EXIT
mkdir -p "$ROOT/bin" "$ROOT/record" "$ROOT/config" "$ROOT/data"

cat > "$ROOT/bin/wsjtx" <<'FAKE'
#!/bin/sh
rig=${1#--rig-name=}
cp "$XDG_CONFIG_HOME/WSJT-X - $rig.ini" "$RECORD/instance.ini"
echo "$1" > "$RECORD/argv"
FAKE
chmod 755 "$ROOT/bin/wsjtx"

"$BIN" --version

RECORD="$ROOT/record" \
XDG_CONFIG_HOME="$ROOT/config" \
XDG_DATA_HOME="$ROOT/data" \
HOME="$ROOT" \
PATH="$ROOT/bin:$PATH" \
  "$BIN" --callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m --audio-device sdr_usb.monitor

for expected in "MyCall=9A3ICE" "MyGrid=JN95" "Mode=FT8" "DialFreq=7074000" \
                "SoundInName=sdr_usb.monitor" "Rig=None" "TxWatchdog=1"; do
  grep -qx "$expected" "$ROOT/record/instance.ini" || {
    echo "FAILED: instance config has no $expected" >&2
    cat "$ROOT/record/instance.ini" >&2
    exit 1
  }
done

grep -q -- "--rig-name=wsjtx-" "$ROOT/record/argv" || {
  echo "FAILED: wsjtx was not given a per-instance rig name" >&2
  exit 1
}

# The config and the data directory belong to the run, not to the operator's
# profile, so neither may outlive it.
if [ -n "$(ls -A "$ROOT/config")" ] || [ -n "$(ls -A "$ROOT/data")" ]; then
  echo "FAILED: the instance left files behind" >&2
  ls -A "$ROOT/config" "$ROOT/data" >&2
  exit 1
fi

# A missing wsjtx has to be reported rather than guessed at.
if HOME="$ROOT" PATH="$ROOT/record" "$BIN" --callsign 9A3ICE --grid JN95 --mode FT8 --rx-band 40m \
     2>"$ROOT/stderr"; then
  echo "FAILED: a missing wsjtx was not reported" >&2
  exit 1
fi
grep -qx "wsjtx binary not found in PATH" "$ROOT/stderr" || {
  echo "FAILED: unexpected message for a missing wsjtx" >&2
  cat "$ROOT/stderr" >&2
  exit 1
}

echo "ok: $BIN"
