# R18-C Decisions log

D1. **No `/goal-loop` skill on this host** (checked `.agents/skills` and `~/.zcode/skills`; not installed). Per task rules we do NOT fake a slash-command run; the embedded Oracle protocol in the task brief is executed natively via this ledger. `[GOAL Loop] R18-C: 2d2881631 | oracles A–G | per-stage | iter N` is printed at each stage start.

D2. **Parallelism**: `-j2` for builds, `-j1` for ctest batches (`CTEST_PARALLEL_LEVEL=1`), guarded by global host lock `/tmp/exp-rs-five-tracks-build.lock` + `resource_guard.py --max-parallel 2`. Host has 40c/62G but five tracks share it; repo docs prefer -j1/max -j2. User's -j4 is a ceiling, not a target.

D3. **Build tree**: independent `cmake -S . -B build-r18-c` (presets hardcode `build-dev` etc., which would collide with other checkouts). Config mirrors `dev-default`: Debug + `ENABLE_TESTS=ON` + `ENABLE_LOCAL_BUILD_SHORTCUTS=ON`.

D4. **Audit freshness**: read-audit agents ran on the stale local master (149 commits behind). Every candidate defect is re-verified RED against `2d2881631` in this worktree before any fix; upstream-fixed items are recorded as FALSE-POSITIVE-FIXED-UPSTREAM and dropped.

D5. **Duplication fence**: r14-C/r15-D/r15-E/r16-B fix inventories (BASELINE.md) are the do-not-duplicate list; `overlap_scan.py --include-merged 40` re-run before each stage gate.

D6. **Surgical patches only**: deepen the existing Pi → MCP → catalog → planner/repair → workflow → journal chain. No second tool catalog, no second agent framework, no GUI/map-cartography code.

D7. **Tests**: new test files prefixed `test_r18_c_*`; existing in-zone test files extended in place. Shared `tests/CMakeLists.txt` edits only to register new test targets (minimal block), flagged in PR.

D8. **Session journal/checkpoint formats**: versioned schemas stay authoritative; drift assertions land as snapshot tests, not second tables.

D9. **Reviews**: R1 by a fresh reviewer context that authored none of the patches; R2 by a second distinct context. Evidence recorded in REVIEW_R1/R2.md.

D10. **PR**: pushed only after local oracles pass; never merged by us; not blocked on remote CI (issue #1491 shows CI has been broken repo-wide since 2026-10-11 — local CTest is the authority).

D11. **Build environment** (host-specific, for reproduction): dependency prefix `/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr` with explicit GDAL/GEOS/PROJ/GSL/NETCDF overrides (recipe proven by r18-b's build cache); Catch2 offline via `-DFETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1`; heavy optional deps OFF (`SICNU_BUILD_OTB/EMBED_PYTHON/ENABLE_OPENVINO/ENABLE_TENSORRT/WITH_ONNX_RUNTIME/VENDOR_GDAL=OFF`). Vendor prefix's static `libexpat.a`/`libsqlite3.a`/`libz.a`/`libzstd.a` are not -fPIC and break the `libqgis_core.so` link; its `libexpat.so` symlink is dangling — override with SYSTEM shared libs (`-DEXPAT_LIBRARY(_RELEASE)=/usr/lib/libexpat.so -DSQLite3_LIBRARY=/usr/lib/libsqlite3.so -DZLIB_LIBRARY(_RELEASE)=/usr/lib/libz.so -DZSTD_LIBRARY=/usr/lib/libzstd.so`). Vendor `libgdal.so` references `wrapper2_*` LAPACK symbols no installed library provides → local build tree only: `-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined` (function symbols are lazy-bound; the gdal/armadillo solve path is never executed by these tests). Clean-env PATH must include `/home/kevin/.local/bin` (cmake lives there).

D12. **Runtime library path** for the built binaries (recorded for every test run):
`LD_LIBRARY_PATH=/usr/lib/pulseaudio:/usr/lib:/home/kevin/project/pw-seismic-runtime-closure-20260930-215353/vendor/prefix/usr/lib/x86_64-linux-gnu:/home/kevin/project/pw-seismic-runtime-closure-20260930-215353/vendor/prefix/usr/lib`
— order matters: vendor prefixes must NOT shadow system Qt6.12 (`Qt_6.12` versioned symbols) nor system pulsecommon (`pa_in_valgrind`). `/usr/lib/pulseaudio` must come first so the system libpulse pairs with the system pulsecommon. `QT_QPA_PLATFORM=offscreen`.

D13. **Parity comparison semantics** (C01): two discovery surfaces are asserted equal under JSON-Schema tool-parameter semantics — absent root `type` ≡ `"object"`, absent `properties` ≡ empty `properties:{}` (both are what `normalizedInputSchema()` materializes). Any other difference is drift and fails the gate. Raw registrations still missing the root type are recorded as cross-track findings (CT-1), not "fixed" by relaxing the gate.

D14. **Commit split** (atomic, reviewable): (1) C00 restoration of the 19 files lost to merge 29596c913, verbatim from a8d5cd510 with provenance note; (2) C05-a pi timeout-cancel + fake-server mode + test; (3) C09 pi id:null fanout pin + fake-server mode; (4) C03-a progress-contract fix + test + r14_C/rate-limiter domain updates; (5) C01 cursor typing + schema-parity normalization + tests + parity comparison semantics; (6) C05-b cancel-map typed keys + test; (7) C04 ghost-unlink logging. `.planning/` docs ride in a final docs commit.
