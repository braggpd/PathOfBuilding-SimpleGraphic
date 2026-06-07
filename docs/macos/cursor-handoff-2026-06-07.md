# macOS Port — Cursor Handoff (2026-06-07)

## What this is

`PathOfBuilding-SimpleGraphic` is the C++ engine that hosts the LuaJIT 5.1
runtime for Path of Building 2. This fork's purpose is a **native macOS Apple
Silicon (arm64) port**. The game logic (~99% Lua) lives in a separate repo
(`PathOfBuilding-PoE2`). All platform work is here.

---

## Repos and directories on your Mac

| Directory | What it is |
|---|---|
| `~/path/to/PathOfBuilding-SimpleGraphic` | Engine repo (this repo, C++) — find it with `find ~ -name "SimpleGraphic.vcxproj" -maxdepth 6` |
| `~/path/to/PathOfBuilding-PoE2` | Game Lua repo — find with `find ~ -name "Launch.lua" -maxdepth 6` |
| `~/PoB-SimpleGraphic-build/` | CMake build dir for the engine |
| `~/PoB-PoE2-build/` | PoB runtime install prefix; run the app from here |
| `~/PoB-PoE2-build/runtime-macos/` | Installed dylibs + launcher binary |
| `~/PoB-PoE2-build/src/` | Symlink or copy of the PoB Lua source |

**Path constraint:** repository paths must not contain spaces (LuaJIT `make`
splits `PREFIX` on spaces).

---

## Active branch

```
Engine repo:  macos/issue-8-sync-smoke-merge
              remote: origin/macos/issue-8-sync-smoke-merge
```

All macOS work lives here. `master` stays in sync with upstream via rebase.

---

## Build workflow (run on your Mac)

```bash
# After pulling new engine commits:
cp ui_main.cpp ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build \
      --prefix ~/PoB-PoE2-build/runtime-macos

# Launch:
cd ~/PoB-PoE2-build
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
```

`ui_main.cpp` and `ui_main.h` are the only engine files changed in this
session. Copy them into the build directory before rebuilding.

---

## Crash history and what fixed each one

### Crash 1 — `Global.lua:433` / `bit.*` SIGSEGV
**Root cause:** LuaJIT's `bit.*` functions are `LJLIB_ASM` — they dispatch via
an assembly table that is broken on arm64 GC64. Any `bit.*` call would SIGSEGV.

**Fix:** Replaced all `bit.*` with plain-C LIGHTFUNC implementations in
`ui_main.cpp` (~line 1982). Commit `f669bfe`.

### Crash 2 — `Global.lua:195` / start-address 4GB boundary
**Root cause:** LuaJIT's GC64 uses full 64-bit pointers, but some bytecode
handlers or helpers have latent 32-bit truncation bugs. If a GC allocation
starts at an address whose lower 32 bits = 0 (i.e., exactly on a 4 GB
boundary), the pointer silently truncates to NULL.

**Fix (v1):** Custom allocator `mac_gc64_alloc` ensures no allocation _start_
address has lower-32 = 0. Uses a uint8_t offset header. Commit `615787e`.

### Crash 3 — `Modules/Data` / interior stack pointer crossing
**Root cause:** v1 only protected start addresses. The Lua stack is a single
large allocation; if it _straddles_ a 4 GB boundary, interior pointers like
`L->base` (which is a raw `TValue*`) can land exactly on a 4 GB boundary
(lower-32 = 0), giving a null dereference in `BC_CALL`.

**Fix (v2):** Upgraded `mac_gc64_alloc` to prevent any allocation from
_crossing_ a 4 GB boundary. Pre-allocates `nsize + min(nsize+8, 512KB+8)` extra
bytes; walks an offset to place user data so no byte in `[user, user+nsize)`
has lower-32 = 0. Uses a uint32_t offset header (4 bytes before user data).
Commit `7723f3c`.

Log line to confirm v2 is running:
```
macOS: GC64 interior-pointer-safe allocator v2 installed (#8)
```

### Crash 4 — `Modules/Data` still / GC finalizer with null method pointer
**Root cause (diagnosed from crash report `.ips` file):**

Crash instruction: `LDR x8, [x2, #0]` where x2 = 0.
Call stack: `mac_run_pload_module_impl → lua_pcall (outer, runs Data.lua) →
LuaJIT+10292 (GC incremental step, runs __gc finalizer) → lua_pcall (inner,
protected finalizer call) → LuaJIT+48504 crash`.

The GC fires _during_ Data.lua's execution (Data.lua creates thousands of
objects → aggressive GC). It finds a dead GCobj with a `__gc` metamethod,
looks up the method via a GC64 pointer, gets NULL (GC64 truncation bug in the
finalizer lookup path), and crashes dereferencing the null method TValue*.

**Fix:** Stop GC for the entire duration of each PLoad module's `lua_pcall`,
then restart it. Commit `52078d3` (most recent, **not yet tested**).

```cpp
// In mac_pload_coroutine_call(), before the lua_pcall for each module:
lua_gc(L, LUA_GCSTOP, 0);
const int callErr = lua_pcall(L, 0, 0, 0);
lua_gc(L, LUA_GCRESTART, -1);
```

---

## Current state (as of this handoff)

- Commits `7723f3c` (v2 allocator) and `52078d3` (GC stop) are pushed to
  `origin/macos/issue-8-sync-smoke-merge` but **not yet tested**.
- The user has NOT rebuilt since `52078d3` was pushed.
- Expected result after rebuild: Data.lua loads without crashing, then
  subsequent modules load, and PoB's UI appears.

---

## How to read macOS crash reports (`.ips` files)

Crash reports are in `~/Library/Logs/DiagnosticReports/`. They are JSON (two
objects separated by a blank line).

Extract the crashed thread:
```bash
python3 -c "
import sys, json
content = open(sys.argv[1]).read().split('\n\n', 1)
for part in content:
    try:
        d = json.loads(part.strip())
        for t in d.get('threads', []):
            if t.get('triggered'):
                print('THREAD STATE:')
                print(json.dumps(t.get('threadState',{}), indent=2))
                print('FRAMES:')
                for f in t.get('frames',[])[:20]: print(f)
    except: pass
" ~/Library/Logs/DiagnosticReports/Path\ of\ Building-PoE2-YYYY-MM-DD-HHMMSS.ips
```

Key LuaJIT arm64 registers:
| Register | LuaJIT role |
|---|---|
| x19 | BASE — current Lua stack frame base (TValue*) |
| x20 | PC — bytecode program counter |
| x21 | DISPATCH — LuaJIT dispatch table |
| FAR | Fault Address Register — address that caused the crash |

Decode the crashing instruction from `instructionByteStream.atPC` (base64):
```python
import base64, struct
at_pc = base64.b64decode("<value from .ips>")
word = struct.unpack_from('<I', at_pc, 0)[0]
print(f"Crashing instruction: {word:08x}")
# f9400048 = LDR x8, [x2, #0]  (load x8 from address in x2 + 0)
# 39400008 = LDRB w8, [x0, #0]  (load byte from address in x0)
```

---

## Key architectural decisions (do not revisit)

| Decision | Detail |
|---|---|
| `mac_gc64_alloc` | Custom allocator passed to `lua_newstate`; protects ALL allocations including lua_State itself from GC64 4GB boundary crashes |
| JIT off | Via `jit.off()` Lua API after LIGHTFUNC replacements; NOT via `luaJIT_setmode` (that breaks the interpreter) |
| `bit.*` as LIGHTFUNC | All `bit.*` replaced in `mac_replace_broken_ffuncs()`; 32-bit semantics only (int64 path not reached since update check is disabled on macOS) |
| `pcall`/`xpcall` replaced | `l_mac_pcall` / `l_mac_xpcall` — use `lua_pcall` directly (NOT `lua_resume`; resume hangs inside pcall-protected frames on arm64 GC64) |
| PLoad module execution on root L | Nested modules loaded via `lua_pcall` on root L, not co; avoids nested-resume crashes |
| GC stopped during module loading | `lua_gc(GCSTOP/GCRESTART)` wraps each module's `lua_pcall` in the PLoad loop |

---

## Files changed in this project

| File | What's in it |
|---|---|
| `ui_main.cpp` | `mac_gc64_alloc` (v2 allocator), `mac_replace_broken_ffuncs`, `l_mac_pcall`, `l_mac_xpcall`, `mac_pload_coroutine_call` (PLoad loop with GC stop), all macOS runtime init |
| `ui_main.h` | Declaration of `mac_gc64_alloc` and other macOS helpers |
| `ui_api.cpp` | `PLoadModule`, `LoadModule`, `mac_push_plm_result` |
| `docs/macos/pob-launch/` | Reference `Launch*.lua` files for the PoB repo |
| `MACOS_PORT.md` | Full port plan, status, handoff sections |
| `CLAUDE.md` | Codebase context for AI assistants |

---

## Next steps (in priority order)

1. **Rebuild and test** with `52078d3` (GC stop during PLoad).
   - Pull `macos/issue-8-sync-smoke-merge`, copy `ui_main.cpp` + `ui_main.h`, rebuild.
   - If Data.lua loads: subsequent modules should follow, then PoB UI appears.
   - If it crashes again: get the new `.ips` crash report and repeat analysis.

2. **If all modules load but UI is blank or crashes later:** check `OnInit` /
   `OnFrame` callbacks. Previous session confirmed rendering worked
   (passive tree, mouse, text all displayed). A regression would be in the
   Lua callback layer.

3. **Close issue #8** once PoB renders and is interactable.
   `https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8`

4. **PoB repo issue #9:** copy `docs/macos/pob-launch/*.lua` into the PoB
   repo's `src/`. PR for the macOS launch files.

5. **Phase 3:** `.app` bundle, code signing, DMG packaging.

6. **Future / known rough edges:**
   - Shutdown SIGBUS (happens on exit; non-blocking, document if persistent)
   - `lcurl.safe` for update check (disabled on macOS via `launch._isMacOS`)

---

## Typical debug loop

```
Run → crash → check ~/Library/Logs/DiagnosticReports/*.ips
→ decode registers (x19/BASE, x20/PC, x2/FAR, x[fault])
→ decode crash instruction from instructionByteStream.atPC
→ read call stack frames (imageIndex 4 = libluajit, imageIndex 1 = libSimpleGraphic)
→ hypothesis → code fix in ui_main.cpp → copy + rebuild → test
```
