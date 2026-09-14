# D13 · Baseline

- **Base commit:** `007e70cff6f43151aef6cf7e501c14bcb94a5090` (`origin/master`, latest landed: #985)
- **Worktree:** `../exp-rs-radiometric-spectral-workbench`, branch `zcode/radiometric-spectral-workbench`
- **Toolchain:** gcc `/usr/bin/c++`, Ninja, Release, ccache launcher; Qt 6, GDAL, Catch2 (vendored via CMake), jsoncpp
- **Configure:** `cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER_LAUNCHER=/usr/bin/ccache -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DSICNU_EMBED_PYTHON=ON`
- **Build/test caps:** `CMAKE_BUILD_PARALLEL_LEVEL=2`, `ninja -j2` (downgrade `-j1` if RSS > 70%), `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`

## Baseline verification

| Check | Result |
|-------|--------|
| Configure succeeds | (filled at baseline run) |
| `ninja -j2 sicnu_processing qgis_analysis sicnu_agent` | (filled at baseline run) |
| Existing kernel tests green (`test_spectral_unmixing`, `test_spectral_library`, `test_radiometric_calibration`) | (filled at baseline run) |

Baseline policy: only the targets touched by D13 are built/run (the full tree is 10k+
edges; the epic budget builds what it owns plus dependencies). Master is assumed green —
verified indirectly by reusing identical ccache config from the main checkout build.
