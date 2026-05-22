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

- [ ] **1.6** Local smoke build · [#5](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/5)

  ```bash
  cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DCMAKE_OSX_ARCHITECTURES=arm64
  cmake --build build --config Release
  ```

  Success criterion: build completes without errors, `libSimpleGraphic.dylib` produced.

---

## Phase 2 — Runtime Integration

**Goal:** Make PoB-PoE2 launch on macOS from the cloned repo (dev mode).

### Tasks

- [ ] **2.1** Define macOS runtime layout in the PoB-PoE2 fork · [#6](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/6)

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

  SimpleGraphic's `CMakeLists.txt` `install()` rules already produce this layout —
  just needs a macOS build run piped into this directory.

- [ ] **2.2** Set macOS user data directory to `~/Library/Application Support/Path of Building 2/` · [#7](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/7)

  Implement in `sys_main.cpp` under the existing `#elif __APPLE__ && __MACH__` guard.

- [ ] **2.3** Verify dev-mode launch · [#8](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/8)

  ```bash
  ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
  ```

  Success criterion: UI renders, passive tree loads, basic calculations run.

- [ ] **2.4** Fix any macOS-specific Lua-side issues · [#9](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/9)

  The Lua layer should need zero changes. If issues appear, document them here.

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
- **2026-05-22** — Phase 1.1 complete. Added `triplets/arm64-osx.cmake` with
  `VCPKG_OSX_DEPLOYMENT_TARGET=13.0` (macOS Ventura, released 2022 — covers all
  M-series hardware in active use). Registered as overlay in `vcpkg-configuration.json`.
  The custom luajit port already has macOS patches and `TARGET_SYS=Darwin` support.
