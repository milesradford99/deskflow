#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# One client connected to two servers at once, each on its own virtual display.
# Checks that the first server to cross over gets control, that the second is
# turned away (and takes its cursor back), and that it takes over once the
# first has been idle for the takeover delay.
#
# Needs: Xvfb, xdotool, a built tree (DESKFLOW_BUILD_DIR, default ./build-dev).
set -euo pipefail

BUILD=${DESKFLOW_BUILD_DIR:-$(pwd)/build-dev}
CORE="$BUILD/bin/deskflow-core"
WORK=$(mktemp -d)
DELAY_MS=${TAKEOVER_DELAY_MS:-2000}
PIDS=()
cleanup() {
  for pid in "${PIDS[@]}"; do kill "$pid" 2>/dev/null || true; done
  wait 2>/dev/null || true
  [ -n "${KEEP:-}" ] && echo "logs kept in $WORK" || rm -rf "$WORK"
}
trap cleanup EXIT

fail() { echo "FAIL: $*"; for f in "$WORK"/*.log; do echo "--- $f"; tail -40 "$f"; done; KEEP=1; exit 1; }
waitfor() { # file pattern seconds
  for _ in $(seq 1 $(($3 * 10))); do grep -q -- "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done
  fail "\"$2\" not in $(basename "$1") after $3 s"
}
count() { grep -c -- "$2" "$1" 2>/dev/null || true; }

for d in 91 92 93; do
  Xvfb ":$d" -screen 0 1280x800x24 -nolisten tcp >/dev/null 2>&1 &
  PIDS+=($!)
done
sleep 1

settings() { # name file display extra...
  local name=$1 file=$2 display=$3; shift 3
  {
    echo "[core]"; echo "computerName=$name"; echo "display=$display"
    echo "[security]"; echo "tlsEnabled=false"; echo "checkPeers=false"
    echo "[log]"; echo "level=Debug"
    printf '%s\n' "$@"
  } > "$file"
}

server() { # name port display
  local name=$1 port=$2 display=$3
  cat > "$WORK/$name.layout" <<LAYOUT
section: links
	$name:
		right = gamma
	gamma:
		left = $name
end
LAYOUT
  settings "$name" "$WORK/$name.conf" "$display" \
    "[server]" "externalConfig=true" "externalConfigFile=$WORK/$name.layout" \
    "[computer_0]" "name=$name" "[computer_1]" "name=gamma" "[core]" "port=$port" "downloadDir=$WORK/$name-dl"
  DISPLAY=$display XDG_CONFIG_HOME="$WORK/$name-cfg" XDG_STATE_HOME="$WORK/$name-state" \
    QT_QPA_PLATFORM=offscreen "$CORE" server --new-instance -s "$WORK/$name.conf" >"$WORK/$name.log" 2>&1 &
  PIDS+=($!)
}

server alpha 24811 :91
server beta 24812 :92
sleep 1 # the core started last owns the IPC socket; it must be the client
settings gamma "$WORK/gamma.conf" :93 \
  "[client]" "remoteHost=127.0.0.1:24811" "extraServers=127.0.0.1:24812" "takeoverDelay=$DELAY_MS"
DISPLAY=:93 XDG_CONFIG_HOME="$WORK/gamma-cfg" XDG_STATE_HOME="$WORK/gamma-state" \
  QT_QPA_PLATFORM=offscreen "$CORE" client --new-instance -s "$WORK/gamma.conf" >"$WORK/gamma.log" 2>&1 &
PIDS+=($!)

waitfor "$WORK/alpha.log" 'client "gamma" has connected' 15
waitfor "$WORK/beta.log" 'client "gamma" has connected' 15
echo "ok: one client connected to both servers"

cursor() { DISPLAY=$1 xdotool getmouselocation | sed 's/x:\([0-9]*\) y:\([0-9]*\).*/\1 \2/'; }
# Xvfb only reports raw motion (what the server watches) for relative moves
cross() { # display: walk to the right edge and over
  DISPLAY=$1 xdotool mousemove 1200 400; sleep 0.2
  for _ in 1 2 3 4 5 6 7 8 9 10; do DISPLAY=$1 xdotool mousemove_relative -- 20 0; sleep 0.05; done
}

# alpha crosses first and gets control
cross :91
waitfor "$WORK/alpha.log" 'switch from "alpha" to "gamma"' 5
waitfor "$WORK/gamma.log" 'entering computer' 5
before=$(cursor :93)
for _ in 1 2 3 4 5; do DISPLAY=:91 xdotool mousemove_relative -- 20 10; sleep 0.1; done
sleep 0.3
after=$(cursor :93)
[ "$before" != "$after" ] || fail "alpha moved its cursor but the client's didn't move ($before)"
echo "ok: alpha controls the client ($before -> $after)"

# beta crosses straight after: turned away and takes its cursor back
cross :92
waitfor "$WORK/beta.log" 'switch from "beta" to "gamma"' 5
waitfor "$WORK/beta.log" 'controlled by another server, taking the cursor back' 5
waitfor "$WORK/beta.log" 'switch from "gamma" to "beta"' 5
echo "ok: beta was turned away while alpha is busy"

# keep alpha busy for a moment, then let it idle past the delay
DISPLAY=:91 xdotool mousemove_relative -- 5 5
sleep $(( (DELAY_MS + 999) / 1000 + 1 ))

lost=$(count "$WORK/alpha.log" 'taking the cursor back')
cross :92
waitfor "$WORK/gamma.log" 'server "127.0.0.1:24812" is now in control' 5
waitfor "$WORK/alpha.log" 'controlled by another server, taking the cursor back' 5
before=$(cursor :93)
for _ in 1 2 3 4 5; do DISPLAY=:92 xdotool mousemove_relative -- -20 -10; sleep 0.1; done
sleep 0.3
after=$(cursor :93)
[ "$before" != "$after" ] || fail "beta took over but the client's cursor didn't follow it ($before)"
echo "ok: beta took over once alpha was idle; alpha took its cursor back ($before -> $after)"

# alpha's input no longer reaches the client
before=$(cursor :93)
for _ in 1 2 3; do DISPLAY=:91 xdotool mousemove_relative -- 30 0; sleep 0.1; done
sleep 0.3
[ "$(cursor :93)" = "$before" ] || fail "alpha still moves the client's cursor after losing control"
echo "ok: alpha no longer reaches the client"

# a clipboard only lands while its server is in control
clip_set() { printf '%s' "$2" | DISPLAY=$1 timeout 20 xclip -selection clipboard -loops 0 & sleep 0.3; }
clip_get() { DISPLAY=$1 timeout 2 xclip -selection clipboard -o 2>/dev/null || true; }
for _ in 1 2 3 4 5 6 7 8; do DISPLAY=:92 xdotool mousemove_relative -- -200 0; sleep 0.05; done # beta goes home
waitfor "$WORK/beta.log" 'switch from "gamma" to "beta"' 5
clip_set :92 "copied on beta"
cross :92
sleep 1
[ "$(clip_get :93)" = "copied on beta" ] || fail "client clipboard is \"$(clip_get :93)\", expected beta's"
clip_set :91 "copied on alpha"
DISPLAY=:92 xdotool mousemove_relative -- 5 5 # beta busy
cross :91 # turned away: its clipboard waits
sleep 1
[ "$(clip_get :93)" = "copied on beta" ] || fail "alpha's clipboard replaced beta's while beta was in control"
sleep $(( (DELAY_MS + 999) / 1000 + 1 ))
cross :91 # beta idle: alpha takes over and its clipboard comes with it
sleep 1
[ "$(clip_get :93)" = "copied on alpha" ] || fail "client clipboard is \"$(clip_get :93)\" after alpha took over"
echo "ok: only the server in control sets the clipboard"

# files go to whichever server knows the target, through one shared transfer manager.
# the client started last, so the core IPC socket is its own.
send_file() { # target file
  python3 - "$1" "$2" <<'PY'
import base64, json, socket, sys, tempfile, os, time
target, path = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.join(tempfile.gettempdir(), "deskflow-core"))
s.sendall(b"hello=e2e\n")
time.sleep(0.3)
payload = base64.urlsafe_b64encode(json.dumps({"target": target, "paths": [path]}).encode()).decode().rstrip("=")
s.sendall(f"sendFiles={payload}\n".encode())
time.sleep(0.5)
s.close()
PY
}
echo "for alpha" > "$WORK/to-alpha.txt"
echo "for beta" > "$WORK/to-beta.txt"
send_file alpha "$WORK/to-alpha.txt"
send_file beta "$WORK/to-beta.txt"
for _ in $(seq 1 50); do
  [ -f "$WORK/alpha-dl/to-alpha.txt" ] && [ -f "$WORK/beta-dl/to-beta.txt" ] && break
  sleep 0.2
done
[ "$(cat "$WORK/alpha-dl/to-alpha.txt" 2>/dev/null)" = "for alpha" ] || fail "file for alpha did not arrive"
[ "$(cat "$WORK/beta-dl/to-beta.txt" 2>/dev/null)" = "for beta" ] || fail "file for beta did not arrive"
echo "ok: files reach both servers, each over its own connection"

echo "PASS"
