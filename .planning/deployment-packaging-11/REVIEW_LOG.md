# REVIEW_LOG — deployment-packaging-11

## Phase 7 — independent adversarial review (subagent #2, read-only, 1.77M tokens)

Full diff `origin/master...HEAD` (43 files, ~3960 insertions) reviewed against all GOAL
dimensions. Verdict: **P0=0, P1=4, P2=9, P3=14**. Architecture/authority clean (canonical
verifier is the single verify authority; builder ordering correct; diagnostics.json ids
well-formed and code-gated). Main-agent self-review also ran (whitespace gate, secrets,
conflict markers).

### P1 — all fixed (commit "review remediation")

| Finding | Fix |
| --- | --- |
| AppDir manifest `required` prefixes (`bin/`...) can never match the DESTDIR `usr/...` layout → verify always failed | prefixes → `usr/bin/`, `usr/share/proj/`, `usr/share/sicnu_geo_rs/` |
| `--check-runtime` / `VERIFY.ps1 -Runtime` ran env-doctor without bundle-local PROJ_DATA/GDAL_DATA, and the doctor had no exe-relative proj.db candidate → deterministic false "broken" on healthy Windows bundles | env_doctor probes `<appDir>/../data/runtime/proj` + `../share/proj` first; both surfaces export bundle-relative PROJ_DATA/GDAL_DATA (RUN.cmd semantics); gate now fail-closed on verdict `broken` only (degraded reported, not fatal) |
| cleanroom `set -e` aborted on degraded env-doctor (rc=2) despite the documented report-only tolerance | `|| rc=$?` idiom; tolerance is live |
| dependencies.json claimed "hashed by the manifest" in six places but no builder produced it | both builders write it BEFORE the manifest (POSIX python report; Windows dumpbin-optional PS report, best-effort); offline_smoke now asserts manifest coverage instead of a vacuous re-assemble |

### P2 — dispositions

| Finding | Disposition |
| --- | --- |
| Smoke re-verify was vacuous (re-assemble deleted deps file) | FIXED (superseded by P1-4: builder writes report; smoke asserts coverage) |
| min_reader_schema refusal exit diverged (2 vs 1) across readers; MIGRATION pinned 2 | FIXED: exit 2 ("cannot verify") in canonical verifier + both PS twins + conformance scenario updated to expect 2 |
| PS twins lacked normalized containment (data/../../x escape) | FIXED: `[IO.Path]::GetFullPath` + prefix check in Test-Bundle and VERIFY.ps1 (mirrors normpath rule) |
| Canonical completeness walk skipped symlinked directories | FIXED: dir symlinks are pruned and flagged (`symlink escapes bundle` or `unlisted symlinked directory`); OFFLINE wording extended |
| Windows fs.unicode probe used path::string() (ACP convert; can throw/mojibake) | FIXED: probeWritable opens via std::filesystem paths (wide API on Windows); JSON detail uses generic_u8string; probe wrapped never-throw. Residual: options.applicationDir crosses the Qt boundary as UTF-8→narrow string (pre-existing CLI convention, ASCII install dirs) — recorded |
| D4 claimed gdalCapabilityMatrix reuse; code uses GDAL C API directly | DECISIONS D4 amended with the real rationale (capability matrix is richer/heavier than targeted probes) |
| D6 recorded degraded=1; implementation uses 2 | DECISIONS D6 amended to the shipped 0/2/2+verdict contract |
| PS conformance lane covers one scenario; docs said "identical expectations" | Docs softened to "tamper expectation" (OFFLINE_BUNDLE + conformance header); full PS matrix is future work |
| deps taxonomy split (host/system, unparsed/skipped) under one schema name | FIXED: PS twin aligned to shipped/host/unresolved/unparsed; OFFLINE_BUNDLE documents the union vocabulary |

### P3 — dispositions (fixed unless noted)

- usage exit 6 row: IMPLEMENTED (unknown env-doctor argument → InvalidInput envelope).
- qt.platform.plugins required only a directory: FIXED — requires the actual platform
  plugin (QT_QPA_PLATFORM-derived name, else platform defaults), not just `platforms/`.
- Human report via std::cout bypassing CliIO: WONTFIX (text report IS the output; CliIO
  has no stdout-line sink; documented behavior) — noted in env-doctor.md.
- Host-brittle full-driver test: FIXED (expected absence set recomputed independently via
  GDALGetDriverByName; host must merely agree with reality).
- ELF unbounded section-table read: FIXED (e_shentsize ≤ 256, e_shnum ≤ 65536 caps →
  typed "implausible section header table").
- ProgramFiles(x86) null guard: FIXED in bundle_dependency_report.ps1.
- cmd :verify -Command quoting: FIXED → -File form.
- AppDir manifest stale after linuxdeploy: DOCUMENTED (pre-packaging snapshot by design;
  the AppImage is not self-verifying, unlike the lab bundle) in build-appimage.sh.
- emitQt duplicated severity counting: FIXED — EnvDoctorReport::append() is the single
  severity-bookkeeping point; geospatial probes and the Qt layer both use it.
- symlink scenario crash without symlink privilege: FIXED (typed not-executed line).
- VERIFY.ps1 ceiling finding line: FIXED; MB formatting (Round vs %.1f) is the D7-era
  cross-impl convention — WONTFIX (would change the historical verdict line format).
- D10/deps claims now true via P1-4; deps doc text fixed (P2-9).
- libsicnu_* unresolved on Linux portable bundles: DOCUMENTED degraded (docs/deployment/
  lab-offline.md §7) — Debug artifacts exceed the size ceiling; Release/AppImage is the
  portability path. Follow-up candidate: ship Release shared libs.
