# Claude Handoff — PLoad nested LoadModule / Global.lua SIGSEGV

Copy the **Session prompt** block below into a new Claude Code or Cursor chat.

---

## Session prompt (copy from here)

```
You are continuing the macOS arm64 (GC64) port of PathOfBuilding-SimpleGraphic on branch `macos/issue-8-sync-smoke-merge`.

## Read first (order matters)

1. `CLAUDE.md` — build paths, conventions, architectural decisions
2. `.cursor/rules/macos-port.mdc` — always-on port rules
3. `docs/macos/claude-handoff-plm-nested-load.md` — this file (full context)
4. Skim `ui_main.cpp` → `mac_pload_coroutine_call`, `l_mac_lm_yield`, `wrapLoadModule` in `kMacReturnApiWraps`
5. Skim `ui_api.cpp` → `l_LoadModule`, `mac_run_pload_module_impl`, `mac_load_global_lua_slice` (bisect only)

## Current milestone

- **Done:** Dev launch reaches UI; PLoad + yield services GameVersions, Common, CalcFormat.
- **Blocked:** Full `PLoadModule("Modules/Main")` dies loading `Data.lua` → nested `LoadModule("Data/Global")` → SIGSEGV in inner `l_LoadModule`.

## Latest LLDB (no bisect) — ground truth

```
EXC_BAD_ACCESS (code=1, address=0x0)
  libluajit
  l_LoadModule          ← inner (likely Data/Global.lua)
  libluajit
  l_LoadModule          ← outer (Modules/Data.lua)
  libluajit
  mac_pload_coroutine_call
  mac_run_pload_module_impl
  ScriptInit → …
```

**Not** nested `lua_pcall` in `l_LoadModule` anymore (that was fixed). Failure is **nested `l_LoadModule`** while servicing PLoad yields.

## Architecture in tree (what we implemented)

| Piece | Purpose |
|-------|---------|
| `mac_pload_coroutine_call` | Run `Main.lua` in a coroutine; loop on `LUA_YIELD` |
| `wrapLoadModule` + `__mac_in_pload_flag` | While PLoad active, `LoadModule` → `__mac_lm_yield_c(req)` (table), not `coroutine.yield` alone |
| `l_mac_lm_yield` | C `lua_yield(L, 1)` so resume returns to `mac_pload_coroutine_call` |
| Yield handler | Reads req table, calls `__mac_loadmodule_c` on **root** with `lua_xmove` args, `lua_call` |
| `l_LoadModule` | `lua_call` chunk; if `mac_is_servicing_pload_queue()` → `mac_run_loaded_chunk_fresh_co` |
| `mac_set_in_pload` / flags | `__mac_in_pload_flag` globals (C bool wrappers unreliable on GC64) |
| Do **not** convert `coroutine.*` to LIGHTFUNC | `coroutine.yield` must stay raw LJLIB_CF |

## Root cause hypothesis (implement this)

When servicing a yield, the handler runs the module on the **root** state via `__mac_loadmodule_c` → `l_LoadModule` → `lua_call(Data.lua)` on root.

`Data.lua` line 7: `LoadModule("Data/Global")`. With `__mac_servicing_lm_flag` true, the Lua wrapper **skips yield** and calls `__mac_loadmodule_c` again → **nested `l_LoadModule`** on root (often `mac_run_loaded_chunk_fresh_co` for Global) → SIGSEGV.

**Intended model (Windows-like):** only one C “protection” boundary at PLoad; nested loads should **not** re-enter `l_LoadModule` on root while a parent module chunk is running on root.

**Fix direction:**

1. Service each yield by running the **wrapped** `LoadModule` on the **Main coroutine** (`co`), not `__mac_loadmodule_c` on root — so nested loads become **another yield** handled by the same `while (status == LUA_YIELD)` loop.
   - Note: `lua_call(co, LoadModule, …)` when the wrapper yields may need a resume loop, not a single `lua_call`; design carefully (nested yields while “servicing”).
2. **OR** keep root C loader but add an internal `mac_loadmodule_exec_c` used only from the yield handler (no Lua wrapper), and make the wrapper **always** yield when `__mac_in_pload_flag` (remove `__mac_servicing_lm_flag` bypass).
3. **Do not** use `lua_settop(co, 0)` on a suspended coroutine — only `lua_pop(co, 1)` for the yield table.
4. **Do not** `lua_xmove` args from `co` without moving them onto `L` before `lua_call(L, …)`.
5. Keep `mac_run_pload_module_impl` → `mac_pload_coroutine_call` (not root `lua_pcall` for Main).

## Bisect findings (Global.lua — do not re-litigate)

| Range | Result |
|-------|--------|
| 106–135, 137–163, 165–191, 193–209, 303–338 | `slice OK` (definitions only) |
| 106–200, 258–338 (harness) | load errors (invalid slice — tool limitation) |
| 106–338 | SIGSEGV when slice **runs** (includes 297–299 `bnot(KeywordFlag.MatchAll)`) |

Crash is likely **executing** Global (bitwise / load-time setup), not defining `OR64` in isolation.

Bisect env: `POB_MAC_BISECT_GLOBAL=1 POB_MAC_BISECT_LINES=start-end`. Uses `lua_pcall` on root inside `mac_load_global_lua_slice` — different from production path.

## Build & test

```bash
ENGINE_REPO="$HOME/GitHub - Personal/PathOfBuilding-SimpleGraphic"
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos

cd ~/PoB-PoE2-build
ln -sfn "$(pwd)/runtime/SimpleGraphic" runtime-macos/SimpleGraphic
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
```

Wait 50–90s+ for Main module load lines. Success looks like:

```text
macOS: PLoad servicing LoadModule Modules/Data
macOS: LoadModule running .../Data.lua
macOS: PLoad servicing LoadModule Data/Global    ← nested yield (goal)
macOS: LoadModule running .../Data/Global.lua
macOS: LoadModule done .../Global.lua
… Main.lua: all LoadModule done
macOS: PLoad coroutine finished OK
```

Regression:

```bash
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua
```

## Constraints (do not regress)

- No nested `lua_pcall` in `l_LoadModule` on macOS.
- Keep Windows paths unchanged (`#if !APPLE`).
- C API return stash wrappers for GC64; no `local x = GetTime()` from C on macOS.
- `PCall` uses `lua_pcall` not nested `lua_resume` for draw callbacks.
- Do not commit to `master`; work on `macos-port` / issue branches.
- Repo path must have **no spaces** (LuaJIT build).

## Success criteria for this task

1. Full launch past `Data/Global.lua` without SIGSEGV.
2. `macOS: PLoadModule … OK` and `mac_sync_globals_from_helper_co` sees `main` table.
3. Eventually `launch._finishAfterMain` / `main.Init` (may surface later bugs).
4. Minimal diff; no unrelated refactors.

## Suggested first implementation

1. Change yield handler: service by resuming nested load protocol on **Main co** (reentrant `while (LUA_YIELD)`), not root `lua_call(__mac_loadmodule_c)`.
2. Remove wrapper bypass `not __mac_servicing_lm_flag` — always yield when `__mac_in_pload_flag`.
3. In `l_LoadModule`, use plain `lua_call` for chunk when called from C (only when not in coroutine yield path); drop `mac_run_loaded_chunk_fresh_co` for servicing unless you prove it needed for a leaf case.
4. Add `ConPrintf` traces: `servicing nested yield`, `LoadModule running`, one line per yield.
5. Test full launch + `Launch_oninit_pload.lua`.

Report: what you changed, test output, and LLDB if still crashing.
```

---

## Files touched this session (engine)

| File | Changes |
|------|---------|
| `ui_main.cpp` | `mac_pload_coroutine_call`, `__mac_lm_yield_c`, flags, `wrapLoadModule`, `mac_get_ui` registry fix, no LIGHTFUNC on `coroutine` |
| `ui_api.cpp` | `mac_run_pload_module_impl` → coroutine; `l_LoadModule` lua_call + bisect; servicing `lua_xmove` fix |
| `ui_main.h` | `mac_set_in_pload`, prototypes |

## Related docs

- `docs/macos/cursor-handoff-2026-05-24.md` — older `lua_call` vs `lua_pcall` notes
- `docs/macos/issue-8-launch-handoff.md` — PoB launch split
- `scripts/macos/bisect-global.sh` — line-range probes for Global.lua
