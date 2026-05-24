# macOS Apple Silicon Port Plan

Tracking the work to build and maintain a native macOS M-series (arm64) port of
PathOfBuilding-PoE2, based on the Windows codebase.

**Forks**
- Engine: https://github.com/braggpd/PathOfBuilding-SimpleGraphic
- App: https://github.com/braggpd/PathOfBuilding-PoE2

---

## Architecture Overview

The app is two layers:

| Layer | Language | Repo | macOS work needed |
|---|---|---|---|
| Game logic, calculations, UI | ~99% Lua | PathOfBuilding-PoE2 `src/` | None — pure Lua, already cross-platform |
| Window, renderer, LuaJIT host | C++ | PathOfBuilding-SimpleGraphic | Yes — all platform work lives here |

SimpleGraphic already has partial macOS scaffolding (`engine/system/win/sys_macos.mm`,
`#ifdef __APPLE__` blocks in `CMakeLists.txt` and `sys_main.cpp`). It has never been
compiled for macOS and the CI is Windows-only.

---

## Phase 1 — Fork & Local Build

**Goal:** Get SimpleGraphic compiling and running on an M-series Mac.

### Tasks

- [x] **1.1** Add `arm64-osx` vcpkg triplet file at `triplets/arm64-osx.cmake` *(no issue — completed on setup)*

  ```cmake
  set(VCPKG_TARGET_ARCHITECTURE arm64)
  set(VCPKG_CRT_LINKAGE dynamic)
  set(VCPKG_LIBRARY_LINKAGE dynamic)
  set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
  set(VCPKG_OSX_ARCHITECTURES arm64)
  ```

- [x] **1.2** Resolve LuaJIT arm64 situation · [#1](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/1)

  Verified on Apple Silicon (2026-05-22): vcpkg overlay `luajit` port (`2023-01-04#7`, v2.1
  beta) builds for `arm64-osx` and runs. **JIT is enabled** on arm64 (`jit.arch` = `arm64`,
  LuaJIT 2.1.0-beta3) — not interpreter-only as originally assumed. Use vcpkg's custom
  port; revisit only if perf profiling shows a bottleneck.

  **Build requirement:** repo path must not contain spaces (LuaJIT `make` splits
  `PREFIX` / `LUA_ROOT`). See Notes & Decisions Log.

  **Standalone `luajit` tool:** set `DYLD_LIBRARY_PATH` to `vcpkg/installed/arm64-osx/lib`
  (vcpkg install leaves no `LC_RPATH` on the tool binary). SimpleGraphic links via CMake
  and is unaffected.

- [x] **1.3** Complete the macOS system layer · [#2](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/2)

  | File | Changes |
  |---|---|
  | `engine/system/win/sys_macos.mm` | `PlatformFindUserPath()` → `~/Library/Application Support/Path of Building 2/` |
  | `engine/system/win/sys_main.cpp` | `FindUserPath()` / `SpawnProcess()` (`posix_spawn`) on Apple; exe path via `proc_pidpath` (existing) |
  | `engine/system/win/sys_video.cpp` | `GLFW_ANGLE_PLATFORM_TYPE_METAL`; global cursor via `CGEventGetLocation` |

- [x] **1.4** Add `mac/entry.cpp` as the macOS entry point, guarded in `CMakeLists.txt` · [#3](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/3)

  `mac/entry.cpp` exports `RunLuaFileAsWin` (same symbol as Windows for the PoB host) and
  provides `main()` for standalone dev launches. CMake selects `mac/entry.cpp` when
  `APPLE`, `win/entry.cpp` on Windows, otherwise `win/entry.cpp` for Linux.

- [x] **1.5** Verify ANGLE Metal backend builds via vcpkg for `arm64-osx` · [#4](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/4)

  Verified on Apple Silicon (2026-05-22): `angle[metal]` for `arm64-osx` builds successfully.
  Installed dylibs: `liblibEGL_angle.dylib`, `liblibGLESv2_angle.dylib` (CMake targets
  `unofficial::angle::libEGL` / `libGLESv2`). Enable via `angle` + `metal` feature in
  `vcpkg.json`.

  **Triplet fix:** `triplets/arm64-osx.cmake` adds `-isystem …/usr/include/c++/v1` so
  Apple Clang finds libc++ when `-isysroot` is set (required for ANGLE and other C++ ports).

- [x] **1.6** Local smoke build · [#5](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/5)

  Verified on Apple Silicon (2026-05-22): Release build in `~/PoB-SimpleGraphic-build`
  (space-free worktree) produces `build/libSimpleGraphic.dylib` (~2 MB, arm64). Use
  vcpkg-downloaded `cmake` + `ninja` (see Notes). Manifest uses builtin `luajit` for
  `arm64-osx` (overlay port debug build still fails).

  **CMake / code fixes for macOS:** append (not overwrite) `SIMPLEGRAPHIC_PLATFORM_SOURCES`
  so `sys_macos.mm` is linked; libc++ `-isystem` for `OBJCXX`; `luasocket` → `usocket.c`;
  `FindLuaJIT` library names; `IndexUTF8ToUTF32` outside `#ifdef _WIN32`; `sys->Sleep` in
  `r_main.cpp`; `<thread>` + `std::min<size_t>` in `r_texture.cpp`; `LUA_BUILD_AS_DLL` only
  on Windows for `lua-utf8`; `base64` includes for Apple Clang.

  ```bash
  cmake -B build -S . -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_BUILD_TYPE=Release
  ninja -C build
  ```

  Success criterion: build completes without errors, `libSimpleGraphic.dylib` produced.

---

## Phase 2 — Runtime Integration

**Goal:** Make PoB-PoE2 launch on macOS from the cloned repo (dev mode).

### Tasks

- [x] **2.1** Define macOS runtime layout in the PoB-PoE2 fork · [#6](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/6) · [spec](docs/macos/issue-6-runtime-layout.md)

  Windows layout (existing):
  ```
  runtime/
    Path of Building-PoE2.exe
    SimpleGraphic.dll
    lua51.dll
    lcurl.dll, lzip.dll, socket.dll, lua-utf8.dll
  ```

  macOS layout (to add):
  ```
  runtime-macos/
    "Path of Building-PoE2"    ← Mach-O binary
    libSimpleGraphic.dylib
    liblua51.dylib
    lcurl.so / lzip.so / socket.so / lua-utf8.so
  ```

  Produced by `cmake --install` (see `cmake/macos_bundle_runtime.cmake` for vcpkg dylibs).
  Sync into PoB: `rsync -a ~/PoB-SimpleGraphic-build/runtime-macos/ /path/to/PoB/runtime-macos/`
  and symlink `runtime/SimpleGraphic` → `runtime-macos/SimpleGraphic` for fonts/assets.

- [x] **2.2** Set macOS user data directory to `~/Library/Application Support/Path of Building 2/` · [#7](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/7)

  Implemented in `engine/system/win/sys_macos.mm` (Phase 1.3).

- [ ] **2.3** Verify dev-mode launch · [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8) *(in progress — split launch reaches `PLoadModule main type=table`; **UI / `main.Init` verification pending** — see plan below)*

  ```bash
  cd /path/to/PathOfBuilding-PoE2
  ln -sfn "$(pwd)/runtime/SimpleGraphic" runtime-macos/SimpleGraphic
  ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
  ```

  macOS passes the Lua script as `argv[1]`; `sys_main.cpp` shifts args so `argv[0]` is the
  script (matching Windows host behaviour).

  **JIT:** `jit.opt.start` stubbed; `mac_jit_off()` runs before the top-level script chunk and after
  `RenderInit` (interpreter path for return capture — see decisions log 2026-05-24).

  **Progress (2026-05-24):** Split launch (`Launch.lua` + `LaunchAfterMain.lua` +
  `LaunchCallbacks.lua`; reference in `docs/macos/pob-launch/`) reaches **`PLoadModule main type=table`**
  in ~30–90 s. Engine runs AfterMain from C (`mac_run_after_main_if_requested`). **Still to verify:**
  `main.Init`, frame loop, UI. **Do not bisect Main.lua first.**

  Success criterion: UI renders, passive tree loads, basic calculations run.

- [ ] **2.4** Fix any macOS-specific Lua-side issues · [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9)

  The Lua layer should need zero changes. If issues appear, document them here.

### Next session — close [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8) (dev launch)

**Branch:** `macos-port` (or active issue branch). Sync engine → `~/PoB-SimpleGraphic-build` before every build.

**Pick up here:** Copy **`docs/macos/pob-launch/*.lua`** → PoB `src/`. Rebuild engine → install → run `Launch.lua`. Expect **`PLoadModule main type=table`** after 30–90 s; **`main.Init`** may take minutes.

| File | Role |
|------|------|
| **`Launch.lua`** | Minimal `OnInit`: dev heuristic → `RenderInit` → `PLoadModule` → `launch._mainPlm` + `launch._runAfterMain` |
| **`LaunchAfterMain.lua`** | Version `"?"`, stash-load callbacks, `main.Init`, xml, updates — **engine runs from C** after `OnInit` |
| **`LaunchCallbacks.lua`** | All other `launch:*` handlers |

**GC64 rules:** no `version* = "?"` before `PLoadModule`; no fat `OnInit` / `__mac_dofile_c` in `Launch.lua`; no `local f, err = loadfile(...)`; use `if mainPlm.err` / `mainPlm.main`.

---

#### F. Next steps (ordered)

1. **Merge / push** engine branch with `mac_run_after_main_if_requested`, `luaJIT_setmode` off, `__mac_loadfile_stash_c`.
2. **PoB PR ([#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9)):** launch split from `docs/macos/pob-launch/`.
3. **Verify:** `Launch_oninit_pload.lua` (regression) → `Launch.lua` (target); confirm UI after `main.Init`.
4. **Close [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8)** when dev launch success criteria met.
5. **Optional:** shutdown SIGBUS on exit after short scripts (separate from Main-load blocker).

---

#### A. Engine fixes landed (2026-05-24, `ui_main.cpp` + `ui_api.cpp`)

| Area | Fix |
|------|-----|
| Script load | `luaL_loadfile` uses full resolved `scriptName` path |
| `PCall` / coroutine | Skip `coroutine._list` when nil; mac `l_PCall` uses `mac_lightfunc_pcall` |
| EGL / GLFW | `libEGL.dylib` / `libGLESv2.dylib` symlinks in `runtime-macos/` |
| Window / input | Null guards (`IsActive`, `KeyEvent`, `glfwWindowShouldClose`) |
| Lua paths | `package.path` + `package.cpath`; `dlopen` preload for `.so` modules |
| LIGHTFUNC builtins | `require`, `loadfile`, `pcall`, `xpcall`, `setmetatable`, hooked `coroutine.create` + `__mac_restore_co` |
| **C return capture** | macOS `ADDFUNC` → `lua_pushcfunction`; APIs that return to Lua **stash** in `__mac_api_result` / `__mac_pload_result` / `__mac_loadmodule_result` and **return 0**; Lua wrappers re-return (`GetTime`, `GetScreenSize`, `GetScreenScale`, `LoadModule`, `PCall`, `PLoadModule`) |
| **`PLoadModule`** | C impl + `__mac_pload_c` + Lua wrapper; `mac_push_plm_result` stack index fix (`lua_setfield(L, -2, "main")`) |
| **JIT** | `luaJIT_setmode` off via C API (`mac_jit_off`); `jit.opt.start` stubbed |
| **loadfile** | `__mac_loadfile_stash_c` + `MacLoadfile` Lua wrapper |
| **AfterMain** | `mac_run_after_main_if_requested` runs `LaunchAfterMain.lua` from C after `OnInit` |

---

#### B. What passes today (`~/PoB-PoE2-build`)

| Test | Result |
|------|--------|
| `Launch_oninit_pload.lua` | `PLoadModule("Modules/Main")` OK — `main type=table` |
| `Launch_stub.lua` | Minimal top chunk + Main load (multi-minute) |
| **Split `Launch.lua`** | **`PLoadModule main type=table`** in ~30–90 s (AfterMain + Init TBD) |
| Monolithic `Launch.lua` (all callbacks in one file) | **Segfault** at Main in &lt;1 s — use split |

---

#### C. Root causes (do not re-litigate)

1. **Bytecode cannot capture C API return values** on arm64 GC64 — stash + Lua wrappers; C API `luaJIT_setmode` JIT off.
2. **`require("xml")` before Main** — breaks Main load; defer until AfterMain.
3. **`type(node)` in manifest loop** — use numeric `for i = 1, #t`.
4. **Monolithic `Launch.lua` chunk** — all `launch:*` defs before `OnInit` poison VM; split into 3 files.
5. **`version* = "?"` before `PLoadModule`** — crashes; set placeholders in `LaunchAfterMain.lua` only.
6. **Fat `OnInit` or `__mac_dofile_c` in `Launch.lua`** — same-chunk bytecode breaks `PLoadModule`; AfterMain runs from **C**.

---

#### D. Launch split (PoB repo [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9); engine [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8))

**Reference files:** `docs/macos/pob-launch/` (`Launch.lua`, `LaunchAfterMain.lua`, `LaunchCallbacks.lua`).

**Load callbacks** via stash (in `LaunchAfterMain.lua`):

```lua
__mac_loadfile_stash_c("LaunchCallbacks.lua")
local cbChunk = __mac_loadfile_chunk
local cbErr = __mac_loadfile_err
__mac_loadfile_chunk = nil
__mac_loadfile_err = nil
if cbChunk then cbChunk() end
```

**PoB macOS patches** (keep in AfterMain / callbacks):

- Numeric manifest loop; no `type(node)`.
- `devMode` via `io.open("Modules/Main.lua")` before Main.
- `pcall(require, "xml")` after Main for version strings.

---

#### E. Session workflow (every time)

**1. Rebuild and install** (space-free worktree `~/PoB-SimpleGraphic-build`):

```bash
cp "$ENGINE_REPO"/ui_api.cpp "$ENGINE_REPO"/ui_main.cpp "$ENGINE_REPO"/ui_main.h ~/PoB-SimpleGraphic-build/
ninja -C ~/PoB-SimpleGraphic-build/build
cmake --install ~/PoB-SimpleGraphic-build/build --prefix ~/PoB-PoE2-build/runtime-macos
```

**2. Staged tests** (from `~/PoB-PoE2-build`):

```bash
ln -sfn "$(pwd)/runtime/SimpleGraphic" runtime-macos/SimpleGraphic

./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_only.lua
./runtime-macos/"Path of Building-PoE2" ./src/Launch_oninit_pload.lua
./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua          # target after split
```

First full Main load may take **1–3+ minutes** — do not kill early.

**3. Debugging notes**

- Use **`ConPrintf`**, not `print()`.
- **Do not bisect `Modules/Main.lua` first** for the full-`Launch.lua` &lt;1 s crash — split the launch chunk first.
- Bisect scripts (`Launch_*.lua`) live in `~/PoB-PoE2-build/src/` only unless committed to PoB fork.
- `lldb` often hangs on this host; prefer staged `ConPrintf` traces.

**4. After #8 passes**

- [ ] Close [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8); track PoB-only quirks in [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9)
- [ ] Commit PoB `Launch.lua` / `LaunchCallbacks.lua` split + macOS manifest/xml ordering
- [ ] PoB fork: `runtime-macos/` layout ([#6](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/6))
- [ ] Defer Phase 3–4 until dev launch stable
- [ ] Optional: shutdown SIGBUS on short scripts; `require("xml")` after Main safety

**Paths:** engine `~/GitHub - Personal/PathOfBuilding-SimpleGraphic`; build `~/PoB-SimpleGraphic-build`; PoB `~/PoB-PoE2-build`. **No spaces** in engine build path.

---

## Phase 3 — .app Bundle & Distribution

**Goal:** Ship a standard macOS `.app` that a user can double-click.

### Tasks

- [ ] **3.1** Add CPack/CMake `.app` bundle config in SimpleGraphic's `CMakeLists.txt` · [#10](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/10)

  ```cmake
  if (APPLE)
    set_target_properties("Path of Building-PoE2" PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "Path of Building 2"
      MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION}"
    )
    include(BundleUtilities)
  endif()
  ```

- [ ] **3.2** Code signing strategy · [#11](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/11)

  - Initial release: ad-hoc signing (`codesign --deep --force --sign -`)
    Users see a first-launch warning; right-click → Open bypasses it.
  - Long-term: shared Apple Developer ID ($99/yr) funded by donations.
    Required for full notarization and no Gatekeeper warnings.

- [ ] **3.3** DMG packaging · [#12](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/12)

  Use `create-dmg` (available via Homebrew) in CI to produce a distributable `.dmg`.

- [ ] **3.4** Adapt auto-update system for macOS · [#13](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/13)

  - Add `[runtime-macos]` section to `manifest.cfg` in the PoB-PoE2 fork
  - `UpdateApply.lua`: add `chmod +x` call on the new binary after download
    (the Windows path skips this; omitting it on macOS causes a permission error)
  - Update server: serve macOS `.tar.gz` alongside the existing Windows `.zip`

---

## Phase 4 — CI/CD

**Goal:** Every commit to SimpleGraphic's `dev` branch builds and tests both Windows
and macOS.

### Tasks

- [ ] **4.1** Add `macos-14` (Apple Silicon) to the CI matrix in `.github/workflows/main.yml` · [#14](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/14)

  GitHub Actions provides free `macos-14` arm64 runners for public repos.

  ```yaml
  strategy:
    matrix:
      include:
        - platform: x64
          os: windows-latest
          triplet: x64-windows
        - platform: arm64
          os: macos-14
          triplet: arm64-osx
  ```

  macOS build step:
  ```yaml
  - name: Install build tools (macOS)
    if: runner.os == 'macOS'
    run: brew install cmake ninja pkg-config

  - name: Build (macOS)
    if: runner.os == 'macOS'
    run: |
      cmake -B build -S . -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_TARGET_TRIPLET=arm64-osx \
        -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_BUILD_TYPE=Release
      cmake --build build
      cmake --install build --prefix install-prefix
  ```

- [ ] **4.2** Artifact upload for macOS dylibs · [#15](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/15)

  ```yaml
  - name: Archive macOS dylibs
    if: runner.os == 'macOS'
    run: tar -czf SimpleGraphicDylibs-arm64-osx.tar.gz -C install-prefix .

  - name: Upload macOS artifact
    if: runner.os == 'macOS'
    uses: actions/upload-artifact@v4
    with:
      name: SimpleGraphic-arm64-osx
      path: SimpleGraphicDylibs-arm64-osx.tar.gz
  ```

- [ ] **4.3** Mirror the `update-simple-graphic.yml` dispatch in PoB-PoE2 · [#16](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/16)

  On SimpleGraphic release, auto-PR the macOS dylibs into PoB-PoE2's
  `runtime-macos/` alongside the existing Windows DLL PR.

- [ ] **4.4** Add macOS headless smoke-test job to PoB-PoE2 CI · [#17](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/17)

  The existing Docker/Busted test suite runs Lua logic — no changes needed.
  Add a separate macOS job that launches `HeadlessWrapper.lua` to catch
  runtime regressions.

---

## Phase 5 — Ongoing Maintenance

### Strategy

| Concern | Approach |
|---|---|
| Lua layer (`src/`) changes | Zero macOS-specific changes needed. Sync PoB-PoE2 fork with `git rebase upstream/dev` regularly. |
| SimpleGraphic upstream changes | All macOS changes are additive (`#ifdef __APPLE__` blocks, new files). Rebase onto upstream tags as they release. |
| Upstream contribution | File PRs to upstream SimpleGraphic early — they already have the `sys_macos.mm` stub, indicating interest. Getting merged upstream is the only truly sustainable path. |

### Version Compatibility Matrix

Keep this table updated as versions are tested:

| PoB-PoE2 version | SimpleGraphic version | macOS dylib tag | Tested on macOS |
|---|---|---|---|
| v0.15.0 | v2.5 | — | — |

### Long-term: Homebrew Cask

Once the `.app` + `.dmg` pipeline is stable, submit a Homebrew Cask formula.
Target command: `brew install --cask path-of-building-2`

---

## Risk Register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| LuaJIT arm64 JIT instability | Medium | Medium | v2.1 beta JIT enabled on arm64 (verified); fall back to `jit.off()` if needed; PoB bottleneck is calc logic, not tight loops |
| ANGLE Metal backend issues | Medium | High | Fall back to native OpenGL (deprecated but functional on macOS 14+) via `find_package(OpenGL)` |
| Apple code signing for auto-updates | High | Medium | Ship ad-hoc signed initially; updater prompts "re-open from Applications" on first run |
| Upstream breaking changes in SimpleGraphic | Low | High | Pin SimpleGraphic version in PoB fork; test before upgrading |
| macOS-14 CI runner availability | Low | Low | Public repo arm64 runners guaranteed by GitHub; fallback: cross-compile from x86_64 |

---

## Timeline

| Week | Deliverable |
|---|---|
| 1–2 | Phase 1 complete: SimpleGraphic builds on arm64-osx, basic window opens |
| 2–3 | Phase 2 complete: PoB dev-mode launches on macOS from cloned repo |
| 3–4 | Phase 3 complete: `.app` bundle + `.dmg` produced locally, auto-update works |
| 4–5 | Phase 4 complete: CI builds macOS artifacts on every commit |
| 6+ | Phase 5: Upstream PRs filed; Homebrew Cask submitted |

---

## Notes & Decisions Log

*Append entries here as decisions are made during development.*

- **2026-05-22** — Initial plan created. Both repos forked to `braggpd`. All macOS
  platform work scoped to SimpleGraphic; Lua layer requires zero changes.
- **2026-05-22** — LuaJIT strategy: use vcpkg overlay `luajit` port (v2.1 branch).
  Initial plan assumed interpreter-only on arm64; smoke test showed **JIT enabled**
  (LuaJIT 2.1.0-beta3, `jit.arch` = `arm64`). Acceptable for PoB's workload.
- **2026-05-22** — Phase 1.2 complete ([#1](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/1)).
  `vcpkg install luajit --triplet arm64-osx` succeeds with overlay ports/triplets.
  Interpreter smoke test: `luajit -e 'print(...)'` OK when `DYLD_LIBRARY_PATH` points at
  installed `lib/`. **Paths with spaces break the build** (Makefile splits `PREFIX` at
  whitespace — same class of issue as CONTRIBUTING.md warns for Windows). Use a clone or
  worktree without spaces (e.g. `~/PoB-SimpleGraphic-build`) for local vcpkg/cmake work
  until the port is patched or the main checkout is relocated.
- **2026-05-22** — Phase 1.3 complete ([#2](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/2)).
  User data dir on macOS uses Application Support; ANGLE Metal GLFW hint; `posix_spawn`
  for `SpawnProcess`; `PlatformOpenURL` remains in `sys_macos.mm`. Full GLFW/context
  verification waits for Phase 1.6 smoke build.
- **2026-05-22** — Phase 1.4 complete ([#3](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/3)).
  `mac/entry.cpp` shares startup with Windows via `RunSimpleGraphic()`; `RunLuaFileAsWin`
  remains the dylib export for the PoB host; `main()` supports standalone runs.
- **2026-05-22** — Phase 1.5 complete ([#4](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/4)).
  `angle[metal]` builds for `arm64-osx`; `USE_METAL=ON`. Triplet adds libc++ `-isystem` path
  (Apple Clang + `-isysroot` otherwise misses standard headers). Dylibs:
  `liblibEGL_angle.dylib`, `liblibGLESv2_angle.dylib`.
- **2026-05-22** — Phase 1.6 complete ([#5](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/5)).
  Full SimpleGraphic + Lua modules link on `arm64-osx`; artifact `libSimpleGraphic.dylib`.
  Runtime window / PoB launch is Phase 2.
- **2026-05-22** — Phase 1.1 complete. Added `triplets/arm64-osx.cmake` with
  `VCPKG_OSX_DEPLOYMENT_TARGET=13.0` (macOS Ventura, released 2022 — covers all
  M-series hardware in active use). Registered as overlay in `vcpkg-configuration.json`.
  The custom luajit port already has macOS patches and `TARGET_SYS=Darwin` support.
- **2026-05-22** — Phase 2.1–2.2 engine work on `macos/issue-6-runtime-layout` ([#6](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/6), [#7](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/7)).
  `cmake/macos_bundle_runtime.cmake` copies vcpkg dylibs into `runtime-macos/`. Host
  `pob-host` → `Path of Building-PoE2`. [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8) in progress: argv/`launchCwd` fixes,
  interpreter-only on macOS (`jit.off` + stub `jit.opt.start`). SIGBUS remains — see
  **Next session — close #8** above.
- **2026-05-22** — Merged `macos/issue-6-runtime-layout` → `macos-port`; aligned
  `docs/macos/issue-6-runtime-layout.md` with `macos_bundle_runtime.cmake` (Part B3) and
  clarified #6 enables but does not close #8.
- **2026-05-23** — Pushed `26dc2b1` on `macos-port`: macOS Lua `LoadModule`/`PLoadModule`,
  `package.path` + `package.cpath`, null window/input guards. Merged `sync-smoke` into
  `macos/issue-8-sync-smoke-merge`: LIGHTFUNC `require`/`pcall`/`xpcall`, `dlopen` preload,
  JIT on with `jit.opt.start` stub. **#8 progress:** segfault → Lua error on `sha1` precalc;
  lazy-init xor tables in PoB `runtime/lua/sha1/init.lua` fixes `require("sha1")`.
  **New blockers:** (1) LuaJIT arm64 **multi-assign** (`local a,b = …`) segfaults with
  `jit.off()` (also on `26dc2b1`); PoB `Launch.lua` needs `errMsg, main = PLoadModule(...)`.
  (2) `l_mac_pcall` LIGHTFUNC return — try `lua_resume` per `sync-smoke` notes.
- **2026-05-24** — **#8 return-capture + `PLoadModule`:** macOS C APIs stash results and Lua
  wrappers re-return (`__mac_api_result`, `__mac_pload_result`, `__mac_pload_c`). `PLoadModule(Main)`
  passes in `Launch_oninit_pload.lua`. **Full `Launch.lua` fails** because the large single chunk
  (all `launch:*` defs) is compiled before `OnInit` — **not** Main content. **`require("xml")` before
  Main** also breaks Main. **`type()` in manifest loop** segfaults. **Next:** split `Launch.lua` →
  bootstrap + `LaunchCallbacks.lua` (see **Next session — close #8**). PoB-local `Launch.lua` patches
  documented there. Interim: `Launch_stub.lua` / `Launch_oninit_pload.lua`.
- **2026-05-24 (evening)** — **Launch split + engine AfterMain:** `luaJIT_setmode` C API JIT off;
  `__mac_loadfile_stash_c` / `MacLoadfile`; `mac_run_after_main_if_requested` runs
  `LaunchAfterMain.lua` from C (must not call `__mac_dofile_c` from `Launch.lua`). Bisect:
  `versionNumber/Branch/Platform = "?"` **before** `PLoadModule` crashes; fat `OnInit` in one function
  also crashes. Reference PoB files in `docs/macos/pob-launch/`. Split `Launch.lua` reaches
  **`PLoadModule main type=table`** (~30–90 s). **#8 still open** until UI + `main.Init` verified.
