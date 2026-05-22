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

- [x] **1.1** Add `arm64-osx` vcpkg triplet file at `triplets/arm64-osx.cmake`

  ```cmake
  set(VCPKG_TARGET_ARCHITECTURE arm64)
  set(VCPKG_CRT_LINKAGE dynamic)
  set(VCPKG_LIBRARY_LINKAGE dynamic)
  set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
  set(VCPKG_OSX_ARCHITECTURES arm64)
  ```

- [ ] **1.2** Resolve LuaJIT arm64 situation

  LuaJIT's JIT compiler has no arm64 backend in stable releases. The vcpkg `luajit`
  port uses the `v2.1` beta branch, which runs in **interpreter mode** on Apple Silicon.
  Performance is acceptable for PoB (calculation logic, not tight render loops).

  Decision log: use vcpkg's default `luajit` port (v2.1 branch, interpreter-only on arm64).
  Revisit if perf profiling shows this is a bottleneck.

- [ ] **1.3** Complete the macOS system layer

  Files to audit and extend:

  | File | Current state | Work needed |
  |---|---|---|
  | `engine/system/win/sys_macos.mm` | Only `PlatformOpenURL()` (13 lines) | User data dir via `NSSearchPathForDirectoriesInDomains` |
  | `engine/system/win/sys_main.cpp` | Has `#ifdef __APPLE__` includes | Verify all `#ifdef _WIN32` paths have `#elif __APPLE__` counterparts |
  | `engine/system/win/sys_video.cpp` | GLFW-based, likely cross-platform | Verify context creation works on macOS |
  | `win/entry.cpp` | Windows `WinMain` | Add `mac/entry.mm` (or `mac/entry.cpp`) with `int main()` |

- [ ] **1.4** Add `mac/entry.cpp` as the macOS entry point, guarded in `CMakeLists.txt`

- [ ] **1.5** Verify ANGLE Metal backend builds via vcpkg for `arm64-osx`

  ANGLE on macOS targets its Metal backend. The `unofficial-angle` vcpkg port should
  handle this — verify it builds cleanly on an M-series Mac.

- [ ] **1.6** Local smoke build

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

- [ ] **2.1** Define macOS runtime layout in the PoB-PoE2 fork

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

- [ ] **2.2** Set macOS user data directory to `~/Library/Application Support/Path of Building 2/`

  Implement in `sys_main.cpp` under the existing `#elif __APPLE__ && __MACH__` guard.

- [ ] **2.3** Verify dev-mode launch

  ```bash
  ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
  ```

  Success criterion: UI renders, passive tree loads, basic calculations run.

- [ ] **2.4** Fix any macOS-specific Lua-side issues

  The Lua layer should need zero changes. If issues appear, document them here.

---

## Phase 3 — .app Bundle & Distribution

**Goal:** Ship a standard macOS `.app` that a user can double-click.

### Tasks

- [ ] **3.1** Add CPack/CMake `.app` bundle config in SimpleGraphic's `CMakeLists.txt`

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

- [ ] **3.2** Code signing strategy

  - Initial release: ad-hoc signing (`codesign --deep --force --sign -`)
    Users see a first-launch warning; right-click → Open bypasses it.
  - Long-term: shared Apple Developer ID ($99/yr) funded by donations.
    Required for full notarization and no Gatekeeper warnings.

- [ ] **3.3** DMG packaging

  Use `create-dmg` (available via Homebrew) in CI to produce a distributable `.dmg`.

- [ ] **3.4** Adapt auto-update system for macOS

  - Add `[runtime-macos]` section to `manifest.cfg` in the PoB-PoE2 fork
  - `UpdateApply.lua`: add `chmod +x` call on the new binary after download
    (the Windows path skips this; omitting it on macOS causes a permission error)
  - Update server: serve macOS `.tar.gz` alongside the existing Windows `.zip`

---

## Phase 4 — CI/CD

**Goal:** Every commit to SimpleGraphic's `dev` branch builds and tests both Windows
and macOS.

### Tasks

- [ ] **4.1** Add `macos-14` (Apple Silicon) to the CI matrix in `.github/workflows/main.yml`

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

- [ ] **4.2** Artifact upload for macOS dylibs

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

- [ ] **4.3** Mirror the `update-simple-graphic.yml` dispatch in PoB-PoE2

  On SimpleGraphic release, auto-PR the macOS dylibs into PoB-PoE2's
  `runtime-macos/` alongside the existing Windows DLL PR.

- [ ] **4.4** Add macOS headless smoke-test job to PoB-PoE2 CI

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
| LuaJIT arm64 JIT instability | Medium | Medium | Interpreter-only mode; PoB's bottleneck is calculation logic, not tight loops — acceptable perf |
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
- **2026-05-22** — LuaJIT strategy: use vcpkg default `luajit` port (v2.1 branch),
  which runs in interpreter mode on arm64. Acceptable for PoB's workload.
- **2026-05-22** — Phase 1.1 complete. Added `triplets/arm64-osx.cmake` with
  `VCPKG_OSX_DEPLOYMENT_TARGET=13.0` (macOS Ventura, released 2022 — covers all
  M-series hardware in active use). Registered as overlay in `vcpkg-configuration.json`.
  The custom luajit port already has macOS patches and `TARGET_SYS=Darwin` support.
