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

**Phase 1 — SimpleGraphic arm64 build** (issues #1–#5 on this repo)

Check the [GitHub Issues](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues?q=label%3Amacos-port+is%3Aopen)
for what is currently open. Look at `MACOS_PORT.md` for the full plan with decisions log.

## Key architectural decisions (do not revisit without discussion)

| Decision | Choice | Reason |
|---|---|---|
| LuaJIT on arm64 | Interpreter-only (v2.1 branch via vcpkg) | No arm64 JIT backend in stable; acceptable perf for PoB's workload |
| Graphics API | ANGLE (Metal backend on macOS) | Matches Windows path; avoids rewriting renderer |
| Min deployment target | macOS 13.0 (Ventura) | Covers all M-series hardware in active use |
| vcpkg triplet | `arm64-osx` in `triplets/arm64-osx.cmake` | Registered as overlay in `vcpkg-configuration.json` |

## Build command (macOS)

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=arm64-osx \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build --config Release
```

## Implementation specs

Detailed per-issue execution specs live in `docs/macos/`. Each spec contains the
exact files to create/modify, the exact content, key decisions, and a verification
checklist. Always check for a spec before starting work on an issue.

| Issue | Spec |
|---|---|
| #6 runtime-macos layout | `docs/macos/issue-6-runtime-layout.md` |

## Files to know

| File | Purpose |
|---|---|
| `engine/system/win/sys_main.cpp` | Platform entry, user data dir, thread/timer |
| `engine/system/win/sys_macos.mm` | macOS-specific Obj-C++ implementations |
| `engine/system/win/sys_video.cpp` | GLFW window + OpenGL context creation |
| `win/entry.cpp` | Windows `WinMain` — macOS needs `mac/entry.cpp` equivalent |
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
