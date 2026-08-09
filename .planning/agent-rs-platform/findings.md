# Phase 1 Production Readiness & Platform Audit Findings

## Executive Overview
A comprehensive audit of `exp-rs` build system, CI/CD, cross-platform support, runtime contracts, packaging, and static analysis was completed.

The codebase features strong C++20 core architecture and an extensive Catch2 unit test suite (>140 test executables, 1404 test cases). However, severe platform and engineering gaps exist:

1. **No CI Pipeline (`.github/workflows` missing)**: Zero automated CI integration or quality gate.
2. **Missing CMake Presets (`CMakePresets.json` missing)**: Standard presets for dev, CI, release, sanitizers, and static analysis are absent.
3. **Cross-Platform Liabilities for Windows**:
   - `NOMINMAX` and `WIN32_LEAN_AND_MEAN` are missing globally, causing `std::numeric_limits<T>::max()` compile errors under MSVC in >300 files.
   - MSVC `/utf-8` and `/bigobj` flags missing, risking encoding corruption and section overflow.
   - Posix-only socket and shared memory assumptions in Python IPC worker (`socket.AF_UNIX`, `/tmp/` paths, `shm_open`).
4. **Packaging & Resource Lookup Deficiencies**:
   - `worker_daemon.py` and Python scripts are not installed by CMake `install()` targets.
   - `app_paths.h` and `python_plugin_host.cpp` hardcode source-tree lookup paths (`../src/python/scripts/`), causing failures when executed from installed packages.
   - `build-appimage.sh` relies on dynamic curl and is not integrated into automated verification.
5. **Runtime Contracts & Concurrency Seams**:
   - TaskCenter cancellation timing and detached thread teardown require explicit contract tests to avoid dangling callbacks or thread leaks.
   - GUI callback lifetime guards need strict signal/slot thread affinity verification.
6. **Static Analysis & Sanitizer Gaps**:
   - Compiler warning flags (`-Wall -Wextra`, `/W4`) are missing.
   - `clang-tidy` integration in CMake is inactive without a root `.clang-tidy` file.
   - `ENABLE_SANITIZERS=ON` exists in CMake but has no automated runner lane.

## Detailed Area Breakdown

### Build System & Presets
- Modern CMake 3.20+ with C++20 setup.
- Support for system dependencies and vendored builds (`SICNU_VENDOR_GDAL`, `SICNU_BUILD_OTB`).
- Presets need to be added (`CMakePresets.json`) to standardize:
  - `dev-default`: Default developer debug build
  - `ci-fast`: Fast unit test build without heavy vendored OTB
  - `ci-full`: Full build including OTB/ITK and OpenCV
  - `sanitizer-debug`: ASan + UBSan instrumented build
  - `release-package`: Release build with LTO and installation check

### CI Architecture Plan
Establish 3-tiered GitHub Actions workflow:
- **Tier 1 (fast-required)**: Linux GCC compile, core unit tests, static checks, `CMakePresets.json` validation.
- **Tier 2 (full-integration)**: Qt6 + GDAL + QGIS + OpenCV + Python worker smoke lane + Sanitizer test lane.
- **Tier 3 (platform-verification)**: Windows (MSVC) build lane & macOS configure/build lane.

### Python Worker Runtime Hardening
- Add Windows IPC compatibility (Named Pipes / loopback sockets).
- Fix `install()` rules for Python scripts to `share/sicnu_geo_rs/scripts`.
- Fallback resource path resolution in `python_plugin_host.cpp`.
- Automated smoke lane testing process launcher, timeout, error propagation, and temp file cleanup.

### Cross-Platform Remediation
- Global `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `/utf-8`, `/bigobj` CMake definitions for MSVC.
- Replace string `/tmp/` concatenation with `QDir::tempPath()` across all modules.
- Guard untwine header inclusions (`<unistd.h>`, `<sys/mman.h>`).
