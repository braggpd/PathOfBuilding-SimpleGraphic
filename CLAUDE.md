# PathOfBuilding-SimpleGraphic — Claude Code Context

This is a fork of `PathOfBuildingCommunity/PathOfBuilding-SimpleGraphic`, the custom
C++ engine that powers Path of Building 2. The fork's purpose is a **native macOS
Apple Silicon (arm64) port**.

## What this repo is

SimpleGraphic is a shared library (.dll / .dylib) that provides:
- Window management (GLFW)
- 2D renderer (OpenGL ES via ANGLE; Metal backend on macOS)
- LuaJIT 5.1 runtime host
- Lua module loader for `lcurl`, `lzip`, `socket`, `lua-utf8`

The game logic (~99% Lua) lives in a separate repo (`PathOfBuilding-PoE2`). That layer
needs **zero macOS-specific changes** — all platform work is here.

## Active branch: `macos-port`

All macOS work happens on the `macos-port` branch. Never commit macOS-specific changes
to `master` — that stays in sync with upstream via `git rebase upstream/master`.

## Current phase

**Phase 1 — SimpleGraphic arm64 build** (issue #5 open; **#1–#4 complete**)

Check the [GitHub Issues](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues?q=label%3Amacos-port+is%3Aopen)
for what is currently open. Look at `MACOS_PORT.md` for the full plan with decisions log.

**Next up:** issue #5 — full local smoke build (`libSimpleGraphic.dylib`).

## Key architectural decisions (do not revisit without discussion)

| Decision | Choice | Reason |
|---|---|---|
| LuaJIT on arm64 | v2.1 beta via vcpkg overlay port; **JIT enabled** on arm64 (verified 2026-05-22) | Custom `luajit` port builds for `arm64-osx`; acceptable perf for PoB |
| Graphics API | ANGLE (Metal backend on macOS) | Matches Windows path; avoids rewriting renderer |
| Min deployment target | macOS 13.0 (Ventura) | Covers all M-series hardware in active use |
| vcpkg triplet | `arm64-osx` in `triplets/arm64-osx.cmake` | Overlay in `vcpkg-configuration.json`; adds libc++ `-isystem` for `-isysroot` builds |
| ANGLE on macOS | `angle` port with **`metal` feature** in `vcpkg.json` | Produces `liblibEGL_angle.dylib` / `liblibGLESv2_angle.dylib` |

## Build command (macOS)

**Path constraint:** the repository path must not contain spaces — LuaJIT's vcpkg
`make` build splits `PREFIX` at whitespace and fails. Relocate the clone or use a
worktree (e.g. `~/PoB-SimpleGraphic-build`) for vcpkg/cmake.

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release
```

**LuaJIT-only verify** (Phase 1.2 — classic manifest install of one port):

```bash
./vcpkg/bootstrap-vcpkg.sh
./vcpkg/vcpkg install luajit --triplet arm64-osx --classic \
  --overlay-ports=vcpkg-ports/ports --overlay-triplets=triplets
export DYLD_LIBRARY_PATH="$(pwd)/vcpkg/installed/arm64-osx/lib"
./vcpkg/installed/arm64-osx/tools/luajit/luajit -e 'print("ok", jit and jit.arch)'
```

## Files to know

| File | Purpose |
|---|---|
| `engine/system/win/sys_main.cpp` | Platform entry, user data dir, thread/timer |
| `engine/system/win/sys_macos.mm` | macOS-specific Obj-C++ implementations |
| `engine/system/win/sys_video.cpp` | GLFW window + OpenGL context creation |
| `win/entry.cpp` | Windows DLL export `RunLuaFileAsWin` |
| `mac/entry.cpp` | macOS `RunLuaFileAsWin` + `main()` (standalone dev launch) |
| `triplets/arm64-osx.cmake` | vcpkg arm64-osx triplet definition |
| `vcpkg-ports/ports/luajit/` | Custom LuaJIT port with macOS patches |
| `MACOS_PORT.md` | Full plan, task checklist, decisions log |

## Conventions

- One branch per issue: `macos/issue-N-short-description`
- PR titles: `[macOS] short description (closes #N)`
- When a task is complete: close the issue and tick the checkbox in `MACOS_PORT.md`
- Append to the **Notes & Decisions Log** in `MACOS_PORT.md` for any non-obvious choice
- Do not modify `CMakeLists.txt` Windows-only paths — add `if(APPLE)` blocks alongside them

## Running tests

```bash
# Lua logic tests (Docker, cross-platform — use PathOfBuilding-PoE2 repo)
docker-compose up

# macOS smoke test (once Phase 2 is complete)
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
```

## Upstream sync

```bash
git fetch upstream
git rebase upstream/master   # on master branch only
# then rebase macos-port onto updated master
git checkout macos-port
git rebase master
```
