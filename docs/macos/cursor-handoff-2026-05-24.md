# Cursor handoff — macOS arm64 CClosure hunt (2026-05-24 evening)

**Latest engine commit:** `b8b7989` on branch `macos/issue-8-sync-smoke-merge`  
**Repo:** `braggpd/PathOfBuilding-SimpleGraphic`  
**Full context:** `MACOS_PORT.md`, `CLAUDE.md`, `docs/macos/issue-8-launch-handoff.md`

---

## What you are doing and why

We are porting PathOfBuilding-SimpleGraphic (the LuaJIT host engine) to macOS Apple Silicon
(arm64). The engine embeds LuaJIT 2.1 with GC64 mode. On arm64 GC64, calling a **CClosure**
(any C function registered via `lua_pushcclosure(L, f, n)` with `n > 0` — i.e. with upvalues)
from Lua bytecode inside a **`lua_resume` coroutine** causes `EXC_BAD_ACCESS` at a tagged
pointer address like `0xffff000000000007`. LIGHTFUNCs (`lua_pushcfunction` / `lua_pushcclosure`
with `n == 0`) are safe.

The fix pattern is established and mechanical: for every crashing CClosure, register it as a
LIGHTFUNC and look up its upvalue data at call time from `LUA_REGISTRYINDEX` or `lua_getglobal`.

---

## What has been done

### Commits landed on `macos/issue-8-sync-smoke-merge`

| Commit | What it fixed |
|--------|--------------|
| `8c5fbe8` | `l_mac_pcall`/`l_mac_xpcall` use `lua_resume`; `CallCallbackOnThread` uses `lua_resume`; macOS `LoadModule`/`PLoadModule` return named tables |
| `b3cc4c6` | `mac_resume_call` replaces `lua_call` in `l_mac_require` |
| `3f44494` | Fix `l_mac_require` leaving extra values on Lua stack |
| `12a15d3` | `luaJIT_setmode` JIT off; `__mac_loadfile_stash_c`; `mac_run_after_main_if_requested` |
| `6b03f97` | **All 7 `ADDFUNCCL` CCLosures fixed**: `ConPrintf`, `SetCallback`, `GetCallback`, `SetMainObject`, `NewImageHandle`, `NewArtHandle`, `NewFileSearch` — all now LIGHTFUNCs on macOS; upvalue data fetched from `LUA_REGISTRYINDEX` at call time |
| `b8b7989` | `MACOS_PORT.md` handoff update (this state) |

### Fix pattern used in `6b03f97` (reference for new fixes)

**Before** (crashes in coroutine):
```cpp
// InitAPI:
lua_getglobal(L, "string");
lua_getfield(L, -1, "format");
ADDFUNCCL(ConPrintf, 1);   // lua_pushcclosure(L, l_ConPrintf, 1) — CClosure!

// l_ConPrintf body:
lua_pushvalue(L, lua_upvalueindex(1));  // ← EXC_BAD_ACCESS on arm64 GC64
lua_insert(L, 1);
lua_call(L, n, 1);
```

**After** (safe LIGHTFUNC):
```cpp
// InitAPI:
#if __APPLE__ && __MACH__
    lua_pushcfunction(L, l_ConPrintf); lua_setglobal(L, "ConPrintf");
#else
    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    ADDFUNCCL(ConPrintf, 1);
    lua_pop(L, 1);
#endif

// l_ConPrintf body (macOS path):
#if __APPLE__ && __MACH__
    lua_getglobal(L, "string");       // fetch upvalue at call time
    lua_getfield(L, -1, "format");
    lua_remove(L, n + 1);
    lua_insert(L, 1);
#else
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
#endif
    lua_call(L, n, 1);
```

For registry-stored upvalues (handle metatables, callbacks table), the pattern is:
```cpp
// Instead of: lua_pushvalue(L, lua_upvalueindex(1)); // metatable
#if __APPLE__ && __MACH__
    lua_getfield(L, LUA_REGISTRYINDEX, "uiimghandlemeta");
#else
    lua_pushvalue(L, lua_upvalueindex(1));
#endif
    lua_setmetatable(L, -2);
```

---

## Current crash

**Output before crash:**
```
Renderer initialised in 218 msec.
before PLoadModule
zsh: segmentation fault
```

**Location:** Inside `mac_lightfunc_pcall` during `Modules/Main.lua` coroutine execution.
`PLoadModule main type=table` does NOT print — the crash happens before Main.lua returns
its value. All engine-registered CCLosures are fixed; the remaining crash is in a C function
called from Main.lua's loading phase.

---

## Your task

### Step 1 — Get the backtrace

On the user's Mac in `~/PoB-PoE2-build`:

```bash
lldb -- ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
(lldb) run
# wait ~30-90s for "before PLoadModule", then crash
(lldb) thread backtrace
```

### Step 2 — Interpret the backtrace

Look for a **named C function** between LuaJIT interpreter frames. The backtrace will look like:

```
frame #0-3: libluajit-5.1.2.1.0.dylib`___lldb_unnamed_symbol...
frame #4:   libluajit-5.1.2.1.0.dylib`___lldb_unnamed_symbol...  ← interpreter
frame #5:   libSimpleGraphic.dylib`SOME_FUNCTION(lua_State*) + N  ← THIS IS THE CRASH
frame #6:   libluajit-5.1.2.1.0.dylib`___lldb_unnamed_symbol...  ← interpreter called it
frame #7:   libluajit-5.1.2.1.0.dylib`lua_resume + ...
frame #8:   libSimpleGraphic.dylib`mac_lightfunc_pcall(...)
...
```

The function at **frame #5** (or whichever named function appears between interpreter frames)
is the CClosure that is crashing.

### Step 3 — Fix based on what the backtrace shows

#### Case A: Function is in `libSimpleGraphic.dylib` (engine function)

Search `ui_api.cpp` and `ui_main.cpp` for any remaining `lua_pushcclosure(L, f, n)` with
`n > 0` that registers the identified function. Apply the same LIGHTFUNC fix pattern as
`6b03f97`. Guard with `#if __APPLE__ && __MACH__`.

Check specifically:
- Any `ADDFUNCCL` calls that might have been missed
- The `sol::usertype<Texture_c>` SOL2 binding — SOL2 registers constructors/methods as
  CCLosures. If `Texture()` constructor is the crash, the fix is to replace the SOL2
  registration with a manual `lua_pushcfunction` wrapper that calls `new Texture_c()` directly.
- Any function in `ui_main.cpp` still using `lua_pushcclosure` with upvalues

#### Case B: Function is in a `.so` extension module (`lcurl`, `lzip`, `lua-utf8`, `socket`)

These are C extensions loaded via `dlopen`/`dlsym` preregistration in `package.preload`. Their
internal functions are already registered by the extension's own init code and we can't change
that source easily.

Workaround: intercept the call in Lua. After the module is loaded via `require`, wrap any
crashing functions:

```lua
-- In mac_jit_off or InitAPI Lua setup code:
local orig = lcurl.something
lcurl.something = function(...) return orig(...) end  -- plain Lua closure, not CClosure
```

OR: pre-wrap at C level by replacing the function in the module table with a LIGHTFUNC that
calls `lua_call` internally.

#### Case C: Function is in `libluajit-5.1.2.1.0.dylib` (LuaJIT internal)

This would mean a built-in like `string.*`, `table.*`, `math.*` is registered as a CClosure
(unusual — LuaJIT built-ins are normally LIGHTFUNCs). If this happens, check the LuaJIT
source for that function's registration and consider:
- Replacing it in the global table with a LIGHTFUNC wrapper
- Filing as a LuaJIT arm64 GC64 bug

#### Case D: Multiple named functions (crash deeper in the call chain)

If the named function calls another C function that is the actual CClosure, fix the innermost
one first. The pattern is always the same: the function registered with `lua_pushcclosure(L, f, n)`
with `n > 0` is the one that crashes when called from Lua bytecode inside `lua_resume`.

---

## After fixing

### Rebuild and test

```bash
# In engine source dir (~/GitHub - Personal/PathOfBuilding-SimpleGraphic or wherever cloned):
git pull origin macos/issue-8-sync-smoke-merge
cp ui_api.cpp ui_main.cpp ui_main.h ~/PoB-SimpleGraphic-build/
touch ~/PoB-SimpleGraphic-build/ui_api.cpp  # force ninja to recompile
ninja -C ~/PoB-SimpleGraphic-build/build

cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos

cd ~/PoB-PoE2-build
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
```

### Success criteria (ordered)

1. `before PLoadModule` prints ← already works
2. `PLoadModule main type=table` prints (~30–90 s) ← currently blocked
3. No crash after step 2 (LaunchAfterMain runs, main.Init called)
4. UI window renders, passive tree loads, basic calc works ← closes #8

### If there are more crashes after step 2

Same diagnosis loop: run under lldb, get backtrace, find the named C function, apply LIGHTFUNC
fix. Each crash should be a different CClosure called later in the execution chain. Repeat until
the UI renders.

### Commit convention

```
[macOS] Fix <FunctionName> CClosure crash in mac_lightfunc_pcall (#8)
```

Branch: `macos/issue-8-sync-smoke-merge`. Push after each fix so the user can rebuild.

---

## Key files

| File | Purpose |
|------|---------|
| `ui_api.cpp` | All Lua API bindings — primary file for CClosure fixes |
| `ui_main.cpp` | `mac_lightfunc_pcall`, `mac_jit_off`, `mac_run_after_main_if_requested`, built-in LIGHTFUNC replacements |
| `docs/macos/pob-launch/Launch.lua` | Reference launch file (copy to PoB `src/`) |
| `docs/macos/pob-launch/LaunchAfterMain.lua` | Reference AfterMain (copy to PoB `src/`) |
| `MACOS_PORT.md` | Full plan, root causes, session workflow |

## GC64 rules that must not be violated

- Never use `lua_pushcclosure(L, f, n)` with `n > 0` for any function that may be called
  from Lua bytecode inside a `lua_resume` coroutine on macOS.
- Never capture C API return values in Lua bytecode on arm64 GC64 — use stash globals.
- Never call `require("xml")` before `main.Init`.
- Never assign `versionNumber = "?"` before `PLoadModule` completes.
