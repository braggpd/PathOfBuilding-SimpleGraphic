# Issue #6 — Implementation Spec: `runtime-macos` Directory Layout

**Repo where work happens:** `braggpd/PathOfBuilding-PoE2` (local: `~/repos/PathOfBuilding-PoE2` or `~/PoB-PoE2-build`)
**Branch:** `macos-port` (SimpleGraphic); PoB fork `macos-port` for Part A
**Enables:** Issue #8 (dev-mode launch — correct install layout and module names)
**Does not fix:** Issue #8 SIGBUS during `Launch.lua` (separate engine/Lua debug track)
**Depends on:** Issue #3 (entry point binary name), Issue #2 (platform string "darwin")

## Status (2026-05-22)

| Part | Status |
|------|--------|
| **A** PoB scaffolding | Done in PoB fork (`runtime-macos/README.md`, `manifest.cfg` / `manifest.xml`) |
| **B** SimpleGraphic CMake | **Merged to `macos-port`** — see `cmake/macos_bundle_runtime.cmake`, `pob-host`, per-target `.so` / `LIBRARY` install |

---

## What to do

This issue has two parts that can be done independently:

- **Part A** — scaffolding in `braggpd/PathOfBuilding-PoE2` (do now, no blockers)
- **Part B** — CMakeLists.txt fixes in `braggpd/PathOfBuilding-SimpleGraphic` (**done on `macos-port`**)

---

## Part A: PoB-PoE2 fork changes

### A1. Create `runtime-macos/README.md`

**File:** `runtime-macos/README.md`

Document expected binaries after `cmake --install`. Versioned dylib names from vcpkg are normal, e.g.
`libluajit-5.1.2.1.0.dylib`, `liblibEGL_angle.dylib`, `liblibGLESv2_angle.dylib` (not the
idealized short names in the table below).

```markdown
# runtime-macos

Populated by the macOS CI build of PathOfBuilding-SimpleGraphic (arm64-osx triplet).
Do not commit binary files here — they are produced by CI and attached to releases.

## Expected contents after build

| File | Description |
|---|---|
| `Path of Building-PoE2` | Mach-O arm64 binary |
| `libSimpleGraphic.dylib` | SimpleGraphic engine |
| `libluajit-*.dylib` | LuaJIT runtime (vcpkg versioned name) |
| `lcurl.so` | Lua curl module |
| `lzip.so` | Lua zip module |
| `socket.so` | Lua socket module |
| `lua-utf8.so` | Lua UTF-8 module |
| `liblibEGL_angle.dylib` | ANGLE EGL (Metal backend) |
| `liblibGLESv2_angle.dylib` | ANGLE GLES2 (Metal backend) |
| `libglfw.*.dylib` | GLFW window management |
| `libcurl.*.dylib` | libcurl (+ OpenSSL deps as needed) |
| `libfmt.*.dylib` | fmt |
| `libre2.*.dylib` | re2 regex |
| `libzstd.*.dylib` | zstd compression |
| `libwebpdecoder.*.dylib` | WebP decoder |
| `libz.*.dylib` | zlib |
| `lua/` | Pure Lua scripts (shared with `runtime/lua/`) |

## Lua module naming

Lua's `require()` resolves `lcurl` → `lcurl.so` (no `lib` prefix, `.so` extension).
CMake must set `PREFIX ""` and `SUFFIX ".so"` on all Lua module targets for macOS.
See Part B in this spec and `CMakeLists.txt` on `macos-port`.

## Platform string

The updater identifies this platform as `"darwin"` (matches `uname -s`).
This string is set in `sys_main.cpp` and read from `manifest.xml` at runtime.
```

### A2. Create `runtime-macos/lua/` symlink placeholder

The `lua/` subdirectory contains pure Lua scripts shared with the Windows runtime.
Create a `.gitkeep` so the directory exists in the repo:

**File:** `runtime-macos/lua/.gitkeep` (empty file)

The CI pipeline will populate this by copying from `runtime/lua/` during the macOS build.

### A3. Update `manifest.cfg`

**File:** `manifest.cfg`

Add this section after the existing `[runtime]` section:

```ini
[runtime-macos]
path = runtime-macos
exclude-files = lua-profiler.lua,SimpleGraphic.cfg
exclude-directories =
```

No `Update.exe` (no Windows updater binary on macOS).
No MSVC runtime DLLs (`msvcp*.dll`, `vcruntime*.dll`, `msvcr100.dll`).

### A4. Update `manifest.xml`

**File:** `manifest.xml`

Add one line after the existing `win32` source line:

```xml
<Source part="runtime" platform="win32" url="https://raw.githubusercontent.com/PathOfBuildingCommunity/PathOfBuilding-PoE2/{branch}/runtime/" />
<Source part="runtime" platform="darwin" url="https://raw.githubusercontent.com/braggpd/PathOfBuilding-PoE2/{branch}/runtime-macos/" />
```

When runtime file entries are generated into `manifest.xml` (by the release tooling),
each macOS runtime file must carry `platform="darwin"` so `UpdateCheck.lua` line 158
only downloads them on macOS:

```lua
if not node.attrib.platform or node.attrib.platform == localPlatform then
```

---

## Part B: SimpleGraphic CMakeLists.txt fixes

**File:** `CMakeLists.txt` in `braggpd/PathOfBuilding-SimpleGraphic` — **implemented on `macos-port`**.

These changes are required for `cmake --install` to produce a usable `runtime-macos/` tree.

### B1. Fix `install(TARGETS ... RUNTIME)` for shared libraries

On macOS, `.dylib` files are the `LIBRARY` component, not `RUNTIME`.
`install(TARGETS X RUNTIME DESTINATION ".")` alone installs nothing for dylibs.

On `macos-port`, Apple targets use `install(TARGETS … LIBRARY DESTINATION ".")` and the
host launcher uses `RUNTIME`:

```cmake
if (APPLE)
    install(TARGETS SimpleGraphic LIBRARY DESTINATION ".")
    install(TARGETS ${PoB_HOST_TARGET} RUNTIME DESTINATION ".")
else ()
    install(TARGETS SimpleGraphic RUNTIME DESTINATION ".")
endif ()
```

(`PoB_HOST_TARGET` is `pob-host` with `OUTPUT_NAME "Path of Building-PoE2"`.)

### B2. Fix Lua module naming on macOS

`require("lcurl")` looks for `lcurl.so` — no `lib` prefix, `.so` extension.

Per-target on `macos-port`:

```cmake
if (APPLE)
    set_target_properties(lcurl PROPERTIES PREFIX "" SUFFIX ".so")
endif ()
```

(same pattern for `lua-utf8`, `luasocket`, `lzip`; `luasocket` keeps `OUTPUT_NAME "socket"`.)

### B3. Fix transitive dependency installation on macOS

`$<TARGET_RUNTIME_DLLS:X>` is Windows-only and no-ops on macOS.

**Do not** use a blind `file(GLOB …/lib/*.dylib)` — that copies the entire vcpkg lib tree.

On `macos-port`, use `cmake/macos_bundle_runtime.cmake` installed via `install(SCRIPT …)`:

- `file(GET_RUNTIME_DEPENDENCIES)` on the built engine, host, and Lua modules
- Copies only resolved transitive dylibs next to the install prefix
- Excludes `/System/` and `/usr/lib/`

Relevant `CMakeLists.txt` tail:

```cmake
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_bundle_runtime.cmake"
    "${_macos_bundle_script}"
    @ONLY
)
install(SCRIPT "${_macos_bundle_script}")
```

Also set `BUILD_RPATH` / `INSTALL_RPATH` to `@executable_path;@loader_path` for engine, host, and Lua modules.

---

## Key decisions (do not change without updating MACOS_PORT.md)

| Decision | Value | Reason |
|---|---|---|
| Platform string | `"darwin"` | Matches `uname -s`; consistent with `__APPLE__` / `TARGET_OS_MAC` |
| Lua module extension | `.so` | LuaJIT's `require()` searches `?.so` before `?.dylib` |
| Lua module prefix | `""` (none) | `require("lcurl")` → `lcurl.so`, not `liblcurl.so` |
| Shared lib install component | `LIBRARY` | macOS dylibs are `LIBRARY`, not `RUNTIME` in CMake |
| Transitive deps | `macos_bundle_runtime.cmake` | Dependency closure, not full vcpkg glob |
| Dev host binary | `Path of Building-PoE2` | Matches Windows updater/runtime naming ([#3](https://github.com/braggpd/PathOfBuilding-SimpleGraphic/issues/3)) |

---

## Verification

After Part A and a Release build on `macos-port`:

1. Build and install:
   ```bash
   cd ~/PoB-SimpleGraphic-build   # path must not contain spaces
   git checkout macos-port
   ninja -C build
   cmake --install build --prefix ~/PoB-PoE2-build/runtime-macos
   ```

2. Verify output:
   ```bash
   ls ~/PoB-PoE2-build/runtime-macos/
   file ~/PoB-PoE2-build/runtime-macos/libSimpleGraphic.dylib
   # expect: Mach-O 64-bit dynamically linked shared library arm64
   ```

3. Verify Lua modules:
   ```bash
   ls ~/PoB-PoE2-build/runtime-macos/*.so
   # expect: lcurl.so  lzip.so  lua-utf8.so  socket.so
   ```

4. Dev launch (Issue #8 — may still SIGBUS until engine/Lua crash is fixed):
   ```bash
   cd ~/PoB-PoE2-build
   ln -sfn "$(pwd)/runtime/SimpleGraphic" runtime-macos/SimpleGraphic
   ./runtime-macos/"Path of Building-PoE2" ./src/Launch.lua
   ```
