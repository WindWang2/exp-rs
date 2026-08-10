# Production Readiness & CI Audit Matrix

| Area | Present | Automated | Blocking | Cross-platform | Gap |
| ---- | ------- | --------- | -------- | -------------- | --- |
| Configure | Yes | Local only | Yes | Partially (Linux primary) | No CI workflow (`.github/workflows` missing), no `CMakePresets.json` |
| Compile | Yes | Local only | Yes | Partially | No multi-platform CI build pipeline |
| Unit Tests | Yes | Local ctest | Yes | Linux tested | Catch2 fetched via network in configure; no CI run |
| Integration Tests | Yes | Local ctest | Yes | Linux tested | OTB / GDAL integration tests run locally only |
| GUI Seams | Yes | Local tests | Yes | Linux tested | Headless Qt offscreen verification needed in CI |
| Agent Tests | Yes | Local ctest | Yes | Linux tested | ToolCallDispatcher & MCP server tests local only |
| Runtime Probes | Yes | Local ctest | Yes | Linux tested | Execution resource probes not gated in CI |
| Python Worker | Yes | Local tests | Optional | Linux tested | No dedicated capability-detected Python worker smoke lane |
| Sanitizers | Yes (CMake) | Local manual | No | Linux GCC/Clang | `ENABLE_SANITIZERS` exists in CMake but no automated CI lane |
| Static Analysis | No | No | No | No | No clang-tidy or strict compiler warning profile configured in CI |
| Package | Yes (script) | Local manual | No | Linux AppImage | `packaging/build-appimage.sh` lacks automated CI artifact generation |
| Install | Yes (CMake) | Local manual | No | Cross-platform | CMake `install()` target not verified in CI |
| Startup Smoke | No | No | No | No | No headless app/cli startup smoke test in CI |
| Windows | Partial | No | No | Feasible (CMake MSVC setup) | `NOMINMAX`, path separators, MSVC warnings, DLL lookup unverified |
| Linux | Yes | Local | Yes | Native host | Primary dev environment, ready for containerized CI |
| macOS Feasibility | Partial | No | No | Feasible | Apple Clang compile guards and bundle rules unverified |
