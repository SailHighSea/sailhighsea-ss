# SailHighSea SimpleSync

One-way folder sync tool in C11 + raylib for Windows, macOS and Linux.

## Layout
- `src/main.c` - window, custom title bar, pages, add/edit dialog, glue between UI and engine
- `src/ui/` - theme colours and the scrolling task-card widget (raylib drawing only)
- `src/engine/` - `sync_engine` (background thread, scan/copy/mirror, no raylib) and `task_store` (task list file)
- `src/platform/` - folder picker and free-disk helpers (Win32 / POSIX)
- `tests/engine_test.c` - headless engine + store tests (`ctest`)
- `scripts/` - `build.ps1` (Windows dev build), `next-version.sh` (CI release tag), `make-icons.ps1` (regenerates `assets/icon.png`, `assets/icon.ico` and `src/ui/icon_data.h` from the icon geometry; keep it in sync with `assets/icon.svg`)
- `assets/` - icon files; `icon.ico` is embedded in the Windows exe via `app.rc.in`, `icon.png` (1024 px) becomes `icon.icns` in the macOS CI job
- `.github/workflows/build-release.yml` - build matrix + releases (main = stable, dev = pre-release)

## Build (Windows)
`.\scripts\build.ps1 -Test` builds with MSYS2 UCRT64 gcc + Ninja and runs tests.
Each local build is numbered and copied to `test\SailHighSea-SimpleSync-v0.0.1-dev-N.exe` (newest 3 kept); the CMake tree lives in `test\_build`. Plain CMake builds (Linux/macOS/CI) write `test/SailHighSea-SimpleSync`.
Dev builds are Release (no console window); `build.ps1 -Console` makes a Debug build with raylib's log console. Dev flags: `--dev` (test sandbox), `--quick <src> <dst>` starts a Quick Copy, `--quick-add <path>` adds a path to the newest Quick Copy (same as dropping it on the card).

## Rules
- Only ever sync against `test\source_folder` / `test\target_folder` while developing (`--dev` flag or Settings > "Create / reset test sandbox"). Never point a test at real user data.
- Do not include `windows.h` in a file that includes `raylib.h` (name clashes). Win32 code lives in `engine/` and `platform/`.
- Win32 headers define `OpenFile` and `ReplaceFile`; engine helpers are named `Fs*`.
- Keep engine code free of raylib so `engine_test` stays headless.
- `--multi` lets a second copy run (measuring / screenshots) and keeps it from touching the start-up registry entry. Test scratch copies must use it, otherwise the single-instance check stops them and the start-up repair repoints the Run key at the scratch exe.
- external/raylib/src/config.h is also locally changed (SailHighSea patch): `SUPPORT_PARTIALBUSY_WAIT_LOOP`, `SUPPORT_SCREEN_CAPTURE` and `SUPPORT_GIF_RECORDING` are off (the busy wait burned ~5% of a core when idle). Re-apply if raylib is updated.
- external/raylib is raylib 5.5 with one local patch in src/platforms/rcore_desktop_glfw.c (marked SailHighSea patch): on Windows with FLAG_WINDOW_HIGHDPI the resize callback and SetWindowMin/MaxSize now convert between physical pixels and the scaled units the UI draws in. Without it, maximizing on a scaled display lays the UI out too wide. Re-apply it if raylib is updated.
