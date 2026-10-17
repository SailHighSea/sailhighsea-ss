<p align="center">
  <img src="assets/icon.png" alt="SailHighSea SimpleSync" width="140">
</p>

<h1 align="center">SailHighSea SimpleSync</h1>

<p align="center">
  A tiny, open-source folder copy and sync tool for Windows, macOS and Linux.<br>
  One small program, no installer, no runtime, portable.
</p>

<p align="center">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-green">
  <img alt="platform" src="https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-blue">
  <img alt="written in" src="https://img.shields.io/badge/written%20in-C-555555">
</p>

<p align="center">
  <img src="assets/screenshot.png" alt="SailHighSea SimpleSync" width="900">
</p>

## What is SailHighSea SimpleSync?

SimpleSync keeps folders in step and moves files fast, without the weight of a backup suite. It does two jobs:

- **Sync tasks**: pick a source and a target folder and new or changed files are copied across. Run a task when you choose, or turn on **auto-sync** and it watches the source folder and syncs after changes.
- **Quick Copy**: a one-off copy that is never saved, with a copy-manager style panel. Pause, skip, stop, verify, and drop more files onto it while it runs.

It is written in C with [raylib](https://www.raylib.com/), so the whole program is a single file. It uses almost no CPU or memory while idle, which makes it comfortable to leave running in the system tray.

> **Status:** early development (v0.0.x). Expect rough edges and please try it on test copies of your data first. The Linux and macOS builds have had much less testing than the Windows one. See [Safety](#safety).

## Features

**Sync tasks**
- Background engine, so the window never freezes; pause, resume or cancel at any time
- Copies a file when it is missing, its size differs, or its modified time differs by more than 2 s (FAT-friendly)
- Auto-sync: watch the source folder and sync automatically after changes stop
- Optional **mirror** mode that also removes files from the target that are gone from the source
- Task cards with progress, speed, remaining time, a file list, and edit / open folder / remove buttons

**Quick Copy**
- Copies a folder (or files you drop on the card) into a target; the folder itself lands inside the target
- Drop files or folders on the expanded card to add them to a running copy; they show up in the list immediately
- Tabs for the file list (with filter and auto-scroll), target, options, status with a speed graph, and a log
- **Overwrite dialog** when a file already exists: overwrite, skip, keep both, or a rule for all files (older, smaller, different size, rename ...)
- **Verify** copied files by reading them back and comparing a 64-bit hash

**Speed**
- Several files are copied in parallel, and small files are read ahead in order to cut drive seeks on hard disks
- Large files are read ahead while the previous block is written, so a copy runs at the speed of the slower drive
- USB sticks and other removable drives automatically use one writer and write files directly
- Copy threads and read-ahead buffer are adjustable in Settings

**App**
- Light, dark or system theme with the system font and a rounded, modern window
- Windows: system tray icon, start with Windows, start hidden, close to tray
- Only one copy runs; starting it again brings the running window forward
- A debug log (`simplesync-debug.log`) next to the program records scans, conflicts and OS error codes

## Download

Get the latest build from the [Releases](../../releases) page:

| File | For |
| --- | --- |
| `SailHighSea-SimpleSync-windows-x64.zip` | Windows 10 / 11, 64-bit |
| `SailHighSea-SimpleSync-windows-arm64.zip` | Windows 11 on ARM (native, experimental; the x64 build also runs there under emulation) |
| `SailHighSea-SimpleSync-linux-x64.tar.gz` | Linux, 64-bit (desktop with OpenGL) |
| `SailHighSea-SimpleSync-macos-arm64.zip` | macOS on Apple silicon |

Unzip it anywhere and run it; settings and the task list are saved next to the program. Builds are unsigned, so Windows SmartScreen and macOS Gatekeeper will warn on first run. A `SHA256SUMS.txt` is attached to every release.

Releases named like `v0.0.1-dev-1` are pre-releases from the `dev` branch; plain `v0.0.1` is stable.

## Safety

- **Mirror deletes files in the target permanently** (no recycle bin). Leave it off unless you want an exact copy.
- Source and target may not contain each other.
- Symbolic links and junctions are skipped; `System Volume Information` and `$RECYCLE.BIN` are never copied.
- Copies are written to `*.shs-tmp` and then renamed, so a cancelled copy never leaves a half-written file (on removable drives files are written directly instead).
- To experiment, use **Settings > Create / reset test sandbox** or start the app with `--dev`. It creates `source_folder` and `target_folder` with mock files next to the program.

## Build

Needs CMake 3.16+ and a C11 compiler. raylib 5.5 is bundled in `external/raylib`, so nothing is downloaded, and the Windows program is a single static `.exe`.

**Windows (MSYS2 UCRT64)**

```powershell
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
.\scripts\build.ps1 -Test      # build + run tests
.\scripts\build.ps1 -Run       # build + launch with the test sandbox
.\scripts\build.ps1 -Console   # Debug build that also opens a console with raylib's log
```

Each build is numbered and written to `test\SailHighSea-SimpleSync-v0.0.1-dev-N.exe` (newest 3 kept).

**Linux / macOS**

```bash
# Linux: sudo apt install build-essential cmake libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./test/SailHighSea-SimpleSync
```

**Command line flags**

| Flag | Effect |
| --- | --- |
| `--dev` | create and add the test sandbox task |
| `--minimized` | start hidden in the tray (Windows) |
| `--quick <source> <target>` | start a Quick Copy |
| `--quick-add <path>` | add a path to the newest Quick Copy |
| `--multi` | allow a second copy of the app to run (testing) |

## Releases

GitHub Actions builds Windows (x64 and ARM64), Linux and macOS on every push and publishes the files to Releases.

| Branch | Result |
| --- | --- |
| `dev` | pre-release `v0.0.1-dev-1`, `v0.0.1-dev-2`, ... (the last number counts dev builds of the next stable version) |
| `main` | stable release `v0.0.1`, `v0.0.2`, ... |
| pull request / manual run | build and tests only; files are kept as workflow artifacts |

Typical flow: push work to `dev`, test the pre-release, then merge `dev` into `main` for the stable release. The `v0.0` part of the version is set in `scripts/next-version.sh`.

## Project layout

```
src/main.c        window, pages, dialogs
src/ui/           theme, icons, task card list
src/engine/       sync / copy engine + task store (no raylib)
src/platform/     folder picker, tray, watcher, autostart, free disk
tests/            headless engine tests (ctest)
scripts/          build.ps1, make-icons.ps1, next-version.sh
assets/           icon files and the README screenshot
external/raylib   raylib 5.5 with small local patches (see CLAUDE.md)
```

## License

MIT, see [LICENSE](LICENSE). raylib is zlib/libpng licensed, see `external/raylib/LICENSE`.
