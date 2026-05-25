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

### Status snapshot (2026-05-25)

| Area | State |
|------|--------|
| **#8 dev launch** | **DONE** — UI renders, passive tree loads, mouse tracks, text displays |
| Engine rendering | VAO + VBO for GL ES 3.0; text culling DPI fix; `glEnable(GL_TEXTURE_2D)` disabled |
| DPI / Retina | Cursor scaling (`vid.dpiScale`); font culling uses `VirtualScreenHeight()` |
| `PCall` | `lua_pcall` (not `lua_resume`) — nested resume dropped draw commands |
| `bit.*` module | Original LuaJIT builtins kept — handle `int64_t` cdata for `sha2.lua` |
| Subscript threads | `package.path` includes `runtime/lua/` for `require("xml")` etc. |
| Update check | Disabled on macOS (`launch._isMacOS`); future task for `lcurl.safe` |
| Version display | Parsed from `manifest.xml` after `main.Init` |

### Next steps

1. **Commit & push** engine changes on `macos/issue-8-sync-smoke-merge`.
2. **Close [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8)** — dev launch success criteria met.
3. **PoB repo:** copy `docs/macos/pob-launch/*.lua` into `src/`; PR for [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9).
4. **Phase 3** — `.app` bundle, code signing, DMG packaging.
5. **Future:** `lcurl.safe` for update check; shutdown SIGBUS fix.
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
| pcall/xpcall/require | **`lua_pcall` directly** — not `lua_resume` | `lua_resume` hangs inside `lua_pcall`-protected frames on arm64 GC64 |
| `mac_lightfunc_pcall` scope | **Top-level callbacks only** (`OnInit`, `OnFrame`) | Nested calls must use `lua_pcall` to avoid resume-inside-pcall |

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
