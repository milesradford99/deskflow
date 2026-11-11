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
| `scripts/dev` | Dev box setup, end-to-end tests and GUI screenshots |

The GUI (`deskflow`) and the core (`deskflow-core`) are separate processes. The core runs
its own event queue on a worker thread and serves IPC on the Qt main thread; IPC messages
are `command=args` lines, with structured arguments as base64 JSON (`common/IpcPayload.h`).

## Build and test locally (Linux)

```bash
scripts/dev/setup-linux.sh          # once: apt packages, Qt from conda-forge, configures ./build-dev
ninja -C build-dev                  # build everything; also runs the unit tests
QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev/src/unittests
scripts/dev/e2e-clipboard-files.sh  # real server + client on two virtual displays
scripts/dev/screenshot.sh out.png dark   # look at the GUI without a desktop
```

- `scripts/dev` is referenced above but is **not in the tree** on this branch; restore or
  rewrite it before relying on those commands. Without it, the two tools CI insists on
  can be had quickly:
  `python3 -m venv /tmp/cf && /tmp/cf/bin/pip install clang-format==20.1.0`, and
  `apt-get install qt6-l10n-tools` for `/usr/lib/qt6/bin/lupdate`, run the way
  `translations/CMakeLists.txt` does:
  `lupdate -no-ui-lines -locations none -silent src -ts translations/deskflow_<lang>.ts`
  (`-pluralonly` for `deskflow_en.ts`). The system clang-format is 18 and reformats code.
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

macOS packages are ad-hoc signed (no Apple developer certificate is configured), so
each new build has to be allowed again under Privacy & Security > Accessibility.
Ad-hoc builds must not use the hardened runtime: its library validation makes dyld
reject the bundled Qt frameworks ("different Team IDs"). Runners have SIP off and never
show this, so `test-package` checks the signature flags and each binary's minimum macOS
against `LSMinimumSystemVersion` instead of trusting a successful launch.

## Feature notes

**macOS permissions**: the GUI must hold Accessibility (the core it launches inherits
it). `checkMacAssistiveDevices()` clears a stale entry with `tccutil reset` (each ad-hoc
build has a new signature, so an old switched-on entry silently stops counting), shows
Apple's prompt, then waits in a dialog until `AXIsProcessTrusted()` turns true.

**macOS pasteboard ownership**: `OSXClipboard::synchronize()` reports a change only when
another app owns the pasteboard. Treating our own writes as a local copy cancels copied
files that are still being fetched, leaving only their names to paste.

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
platforms, Fluent 2 flavoured, light and dark. Widgets opt into roles with dynamic
properties (`card`, `primary`, `secondary`, `segment`, ...), documented in `Theme.h`.
Don't give widgets their own style sheets; add a role instead.
