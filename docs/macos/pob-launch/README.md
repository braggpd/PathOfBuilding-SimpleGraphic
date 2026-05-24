# PoB launch split (macOS #8) — reference files

Copy these into **`PathOfBuilding-PoE2/src/`** (PoB repo), not this engine repo.

| File | Role |
|------|------|
| `Launch.lua` | Minimal bootstrap: small `OnInit`, `PLoadModule`, set `launch._runAfterMain` |
| `LaunchAfterMain.lua` | Post-Main init (engine runs from C when `_runAfterMain` is set) |
| `LaunchCallbacks.lua` | All other `launch:*` handlers (loaded after Main via stash `loadfile`) |

Requires engine with `mac_run_after_main_if_requested`, `__mac_loadfile_stash_c`, and `luaJIT_setmode` JIT off (`macos-port` / `#8` branch).

See `MACOS_PORT.md` → **Next session — close #8** for rules and verification commands.
