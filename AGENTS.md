# Working on this repository

This is a personal fork of Deskflow (keyboard, mouse and clipboard sharing). The owner
delegates engineering decisions to the agent working on it: prefer changes that make
future iteration easier, and keep this file current when you learn something a later
session would otherwise have to rediscover.

## Ground rules

- Work on the branch you were given and push there. Do not open pull requests or issues,
  or comment on them, unless the owner asks.
- Only this fork needs to interoperate with itself. The wire protocol may change freely;
  bump `kProtocolMinorVersion` and add `ClientProxy1_N` / `ServerProxy1_N` when it does.
- The owner's machines are macOS (Apple silicon and Intel) with a possible Windows PC.
  Those platforms cannot be built locally: push and let CI build them (see below).
- The owner tests packages by hand. Say plainly what was verified locally, what only
  compiled in CI, and what needs their eyes on a real Mac or PC.

## Layout

| Path | What lives there |
| --- | --- |
| `src/lib/deskflow` | Core shared by client and server: clipboard, protocol, file transfer, core IPC |
| `src/lib/client`, `src/lib/server` | The two ends; one proxy class per protocol version |
| `src/lib/platform` | Per-OS screens and clipboards (`OSX*`, `MSWindows*`, `XWindows*`, `Ei*`/`Portal*`) |
| `src/lib/gui` | Qt Widgets GUI; talks to the core process over a local socket |
| `src/unittests` | Qt Test suites, mirrored by library |
| `scripts/dev` | Dev box setup, end-to-end test and GUI screenshots |

The GUI (`deskflow`) and the core (`deskflow-core`) are separate processes. The core runs
its own event queue on a worker thread and serves IPC on the Qt main thread; IPC messages
are `command=args` lines, with structured arguments as base64 JSON (`common/IpcPayload.h`).

## Build and test locally (Linux)

```bash
scripts/dev/setup-linux.sh          # once, ~20 s: apt packages, Qt + clang-format 20.1.0, configures ./build-dev
ninja -C build-dev                  # build everything; also runs the unit tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev/src/unittests
scripts/dev/e2e-multi-server.sh     # two real servers + one client on three virtual displays
scripts/dev/screenshot.sh out.png dark [seed.conf]   # look at the GUI without a desktop
```

- The setup script gets Qt from conda-forge with micromamba (also from conda-forge:
  download.qt.io and micro.mamba.pm are blocked in the sandbox). It puts it and
  clang-format 20.1.0 under `~/.cache/deskflow-dev`; add `~/.cache/deskflow-dev/venv/bin`
  to `PATH` first, because the system clang-format is 18 and reformats code differently.
- `/scripts` is git-ignored except `scripts/dev`; the earlier dev scripts never reached
  the repo for that reason, and `e2e-clipboard-files.sh` was lost. `e2e-multi-server.sh`
  shows how to drive real cores: `--new-instance -s file.conf`, computers declared as
  `[computer_N] name=...`, `log/level=Debug` (names, not numbers), TLS off. Xvfb only
  reports raw motion (what an X11 server watches) for **relative** `xdotool` moves. The
  core started last owns the IPC socket `/tmp/deskflow-core`.
- `screenshot.sh` takes an optional settings file to seed; `SHOT_KEYS="alt+c"
  SHOT_DIALOG="Client Configuration"` opens and captures a dialog.
- Set `DESKFLOW_BUILD_DIR` if the build is somewhere other than `./build-dev`.
- In a sandbox, `ArchNetworkTests` (no IPv6 sockets) and `XWindowsClipboardTests`
  (no X display) fail for environmental reasons; everything else must pass.
- The local build is X11 only (`-DBUILD_LIBEI_SUPPORT=OFF`). CI's Debian job builds the
  libei/portal backend, so keep it compiling even though it can't be run here.
- `DESKFLOW_COLOR_SCHEME=light|dark` forces the GUI's colour scheme.

## Before pushing

1. Format what you touched: `clang-format -i <files>` with clang-format **20.1.0**
   (the setup script installs it; CI's lint job fails on any diff).
2. Build once so `lupdate` refreshes `translations/*.ts`, and commit those changes: CI
   fails if a build leaves the tree dirty. Fill in `deskflow_zh_CN.ts` for new strings;
   the owner reads Chinese.
3. CI compiles with warnings as errors. Qt's `[[nodiscard]]` (e.g. `QFile::open`) is the
   usual casualty.
4. Any header that includes Xlib must come after Qt headers (Xlib defines `Status`,
   `Bool`, `None`).

## CI

`.github/workflows/continuous-integration.yml` runs on every push to `master` and
`claude/**`: lint, then macOS arm64, macOS x64, Windows x64 and Debian testing. Each job
uploads its package (dmg / msi + portable zip / deb) as a workflow artifact; tags `v*`
publish a release. A full run takes about 15 minutes; pushing again cancels the run in
progress.

Check a run without flooding the context:

```bash
gh api "repos/OWNER/REPO/actions/runs?per_page=1&branch=BRANCH" --jq '.workflow_runs[0].id'
gh api repos/OWNER/REPO/actions/runs/RUN/jobs --jq '.jobs[] | [.id,.name,.conclusion] | @tsv'
gh api repos/OWNER/REPO/actions/jobs/JOB/logs | grep -E "error|FAILED" | head
```

macOS packages are signed with the certificate in the `MACOS_SIGNING_P12` /
`MACOS_SIGNING_P12_PWD` secrets when they exist (a self-signed one from
`scripts/dev/make-signing-cert.sh` is enough), otherwise ad-hoc. Only a stable certificate
keeps the designated requirement, and with it the user's privacy permissions, the same from
one build to the next; ad-hoc builds lose them on every update. Ad-hoc and self-signed builds must not use the hardened runtime: its library validation makes dyld
reject the bundled Qt frameworks ("different Team IDs"). Runners have SIP off and never
show this, so `test-package` checks the signature flags and each binary's minimum macOS
against `LSMinimumSystemVersion` instead of trusting a successful launch.

## Feature notes

**macOS permissions** (`gui/permissions`, `pages/PermissionsPage`): Accessibility (required;
the core the GUI launches inherits it), Input Monitoring and Screen Recording. `PermissionMonitor`
polls them every second while the app is active and remembers, per permission, a hash of the
designated requirement that last held it (`permissions/*Signature`). Missing with a different
remembered signature means the entry in System Settings is stale (still switched on, no longer
counting); `allow()` then runs `tccutil reset <service> <bundle id>`, prompts and opens the
right pane. The main window opens on the Permissions page when Accessibility is missing or
anything is stale, and `startCore()` waits there until Accessibility arrives. Screen Recording
only applies to a new process, so the page offers a relaunch (`open -n` after this pid exits).
`DESKFLOW_FAKE_PERMISSIONS="accessibility=stale,input=denied,screen=restart"` fakes states on
Linux for `screenshot.sh`.

**macOS pasteboard ownership**: `OSXClipboard::synchronize()` reports a change only when
another app owns the pasteboard. Treating our own writes as a local copy cancels copied
files that are still being fetched, leaving only their names to paste.

**Several servers** (`client/ClientHub`, `client/InputArbiter`, protocol 1.10): a client
can stay connected to `client/remoteHost` plus every `client/extraServers` entry at once.
`ClientApp` keeps one session (retries, failover list) per server; each `Client` talks
to one shared `ClientHub`, which owns what belongs to the computer: enabling it, its
events (fanned out to the clients), one non-router `FileTransferManager` routed by peer
name, and one `ClipboardFileSync`. `InputArbiter` decides who is in control: an enter
claims, a leave or disconnect releases, and another server takes over once the owner
has been idle for `client/takeoverDelay` ms (0: at once, -1: never). A server that is
turned away or taken over gets `CDNY` and its `Server::handleControlDenied` brings the
cursor back just inside the edge it left by. Clipboards from a server not in control
are held until it takes over; `client/shareClipboardBetweenServers` forwards one
server's clipboard to the others (copied files only travel within the network they
came from). The GUI shows each server's state from the core's `servers` IPC message.
`Client::getEventTarget()` is the client itself, not the computer.

**Clipboard formats** (`IClipboard::Format`): `Text`, `HTML`, `Bitmap` (legacy DIB),
`Png`, `Files`. Images travel as PNG; `marshall()` converts a lone `Bitmap` and never
sends both. Each platform clipboard maps `Png` to its native types through converter
classes. Adding a format means: the enum, a converter per platform, and a unit test.

**File transfer** (`FileTransferManager`, protocol 1.9): a stream of records cut into
64 KiB data messages on the normal connection, acknowledged with a 16 message window so
memory stays flat and input stays responsive. The server relays between clients.
Received files are staged in a hidden folder and moved into the download folder at the
end. `FileTransferTests` wires three managers together in memory: extend it for any
protocol change.

**Copied files** (`ClipboardFileSync`): on a platform clipboard `Files` is a list of
local paths; on the wire it is a JSON offer (`origin`, `token`, `total`, ...). The
receiver applies the rest of the clipboard immediately, requests the files, and applies
the clipboard again with cached local copies when they arrive. Sets larger than
`core/clipboardFilesLimit` (MiB, default 500) are not fetched; the GUI's send panel is
the way to move big things.

**GUI theme** (`gui/Theme.cpp`): Fusion style with one palette and style sheet for all
platforms, following macOS conventions and the owner's DayOfFlow app (warm neutrals,
iris accent `#5A55D6`/`#8B87F5`, capsule buttons, 16 px cards). Widgets opt into roles
with dynamic properties (`card`, `primary`, `nav`, `segment`, `eyebrow`, ...), documented
in `Theme.h`. Don't give widgets their own style sheets; add a role instead. Custom
painted controls take colours from `theme::accent()` etc. or the palette.

**GUI layout**: `MainWindowUi` builds the main window in code (no `.ui` file): a sidebar
and a `QStackedWidget` of pages (This Computer, Clipboard, Screenshot, Remote Control, Screen Hints, Permissions (macOS only),
Log, Settings; Ctrl/Cmd+1..8). Reusable controls live in `widgets/MacControls.h` (`ToggleSwitch`,
`SegmentedControl`, `FormCard` rows, `ChoiceCard`, `Page`, line `symbol()`s drawn with
QPainter, since the GUI doesn't link QtSvg). Pages under `pages/` derive from
`RebuildablePage`: they read Settings each time they are shown or the language changes,
and write every change straight back; there is no Save button. Keys the running core
only reads at start-up are listed in `SettingsPage::needsCoreRestart()` and raise a
"restart to apply" banner instead of restarting the core silently. The server layout and
client options are still dialogs (`ServerConfigDialog`, `ClientConfigDialog`).
`screenshot.sh` accepts `SHOT_CLICKS="x,y ..."` to click into a page before capturing.

**Remote view** (`remoteview/`, protocol 1.11): the server shows a client's screen in a
window and drives it from there. The server sends `DRVS` (JSON settings: display,
quality, fps, codecs); the client's `RemoteViewSender` captures (`ScreenCapturer*`:
ScreenCaptureKit, GDI, X11/XShm; Wayland unsupported), encodes with `StreamEncoder` and
answers with `DRVF` frame pieces (<= 192 KiB) and `DRVI` state JSON. Frames are one
H.264 access unit (VideoToolbox, Media Foundation, FFmpeg when `libavcodec` is found at
build time) or JPEG dirty tiles later sharpened as PNG; `RemoteFrame.h` holds the wire
format and `RemoteFrameTests` covers it. At most two frames are unacked; the GUI acks
each one (`remoteAck` IPC -> `DRVA`), so a slow viewer slows the sender. The GUI viewer
(`gui/remote/`) sends input as `remoteInput` IPC; `server/ServerRemoteView.cpp` turns it
into enter/key/mouse calls on that client's proxy with its own sequence number, so the
client's `InputArbiter` treats it like any other server, and `CDNY` shows "controlled".
Clients opt out with `remoteView/allowViewing`. Clipboards follow the viewer like the cursor: the viewer's enter reads our own clipboard first (normally read only when the cursor leaves) and sends it, a copy here while the viewer drives is pushed at once, and the client sends its clipboard back on the viewer's leave (focus out). A press that takes over is held until the queued clipboard chunks have gone out (`RemoteView::holding`), so a paste typed first works. The IPC frame lines are big: don't log
them.
