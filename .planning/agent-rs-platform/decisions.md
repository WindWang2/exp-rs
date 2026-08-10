# Architecture & Engineering Decisions Log (ADR Index)

## Decision Principles (from Goal Mandate)
- Default to minimal changes.
- Maximum backward compatibility.
- Best alignment with current CMake/Qt/QGIS/GDAL architecture.
- Fully automatically verifiable via CI / ctest.

## Logged Decisions

### ADR-0120: CMake Preset Standardization & GitHub Actions Layering
- **Context**: `.github/workflows` and `CMakePresets.json` were missing from the repository.
- **Decision**:
  1. Add `CMakePresets.json` at root defining `dev-default`, `ci-fast`, `ci-full`, `sanitizer-debug`, and `release-package`.
  2. Implement a 3-tiered GitHub Actions CI workflow (`.github/workflows/ci.yml`):
     - **Tier 1 (Fast Required)**: Linux GCC build, core/test targets, CTest suite, formatting & static checks.
     - **Tier 2 (Full Integration)**: Qt6 + GDAL + QGIS + Python worker + Sanitizer lane.
     - **Tier 3 (Platform Verification)**: Windows MSVC build check & macOS configure check.
- **Rationale**: Provides fast feedback on PRs while maintaining comprehensive Linux integration and platform verification without false greens or `continue-on-error`.

### ADR-0121: Windows Compiler Header & Flag Deficiencies Fix
- **Context**: Missing `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `/utf-8`, `/bigobj` in CMake cause build failures under MSVC due to macro collisions with `std::max`/`std::min` and encoding errors.
- **Decision**: Define `NOMINMAX` and `WIN32_LEAN_AND_MEAN` globally in root `CMakeLists.txt`. Pass `/utf-8 /bigobj /W4` for MSVC compilers.
- **Rationale**: Resolves Windows header pollution without mutating standard C++ template code.

### ADR-0122: Python Worker Cross-Platform IPC & Resource Lookup Alignment
- **Context**: Python IPC used UNIX sockets (`AF_UNIX`) and POSIX shared memory, failing on Windows. Python scripts were missing CMake `install()` rules.
- **Decision**:
  1. Update `worker_daemon.py` to support cross-platform IPC socket / Named Pipe fallbacks.
  2. Update `shared_memory_segment.cpp` to use cross-platform `QSharedMemory` key types.
  3. Add `install(DIRECTORY src/python/scripts DESTINATION share/sicnu_geo_rs)` to `src/python/isolated/CMakeLists.txt`.
  4. Update `app_paths.h` and `python_plugin_host.cpp` to include installed `share/sicnu_geo_rs/` candidate paths.
- **Rationale**: Ensures Python worker subsystem is production-ready across both dev trees and installed release packages on Windows and Linux.

### ADR-0123: Sanitizer Profile & Static Analysis Baseline Configuration
- **Context**: `ENABLE_SANITIZERS` in CMake lacked a dedicated CI runner, and `clang-tidy` had no root config.
- **Decision**:
  1. Add a dedicated CI job lane for `ENABLE_SANITIZERS=ON` with Catch2 test suite.
  2. Add top-level warning flags (`-Wall -Wextra -Wno-unused-parameter` for GCC/Clang) and create `.clang-tidy` for touched-code static analysis.
- **Rationale**: Prevents memory corruption, UB, and high-value warnings from regressing in core seams.
