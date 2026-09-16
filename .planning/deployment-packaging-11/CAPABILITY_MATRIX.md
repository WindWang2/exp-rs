# CAPABILITY_MATRIX — before/after

Legend: implemented / degraded (works with declared limits) / not-supported (documented refusal).

| Capability | Before (a5b11b7f10) | After (this track) |
| --- | --- | --- |
| Bundle integrity verify on target machine | Windows only (VERIFY.cmd/ps1) | + Linux in-bundle `VERIFY.sh` + shipped canonical verifier |
| Manifest schema expressiveness | `/1` (files+required+ceiling) | + `/2`: components, build_options, compat; `/1` still accepted |
| Future-schema refusal | none (string mismatch → generic fail) | typed refusal naming supported majors |
| Manifest conformance gate | none (3 drifting impls) | golden fixtures + conformance test (python lane; ps lane not-executed: no pwsh) |
| Dependency inventory (build side) | none | `dependencies.json` in bundle (shipped vs host-required closure) + `components` versions (Windows DLL version resources; Linux ELF parse) |
| First-run env self-check | none | `env-doctor` CLI command (text+json): Qt/GDAL/PROJ/runtime-data/temp/cache/unicode/offline-state/SSL/platform-plugin |
| Missing-item diagnosis (Oracle 3) | loader dialogs / raw GDAL errors | findings name the exact missing driver/db/lib/path + remediation ids |
| Offline guarantee surface | CLI `--offline` + gate + deny (existing, unchanged) | + env-doctor reports gate & deny state; negative tests |
| No silent update check | true (no code exists) | unchanged + audited evidence |
| Upgrade/migration guidance | none | manifest `compat` + `docs/deployment/MIGRATION.md` (side-by-side, rollback) |
| Clean-machine simulation | none | `scripts/cleanroom/cleanroom_smoke.sh` (container / env-starve) |
| Windows runtime check in builder | none | `--check-runtime` runs bundled env-doctor (not-executed locally: no Windows host) |
| AppImage reproducibility | `-continuous` network downloads | pinned URLs + SHA256 + `--tools-dir` offline mode (execution not-executed if no network; static checks run) |
