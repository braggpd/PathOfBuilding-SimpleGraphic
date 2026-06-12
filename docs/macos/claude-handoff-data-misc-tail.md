# Claude Handoff — Data.lua PLoad tail / `data.misc` (2026-05-27)

Copy the **Session prompt** block below into a new Claude Code or Cursor chat.

**Context:** Patrick may pivot to a **different approach** than continued slice-by-slice engine patching. Read this file for ground truth on what was tried and what remains; confirm direction before implementing.

---

## Session prompt (copy from here)

```
You are continuing the macOS arm64 (GC64) port of PathOfBuilding-SimpleGraphic.

Branch: `macos/issue-8-sync-smoke-merge` (engine changes may be **uncommitted** — check `git status`).

## Read first (order matters)

1. `CLAUDE.md` — build paths, GC64 rules, architectural decisions
2. `.cursor/rules/macos-port.mdc` — always-on port rules
3. **`docs/macos/claude-handoff-data-misc-tail.md`** — this file (current blocker)
4. Skim `ui_main.cpp` → `l_mac_pload_data_after_misc_c`, `mac_sync_co_globals_to_root`, `mac_pload_root_loadmodule`
5. Skim `ui_api.cpp` → `mac_build_pload_data_tail_chunk`, Data.lua PLoad patch in `mac_lua_load_module_file`

## Current milestone

- **Done (earlier #8 work):** Dev launch reached UI with a simpler load path; many GC64 fixes landed.
- **Done (this arc):** PLoad past `Data/Global`; `Data/Misc` inline on root; hollow-palm C-fill; Global OR64 skip; `powerStatList` (117–170) stubbed in tail.
- **Blocked:** Root tail `lua_pcall` for `Data.lua` dies at **`data.misc = {`** (line 171). Full Main PLoad does not complete.

## Ground truth — `data.misc` bisect (2026-05-27)

| `POB_MAC_BISECT_DATA_END` | Result |
|---------------------------|--------|
| **170** (preamble 8–111 only; no `data.misc`) | **OK** — `macOS BISECT: data.misc tail OK`, `std::exit(0)` |
| **171** (`data.misc = {` + synthetic `}`) | **SIGILL** during root tail `lua_pcall` |
| **172–248** | All fail (same crash) |

**Conclusion:** Failure is **not** a specific field inside the table. Even an **empty** `data.misc = {}` on root crashes. `data.powerStatList = {}` in the same chunk is fine.

Crash site (log sequence):

```text
macOS: PLoad Data/Misc inline OK
macOS: PLoad Data.lua tail OK ...   ← only when END=170
macOS: PLoad Data.lua tail running pcall...  ← END≥171, then SIGILL
```

## Architecture in tree (uncommitted — verify in diff)

When PLoad loads `Modules/Data`, `mac_lua_load_module_file` patches the chunk:

1. Replace `LoadModule("Data/Misc", data)` with `__mac_pload_data_after_misc_c(data)` + gate original co body (`if true then return else … end` + trailing `end`).
2. Co runs only `LoadModule("Data/Global")` + the C hook call.

**`__mac_pload_data_after_misc_c`** (`ui_main.cpp`) — single session with `savedCoRef` cleared:

1. `lua_xmove` `data` table co → root
2. Root `lua_pcall` → `__mac_loadmodule_c("Data/Misc", data)` (filtered Misc.lua, hollow-palm C-fill)
3. `mac_sync_co_globals_to_root` — **selective keys only** (full `_G` copy SIGSEGVs)
4. Build tail via `mac_build_pload_data_tail_chunk(ui, endLine)`
5. `luaL_loadbuffer` + root `lua_pcall` with `data` arg
6. Restore `s_macHelperCoRef`, `__mac_in_pload_flag`

**`mac_build_pload_data_tail_chunk`** (`ui_api.cpp`):

- Preamble: lines **8–111** (unless `POB_MAC_BISECT_DATA_NO_PREAMBLE=1`)
- Skips **117–170** (`powerStatList`); injects `data.powerStatList = {}` stub at line 170
- Body: lines **171–endLine**; auto-closes `data.misc` with `}` when `171 ≤ endLine < 248`
- Strips body lines containing `transform=function`

## Bisect harness

```bash
POB_ROOT=~/PoB-PoE2-build ./scripts/macos/bisect-data-misc.sh
```

Env vars:

- `POB_MAC_BISECT_DATA_MISC=1` — exit 0/1 after tail probe (no hang in UI loop)
- `POB_MAC_BISECT_DATA_END=N` — cap tail body at line N

**Use `Launch_oninit_pload.lua`** for probes (Main PLoad path). Direct `PLoadModule("Modules/Data")` crashes Global at `nested=0`.

Probe success = log contains `macOS BISECT: data.misc tail OK`. Do **not** treat subprocess exit code alone as success (SIGILL → negative rc).

## What NOT to revisit without discussion

- Nested `l_LoadModule` on root while `__mac_in_pload_flag` true
- Full `_G` sync co → root (`mac_sync_co_globals_to_root` iterating all globals)
- Shared `mac_pload_root_pcall` helper (caused Global SIGBUS when used)
- `lua_resume(co, 0)` after root pcall with return values (SIGSEGV)
- Assigning `version* = "?"` before `PLoadModule`
- Bisecting by raw line number without closing braces (invalid Lua → hang, not signal)

## Build & test

```bash
ENGINE_REPO="$HOME/GitHub - Personal/PathOfBuilding-SimpleGraphic"
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos

cd ~/PoB-PoE2-build
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua
```

Kill zombies after bisect: `pkill -9 -f "Path of Building"`.

## Success criteria (this task)

1. `macOS: PLoad Data.lua tail OK` for **full** tail (through EOF or through next stable gate)
2. `macOS: PLoad coroutine finished OK` / `PLoadModule … OK`
3. Eventually `main.Init` and UI on full `Launch.lua`

## Recommended next steps — **continue slice-and-patch path**

If Patrick chooses to stay on the current approach, this is the order I would use:

### Step 1 — Stub/skip `data.misc` (lines 171–248)

Same pattern as `powerStatList` (117–170) and Misc hollow palm:

- In `mac_build_pload_data_tail_chunk`, skip lines **171–248**
- Inject at line 170 (after powerStatList stub):

  ```lua
  data.misc = {} -- macOS PLoad stub (lines 171-248 skipped arm64 GC64). (#8)
  ```

- C-fill **minimum fields** required by the next tail segments (252–270 use `data.misc.maxExperiencePenaltyFreeAreaLevel`, `experiencePenaltyMultiplier`, `MaxEnemyLevel`). Either:
  - hardcode the ~78 scalar entries in C (`lua_createtable` + `lua_setfield`), or
  - run a one-time offline extract from `Data.lua` into a generated C/Lua snippet

Verify: bisect END=248 OK, then END=270 (monsterExperienceLevelMap `do` block).

### Step 2 — Bisect tail after `data.misc`

| Region | Lines | Notes |
|--------|-------|-------|
| `skillColorMap` | 250 | small table, uses `colorCodes` (synced) |
| `monsterExperienceLevelMap` | 252–270 | `do` block + `triangular()` from preamble |
| `cursePriority` | 272+ | large static table |
| `LoadModule("Data/ModScalability")` | 412 | nested LoadModule on root — test early |

Extend `POB_MAC_BISECT_DATA_END` or add `POB_MAC_BISECT_DATA_START` for post-248 bisect. Reuse `scripts/macos/bisect-data-misc.sh` pattern.

### Step 3 — Nested `LoadModule` in tail (412+)

Hypothesis: root `l_LoadModule` **outside** active PLoad co should work (same as Misc inline path with `__mac_in_pload_flag` cleared). If not, each nested module may need its own inline hook or fresh-co runner.

### Step 4 — Commit engine work

Files: `ui_main.cpp`, `ui_api.cpp`, `ui_main.h`, `scripts/macos/bisect-data-misc.sh`, `scripts/macos/test_pload_data.lua`.

## Alternative approaches — **if pivoting**

Patrick may prefer one of these instead of patching every `Data.lua` hot spot:

| Approach | Pros | Cons |
|----------|------|------|
| **A. Pre-serialize `data` on macOS** | Load one binary/Lua snapshot after Global+Misc; skip interpreting large table literals | Maintenance when PoB updates Data.lua; large one-time build step |
| **B. Run entire `Data.lua` on fresh root co** | Avoid Main-co + root-pcall interaction | Prior attempts: co bytecode after Misc still crashed; needs re-validation |
| **C. Split `Data.lua` in PoB repo (#9)** | Move `data.misc` / static tables to separate files loaded via root hooks | PoB-side change; upstream coordination |
| **D. LuaJIT / allocator fix upstream** | Fixes root cause (GC64 + large table constructors?) | High effort; may be unfixable in 2.1 beta without fork |
| **E. Interpreter-only table build in C** | One C function builds all static tables from extracted constants | Large but deterministic; no Lua table literal bytecode |

**Ask Patrick which direction before large implementation.**

## Files touched (engine, likely uncommitted)

| File | Role |
|------|------|
| `ui_main.cpp` | `l_mac_pload_data_after_misc_c`, selective `mac_sync_co_globals_to_root`, bisect exit hooks |
| `ui_api.cpp` | Data.lua PLoad patch, `mac_build_pload_data_tail_chunk`, Misc/Global filters |
| `ui_main.h` | `mac_build_pload_data_tail_chunk`, `mac_bisect_data_misc_enabled` |
| `scripts/macos/bisect-data-misc.sh` | Binary search 171–248 |
| `scripts/macos/test_pload_data.lua` | Fast probe (broken for Global nested=0 — use Launch_oninit_pload) |

## Related docs

- `docs/macos/claude-handoff-plm-nested-load.md` — earlier nested LoadModule / Global SIGSEGV (partially superseded)
- `docs/macos/issue-8-launch-handoff.md` — PoB launch split
- `scripts/macos/bisect-misc.sh` — Misc.lua hollow-palm bisect
- `scripts/macos/bisect-global.sh` — Global.lua bisect

Report: bisect results, chosen approach, test output, and whether full `Launch.lua` reaches `main.Init`.
```

---

## Notes for humans

- **Branch:** `macos/issue-8-sync-smoke-merge`; engine diff may not be committed.
- **PoB tree:** `~/PoB-PoE2-build` — `Data.lua` line numbers match `src/Modules/Data.lua`.
- **Prior false bisect:** An earlier run reported “last good line 247” because the probe treated SIGILL exit codes as success. Fixed probe greps for `macOS BISECT: data.misc tail OK`.
