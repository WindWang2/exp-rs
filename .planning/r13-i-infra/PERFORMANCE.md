# R13-I Performance notes

- build-r13-i configure (Ninja, Debug, ENABLE_TESTS=ON, ccache): 54s cold, 33.6s re-run.
- ctest -N (992 tests): <0.3s per invocation, byte-identical across runs.
- run_clang_tidy_changed.sh on 1 changed test file (clang-tidy 23, Debug compile DB): ~3.4s.
- check_test_registration.py: ~5s static mode, ~10s with build-dir (56MB compile_commands.json parse).
- collect_readiness.py 46-capability fixture run: <0.2s per invocation.
- Global build lock observed working: R13-G (14:01) and R13-H (14:04) queued/ahead; this track's m1_2 build queued 14:17, serialized as designed.

## Final build timings (this host, -j2 under the global lock)
- m1_2 chain (sicnu_platform+runtime+harness): 25 steps (warm ccache), queue wait dominated by R13-G/H ahead.
- fixture 4-target chain incl. qgis_core/gui + geospatial: 3595 steps, ~78 min wall (15:33-16:51).
- sicnu_geo_rs_cli chain: blocked by #1499 (sicnu_python_isolated fails on master).
- test runs: m1_2 0.31/0.35s; remote_source_cache ~1s; execution_benchmarks ~25s; io_identity ~4s; io_microbench ~1s; census lane 3.2-4.1s.
