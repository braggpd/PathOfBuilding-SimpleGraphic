# Issue #6 — Implementation Spec: `runtime-macos` Directory Layout

**Repo where work happens:** `braggpd/PathOfBuilding-PoE2` (local: `~/repos/PathOfBuilding-PoE2`)
**Branch:** `macos-port`
**Blocks:** Issue #8 (dev-mode launch), Issue #13 (auto-update)
**Depends on:** Issue #3 (entry point binary name), Issue #2 (platform string "darwin")

---

## What to do

This issue has two parts that can be done independently:

- **Part A** — scaffolding in `braggpd/PathOfBuilding-PoE2` (do now, no blockers)
- **Part B** — CMakeLists.txt fixes in `braggpd/PathOfBuilding-SimpleGraphic` (prerequisite for Phase 1.6)

---

## Part A: PoB-PoE2 fork changes

### A1. Create `runtime-macos/README.md`

**File:** `runtime-macos/README.md`

```markdown
# runtime-macos

Populated by the macOS CI build of PathOfBuilding-SimpleGraphic (arm64-osx triplet).
Do not commit binary files here — they are produced by CI and attached to releases.

## Expected contents after build

| File | Description |
|---|---|
| `Path of Building-PoE2` | Mach-O arm64 binary |
| `libSimpleGraphic.dylib` | SimpleGraphic engine |
| `liblua51.dylib` | LuaJIT runtime |
| `lcurl.so` | Lua curl module |
| `lzip.so` | Lua zip module |
| `socket.so` | Lua socket module |
| `lua-utf8.so` | Lua UTF-8 module |
| `libEGL.dylib` | ANGLE EGL (Metal backend) |
| `libGLESv2.dylib` | ANGLE GLES2 (Metal backend) |
| `libglfw.3.dylib` | GLFW window management |
| `libcurl.dylib` | libcurl |
| `libfmt.dylib` | fmt |
| `libre2.dylib` | re2 regex |
| `libzstd.dylib` | zstd compression |
| `libwebpdecoder.dylib` | WebP decoder |
| `libz.dylib` | zlib |
| `lua/` | Pure Lua scripts (shared with `runtime/lua/`) |

## Lua module naming

Lua's `require()` resolves `lcurl` → `lcurl.so` (no `lib` prefix, `.so` extension).
CMake must set `PREFIX ""` and `SUFFIX ".so"` on all Lua module targets for macOS.
See SimpleGraphic CMakeLists.txt Part B fixes in `docs/macos/issue-6-runtime-layout.md`.

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

**File:** `CMakeLists.txt` in `braggpd/PathOfBuilding-SimpleGraphic`

These changes must land before issue #5 (smoke build) can succeed.

### B1. Fix `install(TARGETS ... RUNTIME)` for shared libraries

On macOS, `.dylib` files are the `LIBRARY` component, not `RUNTIME`.
Every `install(TARGETS X RUNTIME DESTINATION ".")` silently installs nothing on macOS.

Find every block matching this pattern and add `LIBRARY DESTINATION "."`:

```cmake
# BEFORE
install(TARGETS SimpleGraphic RUNTIME DESTINATION ".")
install(TARGETS lcurl RUNTIME DESTINATION ".")
install(TARGETS lua-utf8 RUNTIME DESTINATION ".")
install(TARGETS luasocket RUNTIME DESTINATION ".")
install(TARGETS lzip RUNTIME DESTINATION ".")

# AFTER
install(TARGETS SimpleGraphic RUNTIME DESTINATION "." LIBRARY DESTINATION ".")
install(TARGETS lcurl RUNTIME DESTINATION "." LIBRARY DESTINATION ".")
install(TARGETS lua-utf8 RUNTIME DESTINATION "." LIBRARY DESTINATION ".")
install(TARGETS luasocket RUNTIME DESTINATION "." LIBRARY DESTINATION ".")
install(TARGETS lzip RUNTIME DESTINATION "." LIBRARY DESTINATION ".")
```

### B2. Fix Lua module naming on macOS

`require("lcurl")` looks for `lcurl.so` — no `lib` prefix, `.so` extension.
CMake names shared libs `liblcurl.dylib` by default on macOS.

Add this block after the Lua module target definitions, before the first `install()`:

```cmake
if (APPLE)
    foreach(lua_target lcurl lua-utf8 luasocket lzip)
        set_target_properties(${lua_target} PROPERTIES
            PREFIX ""
            SUFFIX ".so"
        )
    endforeach()
endif()
```

Note: `luasocket` already sets `OUTPUT_NAME "socket"` — that still applies; this
just changes the prefix/suffix.

### B3. Fix transitive dependency installation on macOS

`$<TARGET_RUNTIME_DLLS:X>` expands to nothing on macOS (it's a Windows-only feature).
The `install(FILES $<TARGET_RUNTIME_DLLS:X> DESTINATION ".")` lines silently no-op.

Replace the transitive-dep install block with a macOS-aware version:

```cmake
if (APPLE)
    # Install vcpkg-managed dylibs. TARGET_RUNTIME_DLLS is Windows-only.
    set(VCPKG_LIB_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib")
    file(GLOB VCPKG_DYLIBS "${VCPKG_LIB_DIR}/*.dylib")
    install(FILES ${VCPKG_DYLIBS} DESTINATION ".")
endif()
```

Place this block after the `if (WIN32)` DLL install block, inside `else()` or a
separate `if (APPLE)` guard.

---

## Key decisions (do not change without updating MACOS_PORT.md)

| Decision | Value | Reason |
|---|---|---|
| Platform string | `"darwin"` | Matches `uname -s`; consistent with `__APPLE__` / `TARGET_OS_MAC` |
| Lua module extension | `.so` | LuaJIT's `require()` searches `?.so` before `?.dylib` |
| Lua module prefix | `""` (none) | `require("lcurl")` → `lcurl.so`, not `liblcurl.so` |
| Shared lib install component | `LIBRARY` | macOS dylibs are `LIBRARY`, not `RUNTIME` in CMake |
| Transitive deps | vcpkg `lib/*.dylib` glob | `$<TARGET_RUNTIME_DLLS>` is Windows-only |

---

## Verification

After completing Part A and Part B:

1. Run the smoke build (issue #5):
   ```bash
   cmake -B build -S . \
     -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
     -DVCPKG_TARGET_TRIPLET=arm64-osx \
     -DCMAKE_OSX_ARCHITECTURES=arm64
   cmake --build build --config Release
   cmake --install build --prefix ~/repos/PathOfBuilding-PoE2/runtime-macos
   ```

2. Verify output:
   ```bash
   ls ~/repos/PathOfBuilding-PoE2/runtime-macos/
   file ~/repos/PathOfBuilding-PoE2/runtime-macos/libSimpleGraphic.dylib
   # expect: Mach-O 64-bit dynamically linked shared library arm64
   ```

3. Verify Lua modules are named correctly:
   ```bash
   ls ~/repos/PathOfBuilding-PoE2/runtime-macos/*.so
   # expect: lcurl.so  lzip.so  lua-utf8.so  socket.so
   ```
