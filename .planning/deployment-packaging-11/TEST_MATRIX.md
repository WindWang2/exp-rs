# TEST_MATRIX — capability → independent oracle → command → evidence

| # | Capability | Independent oracle (not the implementation) | Command | Exit | Evidence |
| --- | --- | --- | --- | --- | --- |
| T1 | Manifest verifier accepts a valid /2 bundle | golden fixture + independent sha256 recomputation in test | ctest -R bundle_manifest_conformance | 0 | Phase 1/6 |
| T2 | Tampered byte in any file → verify FAIL, file named | fixture mutated by test, expect FAIL + name | same | 0 (test asserts) | Phase 1/6 |
| T3 | Missing file / extra unlisted file / unsafe path / empty required prefix / ceiling breach → FAIL | per-fixture golden verdicts | same | 0 | Phase 1/6 |
| T4 | Future major schema → typed refusal | fixture with `sicnu.offline_bundle/3` | same | 0 | Phase 1 |
| T5 | /1 bundles still verify (backward compat) | golden /1 fixture | same | 0 | Phase 1 |
| T6 | Verify works from another directory (Oracle 1) | verifier run with cwd elsewhere on a real built bundle | scripts/... --verify <abs path> | 0 | Phase 6 |
| T7 | env-doctor healthy host → exit 0, JSON schema keys present | built CLI, --json; jq/python asserts schema | worktree CLI env-doctor --json | 0 | Phase 3/6 |
| T8 | GDAL_SKIP=GTiff → finding names GTiff (Oracle 3) | env-var-driven negative through the real binary | same with env | 2 (degraded/broken contract) | Phase 3/6 |
| T9 | bogus PROJ_DATA + hidden proj.db → finding names probed paths | env-driven | same | 2 | Phase 3/6 |
| T10 | unwritable temp → finding names path | TMPDIR pointed at read-only dir | same | 2 | Phase 3/6 |
| T11 | unicode path roundtrip check real | env-doctor's own probe (zh dir) executed | same | — | Phase 3/6 |
| T12 | offline mode: env-doctor reports engaged gate + deny; zero remote attempts | SICNU_OFFLINE=1 + loopback listener counts 0 connections | test_offline… / script | 0 | Phase 3/6 |
| T13 | C++ unit tests for env_doctor module (no CLI) | Catch2 known-answer + negative | ctest -R test_env_doctor | 0 | Phase 3/6 |
| T14 | bundle build+verify+lab from bundle, offline (Oracle 2) | offline_smoke.sh phases 1–3 | scripts/offline_smoke.sh | 0 | Phase 6 |
| T15 | clean-room starved env: bundle verify + lab under env -i | cleanroom script env-starve mode | scripts/cleanroom/cleanroom_smoke.sh | 0 | Phase 5/6 |
| T16 | tamper → in-bundle VERIFY.sh fails on a copy of the bundle | cp -r bundle, flip byte, run shipped verifier | VERIFY.sh | !=0 asserted | Phase 6 |
| T17 | Windows builder/PS surfaces | static checks (structure/quoting) only | grep/bash -n | — | not-executed: no Windows host/pwsh |
| T18 | AppImage builder pinned tools logic | bash -n + helper-function unit run with fake tools dir | bash -n; partial run | 0 | Phase 4; full run not-executed if no network |
| T19 | dependency report produces closure on built bundle | python test/oracle on fixture ELF set + real bundle | scripts/report_bundle_dependencies.py | 0 | Phase 3/6 |
| T20 | smoke passes twice (Oracle 4/6) | rerun | offline_smoke.sh ×2 | 0,0 | Phase 8 |
