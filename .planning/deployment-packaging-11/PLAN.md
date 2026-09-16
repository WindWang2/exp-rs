# PLAN — deployment-packaging-11

Baseline: origin/master @ `a5b11b7f10` (2026-09-16). Full audit in BASELINE.md.

## Track deliverables mapped to work packages

| WP | Deliverable | Files |
| --- | --- | --- |
| B+A | Canonical manifest verifier (single source for Linux/tests); schema `sicnu.offline_bundle/2` (additive: `components`, `build_options`, `compat`); verify accepts /1 + /2, refuses future major; golden conformance fixtures (valid / tampered / missing / extra / unsafe-path / future-schema / ceiling) | `scripts/verify_bundle_manifest.py` (new), `packaging/OFFLINE_BUNDLE.md`, `scripts/build_offline_bundle.sh`, `scripts/bundle_manifest.ps1`, `tests/fixtures/bundle_manifest/**` (new), ctest wiring |
| A | Dependency inventory: shipped-vs-host library closure report written into the bundle (`dependencies.json`); Windows twin via dumpbin-optional PS | `scripts/report_bundle_dependencies.py` (new), `scripts/windows/bundle_dependency_report.ps1` (new), builder wiring |
| E | Environment self-check `env-doctor`: Qt-free checks (GDAL version/drivers/data dir, PROJ version/proj.db operational EPSG resolve, runtime data dirs, temp/cache writability, unicode roundtrip, offline-gate state) + CLI-layer checks (Qt version, platform plugin discovery, SSL library presence); text + `--json`; exit contract; `diagnostic.env.*` help entries | `src/geospatial/doctor/env_doctor.{h,cpp}` (new), `src/cli/cli_env_doctor.{h,cpp}` (new), `src/cli/cli_commands.{h,cpp}` (append), `data/help/diagnostics.json` (append), docs |
| C | Windows offline: builder gains `--check-runtime` (runs bundled `env-doctor` after verify); manifest gains DLL-version components; `_env.cmd` hardwired default removed; VERIFY.ps1 gains `-Runtime` switch | `scripts/windows/**`, `scripts/build_offline_bundle.cmd` |
| D | Linux packaging: AppImage builder de-networked (pinned tools + checksums + pre-fetched dir option), full PROJ share, AppDir payload verify via canonical verifier; in-bundle Linux integrity: `packaging/bundle/VERIFY.sh` + shipped verifier | `packaging/build-appimage.sh`, `packaging/bundle/VERIFY.sh` (new), `scripts/build_offline_bundle.sh` |
| F | Offline guarantee: env-doctor reports gate + GDAL-deny state; negative test (offline env-doctor makes no remote attempt, names state); no-auto-update evidence recorded | tests + EVIDENCE |
| G | Migration: manifest `compat` (min_reader_schema, bundle_kind); forward-schema refusal message naming supported versions; `docs/deployment/MIGRATION.md` (side-by-side dirs, verify-first, rollback = keep previous dir, no auto-update) | docs + verifier |
| H | Clean-machine sim: `scripts/cleanroom/cleanroom_smoke.sh` — container mode (docker/podman, no-network) + `env-starve` fallback (env -i, PATH=bundle bin); wires bundle build → verify (in another dir) → env-doctor → lab1 | `scripts/cleanroom/**` (new), `scripts/offline_smoke.sh` wiring notes |

## Phases → commits

0. planning docs + .gitignore whitelist (this commit)
1. `scripts/verify_bundle_manifest.py` + schema /2 in builders + fixtures + conformance ctest (no C++ rebuild needed)
2. `env_doctor` Qt-free module + diagnostics.json entries
3. CLI `env-doctor` command + `tests/test_env_doctor.cpp` + dependency report scripts + builder integration
4. Windows/.cmd/.ps1 surfaces + AppImage + VERIFY.sh + docs (lab-offline, env-doctor, MIGRATION) + CHANGELOG
5. cleanroom script + hardening pass (unicode/read-only/atomic writes in scripts)
6. full local validation: build, targeted tests, bundle build+verify+tamper, offline smoke, evidence
7. adversarial review (subagent #2) + remediation
8. double-run, rebase, push, PR

## Verification strategy (build)

Decision D1 (DECISIONS.md): copy main `build-dev` (Makefiles, Debug, 2026-08-30) into the
worktree, delete top-level cache, reconfigure against worktree sources with identical
options, normalize tracked-file mtimes so only (master drift since Aug 30 ≈192 TUs for the
CLI closure) + this track's new files rebuild. `-j2` hard cap, RSS logged. Main build-dev untouched.
Targets: `sicnu_geo_rs_cli`, `tools/sicnu_generate_samples`, new test binaries.
