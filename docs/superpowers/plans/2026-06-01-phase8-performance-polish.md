# Phase 8: Performance & Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add build optimization, sanitizer/Valgrind memory leak detection, proper install rules, Linux packaging (AppImage), and basic documentation.

**Architecture:** Add CMake options for sanitizers (`-DENABLE_SANITIZERS=ON`) and optimization flags. Run existing 191 tests under ASAN to find memory bugs. Add proper `install()` rules for binary + resources + fonts + icons. Create `.desktop` file and linuxdeploy-based AppImage script. Write a README with build/usage instructions.

**Tech Stack:** CMake, AddressSanitizer, Valgrind, linuxdeploy, AppImage, freedesktop.org `.desktop` spec

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `CMakeLists.txt` | Modify | Add ENABLE_SANITIZERS option, Release optimization flags, LTO option |
| `tests/CMakeLists.txt` | Modify | Propagate sanitizer flags to test targets |
| `src/app/CMakeLists.txt` | Modify | Add install rules for binary, resources, fonts, icons, desktop file |
| `packaging/sicnu_geo_rs.desktop` | Create | Freedesktop .desktop file |
| `packaging/sicnu_geo_rs.appdata.xml` | Create | AppStream metadata |
| `packaging/build-appimage.sh` | Create | linuxdeploy-based AppImage build script |
| `README.md` | Create | Build instructions, features, screenshots |

---

### Task 1: CMake Sanitizer Support

**Goal:** Add `-DENABLE_SANITIZERS=ON` CMake option that enables ASAN + UBSAN for debug builds. Run existing tests to find memory bugs.

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add ENABLE_SANITIZERS option to top-level CMakeLists.txt**

In `CMakeLists.txt`, after the `ENABLE_TESTS` option (around line 20), add:

```cmake
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()
```

- [ ] **Step 2: Build with sanitizers and run tests**

```bash
cd /home/kevin/projects/exp-rs
rm -rf build-asan && mkdir build-asan && cd build-asan
cmake .. -DENABLE_TESTS=ON -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

Expected: Tests pass or ASAN reports specific errors to fix.

- [ ] **Step 3: Fix any ASAN/UBSAN errors found**

If ASAN reports errors (use-after-free, stack-buffer-overflow, etc.), fix each one. Common fixes:
- Dangling pointer → use `QPointer` or connect to `destroyed` signal
- Buffer overflow → bounds check
- Use-after-free → ensure object lifetime

- [ ] **Step 4: Run tests again to verify fixes**

```bash
cd build-asan && ctest --output-on-failure
```

Expected: All 191 tests pass with zero ASAN/UBSAN errors.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat(build): add ENABLE_SANITIZERS option for ASAN/UBSAN"
```

---

### Task 2: Build Optimization Flags

**Goal:** Ensure Release builds use proper optimization flags and optionally enable LTO.

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add Release optimization flags and LTO option**

In `CMakeLists.txt`, after the sanitizer block, add:

```cmake
# Release optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O2 -DNDEBUG)
    # Link-Time Optimization (optional, slower build)
    option(ENABLE_LTO "Enable Link-Time Optimization for Release builds" OFF)
    if(ENABLE_LTO)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()
```

- [ ] **Step 2: Build Release and verify**

```bash
cd /home/kevin/projects/exp-rs
rm -rf build-release && mkdir build-release && cd build-release
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

Expected: All tests pass. Binary size should be smaller than Debug.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat(build): add Release optimization flags and LTO option"
```

---

### Task 3: Install Rules

**Goal:** Add proper `install()` rules so `cmake --install` deploys binary, resources, fonts, icons, and QSS theme.

**Files:**
- Modify: `src/app/CMakeLists.txt`

- [ ] **Step 1: Add install rules to src/app/CMakeLists.txt**

After the existing `install(TARGETS sicnu_geo_rs ...)` line, add:

```cmake
# Install resources
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/fonts
        DESTINATION share/sicnu_geo_rs/resources)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/icons
        DESTINATION share/sicnu_geo_rs/resources)
install(FILES ${CMAKE_SOURCE_DIR}/resources/styles.qss
        DESTINATION share/sicnu_geo_rs/resources)
install(FILES ${CMAKE_SOURCE_DIR}/resources/icons.qrc
        DESTINATION share/sicnu_geo_rs/resources)

# Install QGIS reference data (symbology, projections)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/qgis_ref/resources
        DESTINATION share/sicnu_geo_rs/qgis_ref
        PATTERN "*.db" EXCLUDE)

# Install desktop file
install(FILES ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.desktop
        DESTINATION share/applications)

# Install app icon
install(FILES ${CMAKE_SOURCE_DIR}/packaging/sicnu_geo_rs.svg
        DESTINATION share/icons/hicolor/scalable/apps)
```

- [ ] **Step 2: Test install to staging directory**

```bash
cd /home/kevin/projects/exp-rs/build
cmake --install . --prefix /tmp/sicnu-install
ls -la /tmp/sicnu-install/bin/sicnu_geo_rs
ls -la /tmp/sicnu-install/share/sicnu_geo_rs/resources/
ls -la /tmp/sicnu-install/share/applications/sicnu_geo_rs.desktop
```

Expected: All files present in staging directory.

- [ ] **Step 3: Commit**

```bash
git add src/app/CMakeLists.txt
git commit -m "feat(build): add install rules for binary, resources, and desktop file"
```

---

### Task 4: Desktop File & App Icon

**Goal:** Create freedesktop.org `.desktop` file and a simple SVG app icon.

**Files:**
- Create: `packaging/sicnu_geo_rs.desktop`
- Create: `packaging/sicnu_geo_rs.svg`
- Create: `packaging/sicnu_geo_rs.appdata.xml`

- [ ] **Step 1: Create .desktop file**

```bash
mkdir -p /home/kevin/projects/exp-rs/packaging
```

Create `packaging/sicnu_geo_rs.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=SICNU GEO RS
GenericName=Remote Sensing Analysis
Comment=Professional remote sensing analysis platform based on QGIS
Exec=sicnu_geo_rs %F
Icon=sicnu_geo_rs
Terminal=false
Categories=Science;Geography;Education;
MimeType=image/tiff;image/geotiff;application/x-qgis-project;
Keywords=remote sensing;GIS;raster;vector;NDVI;
StartupWMClass=sicnu_geo_rs
```

- [ ] **Step 2: Create SVG app icon**

Create `packaging/sicnu_geo_rs.svg` — a simple satellite/earth icon:

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" width="64" height="64">
  <circle cx="32" cy="32" r="28" fill="#3a7f1a" stroke="#2d6313" stroke-width="2"/>
  <ellipse cx="32" cy="32" rx="28" ry="10" fill="none" stroke="#fff" stroke-width="1.5" opacity="0.6"/>
  <ellipse cx="32" cy="32" rx="10" ry="28" fill="none" stroke="#fff" stroke-width="1.5" opacity="0.6" transform="rotate(30 32 32)"/>
  <circle cx="32" cy="32" r="4" fill="#fff"/>
  <path d="M44 12 L52 4 L56 8 L48 16 Z" fill="#ffd700" stroke="#cc9900" stroke-width="1"/>
  <line x1="48" y1="16" x2="40" y2="24" stroke="#ffd700" stroke-width="1.5"/>
</svg>
```

- [ ] **Step 3: Create AppStream metadata**

Create `packaging/sicnu_geo_rs.appdata.xml`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<component type="desktop-application">
  <id>com.sicnu.geo_rs</id>
  <name>SICNU GEO RS</name>
  <summary>Professional remote sensing analysis platform</summary>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>GPL-2.0-or-later</project_license>
  <description>
    <p>
      SICNU GEO RS is a pure C++ remote sensing analysis platform built on the QGIS engine.
      It provides spectral index computation, atmospheric correction, band math, change detection,
      and integration with GDAL and OTB processing tools.
    </p>
  </description>
  <categories>
    <category>Science</category>
    <category>Geography</category>
  </categories>
  <url type="homepage">https://github.com/sicnu/geo-rs</url>
  <provides>
    <binary>sicnu_geo_rs</binary>
  </provides>
</component>
```

- [ ] **Step 4: Validate .desktop file**

```bash
desktop-file-validate /home/kevin/projects/exp-rs/packaging/sicnu_geo_rs.desktop
```

Expected: No errors (warnings are OK).

- [ ] **Step 5: Commit**

```bash
git add packaging/
git commit -m "feat(packaging): add .desktop file, SVG icon, and AppStream metadata"
```

---

### Task 5: AppImage Build Script

**Goal:** Create a shell script that uses linuxdeploy to produce an AppImage.

**Files:**
- Create: `packaging/build-appimage.sh`

- [ ] **Step 1: Create the build script**

Create `packaging/build-appimage.sh`:

```bash
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-appimage"
APPDIR="$BUILD_DIR/AppDir"

echo "=== Building SICNU GEO RS AppImage ==="

# Clean
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Build
cd "$BUILD_DIR"
cmake "$PROJECT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DENABLE_TESTS=OFF
make -j"$(nproc)"

# Install to AppDir
make install DESTDIR="$APPDIR"

# Copy resources that install() may miss
mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/resources"
cp -r "$PROJECT_DIR/resources/fonts" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp -r "$PROJECT_DIR/resources/icons" "$APPDIR/usr/share/sicnu_geo_rs/resources/"
cp "$PROJECT_DIR/resources/styles.qss" "$APPDIR/usr/share/sicnu_geo_rs/resources/"

# Copy QGIS reference data
mkdir -p "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources"
cp "$PROJECT_DIR/qgis_ref/resources/symbology-style.xml" \
   "$APPDIR/usr/share/sicnu_geo_rs/qgis_ref/resources/" 2>/dev/null || true

# Download linuxdeploy if not present
LINUXDEPLOY="$BUILD_DIR/linuxdeploy-x86_64.AppImage"
if [ ! -f "$LINUXDEPLOY" ]; then
    echo "Downloading linuxdeploy..."
    curl -L -o "$LINUXDEPLOY" \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    chmod +x "$LINUXDEPLOY"
fi

# Create AppImage
cd "$BUILD_DIR"
"$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --desktop-file "$PROJECT_DIR/packaging/sicnu_geo_rs.desktop" \
    --icon-file "$PROJECT_DIR/packaging/sicnu_geo_rs.svg" \
    --output appimage

echo "=== AppImage created in $BUILD_DIR ==="
ls -lh "$BUILD_DIR"/*.AppImage
```

- [ ] **Step 2: Make script executable**

```bash
chmod +x /home/kevin/projects/exp-rs/packaging/build-appimage.sh
```

- [ ] **Step 3: Commit**

```bash
git add packaging/build-appimage.sh
git commit -m "feat(packaging): add AppImage build script using linuxdeploy"
```

---

### Task 6: README

**Goal:** Create a README with build instructions, features, and usage.

**Files:**
- Create: `README.md`

- [ ] **Step 1: Write README**

Create `README.md`:

```markdown
# SICNU GEO RS

Professional remote sensing analysis platform built on the QGIS engine. Pure C++ (no Python at runtime).

## Features

- **Spectral Analysis:** NDVI, EVI, SAVI, NDWI, NDBI, MNDWI indices
- **Band Math:** Custom expression evaluation across raster bands
- **Atmospheric Correction:** DOS1 and DOS2 methods
- **Change Detection:** Multi-temporal image comparison
- **Mosaic:** Raster mosaic with nodata handling
- **Processing Toolbox:** 70+ algorithms (GDAL, OTB, QGIS native)
- **Layer Properties:** Raster and vector layer dialogs with statistics
- **Measurement Tools:** Geodesic distance and area measurement
- **Identify Tool:** Click-to-query pixel/feature values
- **CRS Presets:** 36 common coordinate reference systems
- **Logging:** Unified logging with file output option

## Prerequisites

- CMake 3.20+
- Qt 6.2+ (Core, Gui, Widgets, Concurrent, Network, Svg, Xml, Sql)
- GDAL 3.4+
- PROJ 8+
- GEOS 3.10+
- SQLite3, ZLIB, LibZip, ZSTD, Protobuf, CURL, PCRE2, QCA

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
./sicnu_geo_rs
```

### With Tests

```bash
mkdir build-tests && cd build-tests
cmake .. -DENABLE_TESTS=ON
make -j$(nproc)
ctest --output-on-failure
```

### With Sanitizers (Debug)

```bash
mkdir build-asan && cd build-asan
cmake .. -DENABLE_TESTS=ON -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Install

```bash
cmake --install . --prefix /usr/local
```

### AppImage

```bash
./packaging/build-appimage.sh
```

## Architecture

```
src/
├── app/           Application (main window, dialogs, widgets)
├── core/          QGIS core library (vendored)
├── gui/           QGIS GUI library (vendored)
├── native/        Platform integration
├── processing/    GDAL/OTB/QGIS algorithm providers
├── plugins/       Plugin system
└── ui/            Qt Designer forms
```

## License

GPL-2.0-or-later
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add README with build instructions and features"
```

---

### Task 7: Valgrind Run (Investigative)

**Goal:** Run existing test suite under Valgrind to detect memory leaks. Fix any real leaks found.

**Files:**
- Potentially modify: any source file with leaks

- [ ] **Step 1: Install Valgrind (if not present)**

```bash
which valgrind || sudo pacman -S valgrind
```

- [ ] **Step 2: Run tests under Valgrind**

```bash
cd /home/kevin/projects/exp-rs/build
ctest --output-on-failure -T memcheck 2>&1 | tee /tmp/valgrind-results.txt
```

Or for a single representative test:

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
    ./tests/test_band_math 2>&1 | tee /tmp/valgrind-band_math.txt
```

- [ ] **Step 3: Analyze results and fix real leaks**

Look for "definitely lost" and "indirectly lost" in output. Ignore:
- Qt internal allocations (QObject parent/child tree)
- QGIS internal allocations (singleton patterns)
- Third-party library leaks

Fix any leaks in our code (missing `delete`, missing `QObject::deleteLater`, etc.).

- [ ] **Step 4: Re-run to verify**

```bash
valgrind --leak-check=full ./tests/test_band_math 2>&1 | grep -E "definitely|indirectly"
```

Expected: Zero "definitely lost" bytes in our code.

- [ ] **Step 5: Commit if fixes were made**

```bash
git add -A
git commit -m "fix(memory): fix memory leaks found by Valgrind"
```

---

### Task 8: Full Test Suite Verification

**Goal:** Run all 191 tests in clean build, verify no regressions from Phase 8 changes.

- [ ] **Step 1: Clean build and test**

```bash
cd /home/kevin/projects/exp-rs
rm -rf build-final && mkdir build-final && cd build-final
cmake .. -DENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
```

Expected: 191/191 tests pass.

- [ ] **Step 2: Update task_plan.md**

Mark Phase 8 tasks as complete in `task_plan.md`.

- [ ] **Step 3: Commit**

```bash
git add task_plan.md
git commit -m "docs: mark Phase 8 performance & polish complete"
```

---

*Plan created: 2026-06-01*
