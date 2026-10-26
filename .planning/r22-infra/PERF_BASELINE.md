# R22-07 PERF_BASELINE

**N/A** — this track changes no product hot path. The only performance-adjacent
changes are:

- Test wall-clock ceilings (#1699): base values are UNCHANGED for Release
  (default scale 1.0, verified by executed harness: `base2000->2001`); scaling
  applies only under sanitizers or an explicit `SICNU_TEST_TIME_SCALE`, and the
  Release/CI-fast lane still enforces the original bases.
- Build-time: no compile-flag, unity, PCH, or dependency-graph changes (G5 not
  attempted — no fabricated baseline). The RPATH addition is link-time neutral
  (verified: full target builds unchanged, BUILD_EXIT=0).
- The lint lane's merge-base + changed-headers filter REDUCES lint cost
  (unchanged-header diagnostics suppressed); no timing baseline claimed.
