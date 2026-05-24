# Issue #8 — Launch handoff (2026-05-24)

**Read first:** `MACOS_PORT.md` → **Next session — close #8** (sections A–F).  
**Cursor:** `.cursor/rules/macos-port.mdc`  
**Claude:** `CLAUDE.md` → Current phase  
**PoB reference files:** `docs/macos/pob-launch/` (copy into PoB `src/`)

## One-line status

Engine + **split launch** reach `PLoadModule main type=table` (~30–90 s). **#8 not closed** until full UI runs through `main.Init` and frame loop; shutdown SIGBUS may remain.

## PoB file split (required for `Launch.lua`)

| File | Role |
|------|------|
| **`Launch.lua`** | Small `OnInit` only: dev heuristic → `RenderInit` → `PLoadModule` → `launch._mainPlm` + `launch._runAfterMain` |
| **`LaunchAfterMain.lua`** | Version `"?"`, load `LaunchCallbacks.lua`, `main.Init`, xml manifest, updates |
| **`LaunchCallbacks.lua`** | All other `launch:*` methods |

Engine runs **`LaunchAfterMain.lua` from C** after `OnInit` when `launch._runAfterMain` is true (`mac_run_after_main_if_requested` in `ui_main.cpp`).

Reference copies: `docs/macos/pob-launch/`.

## macOS GC64 rules (do not break)

1. **No `versionNumber` / `versionBranch` / `versionPlatform = "?"` before `PLoadModule`** — crashes in &lt;1 s.
2. **No large post-Main code in the same `OnInit` function as `PLoadModule`** — bytecode poisons Main load.
3. **No `__mac_dofile_c` or `loadfile()` return capture in `Launch.lua`** — use C AfterMain hook + `__mac_loadfile_stash_c`.
4. **No `local chunk, err = loadfile(...)`** — use stash globals (`__mac_loadfile_chunk` / `__mac_loadfile_err`).
5. **`if mainPlm.err` / `mainPlm.main`** — not `local errMsg = mainPlm.err` before callbacks exist.
6. **No `require("xml")` before Main**; manifest loop uses numeric `for`, not `type(node)`.

## Next steps (in order)

1. **Merge engine branch** with `mac_run_after_main`, `luaJIT_setmode` JIT off, `__mac_loadfile_stash_c` (this repo).
2. **Copy** `docs/macos/pob-launch/*.lua` → PoB `src/` (or keep local `~/PoB-PoE2-build` copies).
3. **Rebuild & install** engine into `runtime-macos/`.
4. **Run** `./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua` — wait **50–90+ s** for `PLoadModule main type=table`; **minutes** for `main.Init`.
5. **Confirm** UI renders; fix regressions in `LaunchAfterMain` / callbacks only.
6. **Open PoB PR** for launch split ([#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9)); engine PR closes [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8) when dev launch verified.
7. **Optional:** shutdown SIGBUS after minimal scripts; document if still present.

## Verify

```bash
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua   # regression (~1–3 min)
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua                 # target
```

Success: `before PLoadModule` → `PLoadModule main type=table` → UI / no immediate segfault during Init.
