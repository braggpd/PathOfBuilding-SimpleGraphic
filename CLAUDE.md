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
needs **minimal macOS-specific changes** for launch/bootstrap only — all platform work
lives here.

## Active branch: `macos-port`

All macOS work happens on the `macos-port` branch. Never commit macOS-specific changes
to `master` — that stays in sync with upstream via `git rebase upstream/master`.

## Current phase

**Phase 2 — runtime integration** (active). **Phase 1 #1–#5 complete** (2026-05-22).

Check [open macOS issues](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues?q=label%3Amacos-port+is%3Aopen)
and **`MACOS_PORT.md` → “Next session — close #8”** for the handoff plan.

### Status snapshot (2026-05-24, evening)

| Area | State |
|------|--------|
| Engine `PLoadModule` / return capture | **Done** — stash + Lua wrappers (`ui_api.cpp`, `ui_main.cpp`) |
| JIT on arm64 | **C API** `luaJIT_setmode` off + stub `jit.opt.start` (`mac_jit_off`) |
| `loadfile` on macOS | **`__mac_loadfile_stash_c`** + `MacLoadfile` wrapper — never `local f, err = loadfile(...)` |
| Post-Main init | **`mac_run_after_main_if_requested`** runs `LaunchAfterMain.lua` from C after `OnInit` |
| PoB launch split | **Implemented locally** — see `docs/macos/pob-launch/` (copy to PoB `src/`) |
| `Launch.lua` dev test | **`PLoadModule main type=table`** in ~30–90 s with split + minimal `OnInit` |
| **#8 close criteria** | UI renders, tree loads, calcs run — **`main.Init` + frame loop still to verify** |

### Next steps

1. **Push / merge** engine changes on `macos/issue-8-*` → `macos-port` (this repo).
2. **PoB repo:** copy `docs/macos/pob-launch/*.lua` into `src/`; PR for [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9).
3. **Rebuild** → install → run `Launch.lua`; wait for `PLoadModule main type=table`, then **minutes** for `main.Init`.
4. **Fix** any failure in `LaunchAfterMain.lua` / `LaunchCallbacks.lua` only — **do not bisect `Modules/Main.lua` first**.
5. **Document** shutdown SIGBUS if it persists after successful Init.

## Key architectural decisions (do not revisit without discussion)

| Decision | Choice | Reason |
|---|---|---|
| LuaJIT on arm64 | v2.1 beta; `luaJIT_setmode` engine off + stub `jit.opt.start` | JIT + GC64 breaks C return capture |
| C API returns on macOS | Stash in globals; Lua wrappers re-return | Bytecode cannot `local x = GetTime()` from C |
| `PLoadModule` | `__mac_pload_c` + Lua wrapper + `{ err, main }` | Matches PoB `mainPlm.err` / `mainPlm.main` |
| PoB launch structure | **3 files** — `Launch.lua`, `LaunchAfterMain.lua`, `LaunchCallbacks.lua` | Monolithic chunk + fat `OnInit` poison VM before Main |
| Version placeholders | **`"?"` only after `PLoadModule`** | Assigning `version* = "?"` before Main crashes |
| AfterMain load | **From C**, not `__mac_dofile_c` in `Launch.lua` | Same-chunk `dofile` breaks `PLoadModule` at compile time |
| `require("xml")` | **After** Main | xml before Main breaks load on macOS |
| Graphics API | ANGLE (Metal on macOS) | Matches Windows path |

## Build command (macOS)

**Path constraint:** repository path must not contain spaces (LuaJIT `make` splits `PREFIX`).

```bash
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos
```

**PoB dev launch** (from `~/PoB-PoE2-build`):

```bash
ln -sfn "$(pwd)/runtime/SimpleGraphic" runtime-macos/SimpleGraphic
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua  # regression
```

Use space-free worktrees: `~/PoB-SimpleGraphic-build`, `~/PoB-PoE2-build`.

## Implementation specs

| Issue | Spec |
|---|---|
| #6 runtime-macos layout | `docs/macos/issue-6-runtime-layout.md` |
| #8 dev launch handoff | `docs/macos/issue-8-launch-handoff.md` |
| PoB launch reference | `docs/macos/pob-launch/` |

## Files to know

| File | Purpose |
|---|---|
| `ui_main.cpp` | `mac_jit_off`, LIGHTFUNC builtins, stash wrappers, **`mac_run_after_main_if_requested`** |
| `ui_api.cpp` | `PLoadModule` / `LoadModule`, `mac_push_plm_result` |
| `docs/macos/pob-launch/` | Reference `Launch*.lua` for PoB repo |
| `MACOS_PORT.md` | Full plan + handoff sections A–F |
| `.cursor/rules/macos-port.mdc` | Cursor always-on port context |

## Conventions

- One branch per issue: `macos/issue-N-short-description`
- PR titles: `[macOS] short description (closes #N)`
- Tick `MACOS_PORT.md` checkboxes and append **Notes & Decisions Log** when done
- Do not modify Windows-only paths — add `if(APPLE)` alongside

## Running tests

```bash
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua   # wait 50–90s+ for Main load line
```

## Upstream sync

```bash
git fetch upstream
git rebase upstream/master   # on master only
git checkout macos-port && git rebase master
```
