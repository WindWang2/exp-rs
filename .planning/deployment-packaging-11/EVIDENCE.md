# EVIDENCE — deployment-packaging-11

Evidence policy (goal-template D-024): every capability claim = local command + exit code,
or an explicit `not-executed` with the blocking condition. Nothing else.

## Phase 0 (audit + planning)

- `git fetch origin --prune && git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`, exit 0.
- `gh pr list --state open` → #1009 (MERGEABLE), #1008 (CONFLICTING); #991/#992 confirmed merged via `git log --decorate`.
- `gh issue list --state open` → #1001–#1007, all scientific-domain (see BASELINE §1).
- `gh pr diff 1009 --name-only` / `gh pr diff 1008 --name-only` → ownership table in PARALLEL_OWNERSHIP.md.
- Subagent #1 (read-only audit, 73 tool calls) → BASELINE §2.
- `make -n sicnu_geo_rs_cli` in main build-dev → 192 compile steps (build-strategy input; main tree not built).
- Host capability: `which cmake ninja g++ python3 pwsh docker` → all present except pwsh → **not-executed (host limitation): execution of Windows .ps1/.cmd scripts and pwsh-based conformance lane**.
- `git check-ignore -v .planning/deployment-packaging-11/GOAL.md` → no output (exit 1) after whitelist added → tracked OK (verify again post-commit).

## Phase 1 — manifest contract

(filled during Phase 1)

## Phase 2 — env_doctor module

(filled during Phase 2)

## Phase 3 — CLI surface + inventory scripts

(filled during Phase 3)

## Phase 4 — packaging surfaces + docs

(filled during Phase 4)

## Phase 5 — hardening + cleanroom

(filled during Phase 5)

## Phase 6 — validation

(filled during Phase 6)

## Phase 7 — review

(filled during Phase 7)

## Phase 8 — final

(filled during Phase 8)

## OUT_OF_SCOPE findings

- GUI has no `--offline` flag / offline-state surface; GUI (src/app) is read-only scope for
  this track (parallel-PR safety). Follow-up candidate. (BASELINE gap F)
- QNetworkAccessManager-based transports (LLM streaming client, file downloader) are not all
  routed through `offline_gate`; no test denies QNAM traffic. Crosses scientific/agent code →
  out of scope; recorded for follow-up. (BASELINE gap F)
- App version authority inconsistent (CLI `0.9.2-dev` vs GUI `1.0` vs vcpkg `1.0.0`); this
  track records versions as declared per-component in manifest `components` without forcing
  a repo-wide version unification (would touch app/cli/core — cross-track). (BASELINE)

## Phase 1 — manifest contract (commit 68e707e97b)

- `python3 tests/fixtures/bundle_manifest/conformance.py --script scripts/verify_bundle_manifest.py` → **14 checks, 0 failed, exit 0** (PS lane: `not-executed: pwsh unavailable on this host`).
- Dry-run assemble with stub build dir: `scripts/build_offline_bundle.sh --build-dir /tmp/stubbuild --out /tmp/bndl-dry --version dryrun` → builder exit 0, `BUNDLE VERIFY PASS ... (746 files, 14.5 MB / ceiling 250 MB)`, manifest `schema sicnu.offline_bundle/2`, `components {gdal 3.13.3, proj 9.8.1, geos 3.14.1, qt 6.11.2, python 3.14.7, qgis 4.2.2}`, `compat {min_reader_schema 1, bundle_kind lab-cli}`.
- Cross-directory verify (Oracle 1): `python3 scripts/verify_bundle_manifest.py /tmp/bndl-dry/...` with cwd=/ → exit 0 PASS.
- Tamper on the real bundle (`printf X >> labs/lab1/lab1_ndvi.pipeline.json`) → `BUNDLE VERIFY FAIL ... size mismatch labs/lab1/lab1_ndvi.pipeline.json: 486 != 485`, exit 1 (Oracle 1 tamper side).
- In-bundle `VERIFY.sh` on the tampered bundle → exit 1, same finding (shipped-verifier authority proven).
- `--skip-samples` leaves `data/samples/` empty → required-prefix FAIL (pre-existing contract behavior, unchanged).
- `sh -n` on both POSIX scripts → clean.
- Defect found & fixed during the phase: my first `probe_component` passed the component name into the command vector (executed `gdal gdal-config --version`, capturing usage text "GDAL"); fixed by shifting the name, first-line capture, `timeout 60` guard, and QGIS codename stripped via `sed 's/-.*//'`.

## Phase 2 — env_doctor module (commit 503b0d28ef)

- Module written: `src/geospatial/doctor/env_doctor.{h,cpp}` (namespace
  `sicnu::geo::envcheck`, envelope `exp.env.report.v1`); probes GDAL
  version/drivers/required set/GDAL_DATA, proj.db candidate scan (every
  probed path named) + operational EPSG:4326 roundtrip (CPL noise silenced),
  runtime-data marker walk, temp writability, Chinese-path roundtrip,
  offline-gate state (report-only), injectable required-driver set and PROJ
  candidates for negative tests.
- 12 `diagnostic.env.*` zh entries appended to `data/help/diagnostics.json`
  (104 → 116; `git diff --stat`: 279 insertions, 0 deletions — no reformat).
- Compile evidence lands with Phase 6 build (same shared build).

## Phase 3 — CLI surface + tests + inventory scripts

- `src/cli/cli_env_doctor.{h,cpp}` written; wired into `isCliCommand` /
  `dispatchCliCommand` (append-only) and `src/cli/CMakeLists.txt`.
- Exit contract: 0 healthy; ValidationFailure (2) degraded/broken; JSON
  envelope via `CliIO::finish` carries `data.verdict`.
- `tests/test_env_doctor.cpp` (io-test link set): self-consistency oracle
  (counts recomputed from checks), negative oracles through public API
  (nonsense required driver name → error naming it; injected nonexistent PROJ
  candidate → probed list evidence; missing SICNU_DATA_DIR → warning naming
  the path), fixture marker-walk oracle, unicode probe + cleanup assertion,
  offline state roundtrip with restore, and the diagnostic-id-existence gate
  against `data/help/diagnostics.json` parsed independently.
- `scripts/report_bundle_dependencies.py`: stdlib ELF DT_NEEDED parser →
  `dependencies.json` (`exp.bundle.deps.v1`, shipped/host/unresolved).
- Execution evidence lands with Phase 6 (binary needed).

## Phase 4 — packaging surfaces + docs

- `scripts/windows/_env.cmd`: hardwired `C:\Users\wangj.KEVIN\...` default →
  `%SICNU_WORKSPACES%`-relative fallback + loud warning when missing.
- `scripts/build_offline_bundle.cmd`: `--schema {1,2}` (default 2),
  `--check-runtime` (runs bundled `env-doctor`, fail-closed), copies
  VERIFY.sh + tools verifier, passes `-ComponentsFromBin`.
- `scripts/bundle_manifest.ps1`: `-Schema` (default 2), `-ComponentsFromBin`
  (DLL version resources), future-major refusal exit 2, v2 shape checks.
- `packaging/bundle/VERIFY.ps1`: accepts /1+/2 (was: /1 only — would have
  FAILed all new bundles), typed refusal exit 2, `min_reader_schema` check,
  optional `-Runtime` (bundled env-doctor).
- `packaging/build-appimage.sh` + `packaging/appimage_toolchain.sh` +
  `packaging/appimage-tool-checksums.txt`: pinned tags, SHA256-verified
  tools (fail-closed while PENDING), full dereferenced PROJ share, AppDir
  payload /2 manifest verified by the canonical verifier, `-j2` envelope.
- `fetch_tool` unit harness (`/tmp/test_toolchain.sh`): **6/6 checks PASS**
  (prefetched verified, PENDING refuses, missing entry refuses, tampered
  prefetched refuses, missing prefetched refuses, no artifact left behind).
- Docs: `docs/deployment/env-doctor.md` (new), `docs/deployment/MIGRATION.md`
  (new), `docs/deployment/lab-offline.md` (F19 sections), CHANGELOG entry.
- **not-executed (host limitation)**: execution of all .cmd/.ps1 surfaces
  (no Windows host, no pwsh). Static review done: PS 5.1 constructs
  (ordered dict `.Contains`, `PSCustomObject` type checks, `$LASTEXITCODE`),
  quoting, no `[IO.Path]::GetRelativePath`, no pwsh-only syntax.
- **not-executed (host limitation)**: full AppImage build (GitHub egress
  blocked in sandbox: `curl` → `TLS connect error: unexpected eof`, same as
  `git fetch` flake; linuxdeploy hashes cannot be recorded without egress —
  recorded once in `packaging/appimage-tool-checksums.txt` as PENDING and
  the builder fails closed).

## Phase 5 — cleanroom + hardening

- `scripts/cleanroom/cleanroom_smoke.sh` written (env-starve + container
  modes); `sh -n` clean. Execution evidence in Phase 6.
- `scripts/offline_smoke.sh` extended with phase 2b (in-bundle VERIFY.sh,
  dependency report + schema assertion, re-assemble + re-verify so
  `dependencies.json` is manifest-covered, env-doctor verdict line).
- Concurrency note: another local track (`exp-rs-geospatial-io-formats-11`)
  builds on this host concurrently (its own worktree/build dir, verified via
  /proc cwd). Host load 12-13/16 cores observed during our -j2 build; RSS
  headroom > 50 GB → `-j2` maintained per envelope (degrade trigger is RSS
  > 70%).
- Build strategy evidence: baseline commit for mtimes `c738e7a1b6`
  (2026-08-29 23:26, master just before the Aug-30 build) — the new-mtime set
  (913 commits, 3555 files incl. non-sources) is a strict superset of
  anything stale, so no object can be reused across an actual source change.
