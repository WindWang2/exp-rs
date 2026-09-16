# feat(packaging): offline bundle schema /2 + canonical verifier, env-doctor, dependency inventory, cleanroom simulation (Deployment 11.0 / F19)

**Baseline**: origin/master `a5b11b7f10` ("fix: fail-closed fixes for review issues #994–#999 (#1000", 2026-09-15).
**Local evidence only; no online CI dependency.** Every capability claim below maps to a
command + exit code recorded in `.planning/deployment-packaging-11/EVIDENCE.md` (full
evidence ledger), or is explicitly marked `not-executed` with its host limitation.

## Dedupe / parallel ownership (Phase 0 audit)

- Prompt-generation snapshot was stale: PR #991/#992 are **merged** into the baseline;
  re-audited against fresh master facts.
- Open at start: #1009 (execution-runtime — touches `src/runtime/**`, `data/help/diagnostics.json`),
  #1008 (radiometric-spectral — conflicts with master independently). Both own
  `.gitignore`/`CHANGELOG.md`/`tests/CMakeLists.txt` overlaps; this track's edits there are
  strictly append-only (3-line whitelist, one changelog section, one test/ctest block).
  No file in either PR's changed set is functionally touched here.
- Open issues #1001–#1007 are all scientific-domain (io/workflow/dataset/georef/agent) —
  deduped, none belong to packaging/deployment; not touched.
- New concurrent local track `exp-rs-geospatial-io-formats-11` appeared mid-track (host
  only); no file overlap.

## What ships (work packages A–H)

- **B (deterministic manifest)** — `scripts/verify_bundle_manifest.py`: canonical verifier,
  single verify authority (dev host, tests, and the copy shipped inside every bundle).
  Accepts `sicnu.offline_bundle/1` and `/2`, refuses future majors with a typed exit-2
  refusal naming supported versions, rejects unsafe/escaping-symlink paths (files AND
  symlinked dirs), validates the `/2` sections. `/2` adds `components`, `build_options`,
  `compat` (`min_reader_schema`, `bundle_kind`) and the in-bundle Linux verifier to
  `required`. Contract updated in `packaging/OFFLINE_BUNDLE.md`. Golden conformance suite
  (`tests/fixtures/bundle_manifest/conformance.py`, 14 checks: valid /1+//2, tamper,
  missing, extra, unsafe path, empty required prefix, ceiling, future major, min_reader,
  symlink escape, PS twin lane) — oracle computes digests independently and verifies from
  another cwd.
- **A (dependency inventory)** — `scripts/report_bundle_dependencies.py` (stdlib ELF
  `DT_NEEDED` parser with implausible-header caps) writes `dependencies.json`
  (`exp.bundle.deps.v1`, shipped/host/unresolved/unparsed) into the bundle before the
  manifest hashes it; Windows twin `bundle_dependency_report.ps1` (dumpbin-optional).
  Both builders integrate it (best-effort).
- **E (first-run diagnostics)** — `sicnu_geo_rs_cli env-doctor [--json]`:
  Qt-free module `src/geospatial/doctor/env_doctor.{h,cpp}` (envelope `exp.env.report.v1`)
  probes GDAL version/required drivers (injectable set), proj.db candidate scan that names
  every probed path + operational EPSG:4326 roundtrip, runtime-data marker walk,
  temp writability, Chinese-path roundtrip, offline-gate state (report-only). CLI layer
  adds qVersion, platform-plugin discovery (QT_QPA_PLATFORM-aware, names every candidate,
  requires the actual plugin file), SSL runtime availability (QLibrary). Exit contract
  0 healthy / 2 degraded|broken (`data.verdict` distinguishes) / 6 usage. Findings map to
  12 new curated `diagnostic.env.*` entries in `data/help/diagnostics.json` (existence
  enforced by test).
- **C (Windows offline)** — `build_offline_bundle.cmd --check-runtime` runs the bundled
  CLI's env-doctor fail-closed (bundle-local `PROJ_DATA`/`GDAL_DATA`, broken-only gate);
  `-ComponentsFromBin` records DLL version resources into `components`;
  `VERIFY.ps1` accepts /1+/2, refuses newer majors, gains `-Runtime`;
  `_env.cmd` replaces the hardwired user-profile vcpkg default with a
  `SICNU_WORKSPACES`-relative fallback + loud warning.
- **D (Linux packaging)** — in-bundle `VERIFY.sh` + shipped canonical verifier
  (target-machine integrity without repo access); AppImage builder: linuxdeploy pinned to
  `1-alpha-20240109` with SHA256 verification via `packaging/appimage_toolchain.sh`
  (fail-closed while hashes are `PENDING`; `--tools-dir` offline provisioning verified
  against the same file), full dereferenced PROJ share, AppDir payload /2 manifest
  (documented as a pre-packaging snapshot), envelope-compliant `-j2`.
- **F (offline guarantee)** — env-doctor reports gate + GDAL-deny state (never toggles);
  offline smoke runs the lab from the bundle with proxy vars unset; no auto-update code
  exists (audited, unchanged).
- **G (upgrade/migration)** — `compat.min_reader_schema` forward refusal (exit 2 in all
  three readers), `docs/deployment/MIGRATION.md` (side-by-side upgrades, verify-first,
  rollback by directory, no auto-update by design).
- **H (clean-machine simulation)** — `scripts/cleanroom/cleanroom_smoke.sh`: `env-starve`
  (`env -i`, no proxy vars, bundle-only PATH, shipped verifier + env-doctor) and
  `container` (docker/podman `--network=none`) modes.

## Compatibility

- `/1` bundles keep verifying everywhere; `/2` is additive; future majors are refused with
  a typed message. The in-bundle `VERIFY.ps1` previously accepted `/1` only — it would
  have FAILed every newly built bundle; fixed here.
- Windows script line endings follow the repo convention (`*.cmd/*.ps1 text eol=crlf`).
- No new dependencies: python3 (already the POSIX builder's declared verify dep), dumpbin
  optional on Windows (typed skip), no C++ dependency changes (GDAL C API + jsoncpp +
  std::filesystem, all existing links).

## Local tests & evidence (all commands + exits in EVIDENCE.md)

- `test_env_doctor` (Catch2): **92 assertions, 8 cases, exit 0** — counts recomputed
  independently, negatives via injected options, diagnostic-id existence gate against the
  curated catalog parsed fresh.
- Conformance suite: **14 checks, 0 failed** (PS lane `not-executed`: no pwsh on host).
- CLI healthy text/`--json`; negative: `PROJ_DATA=/nonexistent` → verdict `broken`, exit 2,
  `diagnostic.env.proj_db_unusable`, injected path present in `probed[]`.
- `SICNU_OFFLINE=1` → engaged + GDAL-deny reported.
- `offline_smoke.sh` (Oracle 1+2): bundle build → canonical verify → in-bundle `VERIFY.sh`
  → dependencies.json produced AND manifest-covered → env-doctor verdict → lab 1 offline
  pipeline → single grade (RSS baseline) → 61-row batch with corrupt-row isolation and
  batch RSS ≤ 1.25× single. **exit 0**.
- Cleanroom env-starve on a pristine bundle: **exit 0** (shipped verify + env-doctor
  healthy, offline gate engaged, no proxy env, bundle-only PATH).
- Tamper: byte appended to a shipped bundle file → canonical verifier and in-bundle
  `VERIFY.sh` both FAIL (exit 1) naming the file.
- Final gate: full battery run **twice consecutively, all green** (Oracle 6); build via the
  recorded copied-build-tree strategy at `-j2` with 60 s CPU/RSS/load logs.

## Independent review

Subagent #2 adversarial review of the full diff: **P0=0, P1=4, P2=9, P3=14** — all 4 P1
fixed (AppDir prefix mismatch, Windows runtime-check PROJ gap, cleanroom set -e abort,
dependencies.json builder integration), high-value P2/P3 fixed, every remaining finding
dispositioned in `.planning/deployment-packaging-11/REVIEW_LOG.md`. Mechanical gates:
`git diff --check` clean, no conflict markers, no secrets, `.planning` tracks markdown only.

## Known limitations (honest, not silent)

- **not-executed (host)**: execution of `.cmd/.ps1` surfaces (no Windows host/pwsh) —
  static PS 5.1 review only; conformance PS lane runs on pwsh-capable hosts.
- **not-executed (host)**: full AppImage build (GitHub egress blocked in the sandbox;
  toolchain hashes stay `PENDING` and the builder fails closed until recorded once).
- Linux portable bundles ship no `libsicnu_*` shared libs (Debug artifacts exceed the
  size ceiling): `dependencies.json` names the unresolved set; Release/AppImage is the
  portability path (documented in `docs/deployment/lab-offline.md` §7).
- `data_platform_tools.cpp` fails to compile on this host's GCC 16.2.1 — **pre-existing
  master failure** (PR #1009's file, untouched here); does not affect this track's targets.
- PROJ's one-line stderr trace accompanies a failing operational resolve (not reachable
  through OSR); the typed finding is the contract surface.

## Follow-ups (out of scope here)

GUI `--offline` flag + offline-state surface; QNAM transports routing through
`offline_gate`; full PS conformance matrix; signed manifests (completeness-only today);
Release-artifact Linux lib shipping.

Reviewer notes: `data/help/diagnostics.json` +12 appended entries (ids also referenced
from C++), `tests/CMakeLists.txt` +one io-test +one add_test block, `cli_commands.cpp`
+dispatch entries, `.gitignore` +3 whitelist lines — all append-only against concurrent
PRs #1008/#1009.
