# Cursor handoff — macOS arm64 pcall/require rewrite + Data/Global hang (2026-05-24 late)

**Branch:** `macos/issue-8-sync-smoke-merge`  
**Repo:** `braggpd/PathOfBuilding-SimpleGraphic`  
**Full context:** `MACOS_PORT.md`, `CLAUDE.md`, `docs/macos/issue-8-launch-handoff.md`

---

## What you are doing and why

We are porting PathOfBuilding-SimpleGraphic (the LuaJIT host engine) to macOS Apple Silicon
(arm64). The engine embeds LuaJIT 2.1 with GC64 mode. This session discovered and fixed a
critical architectural issue: **`lua_resume` cannot be called from inside a `lua_pcall`-protected
frame** on arm64 GC64 — doing so hangs the interpreter.

---

## What has been done this session

### Critical architectural change: pcall/xpcall/require → lua_pcall

Previously, `pcall`, `xpcall`, and `require` were all implemented using `mac_lightfunc_pcall`
which uses `lua_resume` on a helper coroutine. This worked when they were the outermost
protected call, but **hung** when called from inside any `lua_pcall`-protected frame (e.g.
inside PLoadModule, or nested require → pcall chains like `sha1/init.lua → pcall(require, "bit")`).

**Fix applied to these functions in `ui_main.cpp`:**

| Function | Old impl | New impl |
|----------|----------|----------|
| `l_mac_pcall` | `mac_lightfunc_pcall` (lua_resume) | `lua_pcall` directly |
| `l_mac_xpcall` | `lua_newthread` + `lua_resume` | `lua_pcall` with error handler at slot 1 |
| `l_mac_prerequire` | `mac_lightfunc_pcall` | `lua_pcall` directly |
| `l_mac_require` (Lua file loader) | `mac_lightfunc_pcall` / conditional | `lua_pcall` directly |
| `l_mac_require` (C module loader) | `mac_lightfunc_pcall` | `lua_pcall` directly |

**Fix applied to `ui_api.cpp`:**

| Function | Old impl | New impl |
|----------|----------|----------|
| `mac_run_pload_module_impl` | `mac_lightfunc_pcall` | `lua_pcall` with traceback |
| `l_LoadModule` | `lua_call` (unprotected) | `lua_pcall` + `luaL_error` |

### Other additions

- `mac_sync_globals_from_helper_co` — copies `main`/`launch` globals from the helper coroutine registry ref to root state
- `mac_ensure_global_launch` — syncs `launch` from `uicallbacks.MainObject` to `_G.launch`
- `mac_sync_main_object_from_co` — copies `launch` from coroutine to `uicallbacks.MainObject`
- `mac_stash_loadfile` + `mac_run_stashed_chunk` — load/run Lua files from C without bytecode issues
- `mac_run_after_main_if_requested` now: loads `LaunchCallbacks.lua` → runs `PLoadModule("Modules/Main")` from C → syncs globals → runs `launch._finishAfterMain` → calls `main.Init`

### What passes now

- All `require` chains work: `lcurl.safe`, `xml`, `base64`, `sha1` (including `sha1/init.lua`'s `pcall(require, "bit")`)
- `setmetatable` works
- `Common_test.lua` (minimal Common with all requires) passes fully
- Main.lua successfully loads: `GameVersions`, `Common`, `CalcFormat`

---

## Current blocker

**Hang at `LoadModule("Data/Global")`** inside Main.lua (which is inside PLoadModule).

Call chain when it hangs:
```
mac_run_after_main_if_requested
  → mac_pload_module_pcall        [lua_pcall frame 1]
    → Main.lua
      → LoadModule("Data")        [lua_pcall frame 2]
        → Data/init.lua
          → LoadModule("Data/Global")  [lua_pcall frame 3]
            → Data/Global.lua     ← hangs here
```

`Data/Global.lua` is a **pure-data file** (613 lines, only table literals, no `require`/`pcall`/
function calls). The hang is not in Lua logic — it's suspected to be a `lua_pcall` nesting depth
limit on the arm64 GC64 interpreter.

---

## Your task

### Step 1 — Try `lua_call` for LoadModule

Change `l_LoadModule` in `ui_api.cpp` to use `lua_call` (unprotected) instead of `lua_pcall`:

```cpp
// Current (hangs at depth):
const int perr = lua_pcall(L, extraArgs, LUA_MULTRET, 0);
if (perr != LUA_OK) {
    const char* msg = lua_tostring(L, -1);
    luaL_error(L, "LoadModule() error running '%s':\n%s", fileStr.c_str(), msg ? msg : "unknown");
}

// Try (unprotected — errors propagate to PLoadModule's pcall):
lua_call(L, extraArgs, LUA_MULTRET);
```

This reduces pcall depth by 1 for every `LoadModule` call. Errors will propagate up to the
PLoadModule's `lua_pcall` frame as `luaL_error` (already protected).

### Step 2 — Rebuild and test

```bash
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos
cd ~/PoB-PoE2-build
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
```

Wait 50–90s for PLoadModule; then minutes for Main.Init.

### Step 3 — If Data/Global still hangs

The issue may not be pcall depth. Other possibilities:
- `Data/Global.lua` is large enough to trigger a LuaJIT compilation timeout in interpreter mode
- The `luaL_loadfile` call itself hangs (parsing 613 lines)
- Add `ConPrintf` traces around the `luaL_loadfile` and `lua_call` in `l_LoadModule` to isolate
  whether it's the load or the execution

### Step 4 — After Main.lua loads fully

Check whether `global main` is visible:
```
macOS: global main after plm is table  ← success
macOS: global main after plm is nil    ← need mac_sync fix
```

If nil, the `_G.main = new("ControlHost")` in Main.lua is being set in a coroutine-local environment.
Fix: add explicit `lua_setglobal(L, "main")` from C after PLoadModule returns, pulling `main`
from the PLoadModule result table.

### Step 5 — main.Init and beyond

Once Main loads and `global main` exists, `mac_run_after_main_if_requested` will call:
1. `launch._finishAfterMain(launch)` — sets up version strings, manifest, update check
2. `main.Init(main)` — the big initialization (may take minutes on first run)

Expect new errors here — likely more CClosure issues or missing C modules. Fix iteratively.

---

## Key files

| File | Purpose |
|------|---------|
| `ui_api.cpp` | `l_LoadModule`, `mac_run_pload_module_impl`, `l_PLoadModule` |
| `ui_main.cpp` | `mac_lightfunc_pcall`, `l_mac_pcall/xpcall/require`, `mac_run_after_main_if_requested`, global sync |
| `ui_main.h` | Prototypes for `mac_pload_module_pcall`, `mac_sync_globals_from_helper_co` |
| `docs/macos/pob-launch/Launch.lua` | Reference launch file (copy to PoB `src/`) |
| `MACOS_PORT.md` | Full plan, root causes C.1–C.9, session workflow |

## GC64 rules (updated)

- Never use `lua_pushcclosure(L, f, n)` with `n > 0` on macOS (CClosure crash in coroutines)
- Never call `lua_resume` from inside a `lua_pcall`-protected frame (hangs on arm64)
- Never capture C API return values in Lua bytecode — use stash globals
- `pcall`/`xpcall`/`require` must use `lua_pcall` (not `mac_lightfunc_pcall`/`lua_resume`)
- `mac_lightfunc_pcall` is still used for the top-level `OnInit` and `OnFrame` callbacks only
- Never call `require("xml")` before Main
- Never assign `version* = "?"` before PLoadModule
