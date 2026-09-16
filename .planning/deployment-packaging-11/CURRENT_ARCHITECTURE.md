# CURRENT_ARCHITECTURE — authority/seam map (Phase 0 state, master a5b11b7f10)

## Deployment/packaging domain

```
packaging/OFFLINE_BUNDLE.md            <- normative bundle contract (schema sicnu.offline_bundle/1)
        ^ consumed by
scripts/build_offline_bundle.sh  —— twin —— scripts/build_offline_bundle.cmd
        |  (verify: embedded python heredoc)      | (manifest: bundle_manifest.ps1; verify: PS Test-Bundle)
        v                                          v
   manifest.json  <===== 3 mirrored writer/verifier implementations (drift risk; no conformance gate)
        |
packaging/bundle/{RUN,GENERATE_SAMPLES,GRADE_ALL,VERIFY}.cmd + VERIFY.ps1 + README  <- in-bundle surface
packaging/build-appimage.sh           <- legacy AppImage (network downloads, no verify)

scripts/offline_smoke.sh               <- acceptance smoke (manual, unwired)
scripts/windows/_env.cmd               <- shared toolchain probe (hard bounds -j2)
cmake/SicnuLabProfile.cmake            <- offline configure profile (wired)
cmake/{Bundle,VcpkgToolchain,VcpkgInstallDeps,sicnu_otb_bundle}.cmake  <- DEAD (not included)
```

## Runtime diagnostics domain

```
src/geospatial/remote/offline_gate.*   <- OFFLINE AUTHORITY (setEnabled/enabledFromEnv/isRemoteTarget/
        ^                                 applyGdalNetworkDeny; typed refusal; atomic flag)
        |
src/data/offline_mode.*                <- Qt facade over the gate (do not add a third spelling)
src/geospatial/doctor/data_doctor.*    <- per-dataset doctor (findings pattern) + gdalCapabilityMatrix()
src/runtime/observability/diagnostic_report.h <- exp.diag.v1 envelope (single failure envelope)
src/help/diagnostic_catalog.h + data/help/diagnostics.json (104 curated zh entries; qrc in both binaries)
src/cli/main_cli.cpp                   <- global flags (--offline strip), QgsApplication bootstrap
src/cli/cli_commands.{h,cpp}           <- CLI 3.0 command registry (isCliCommand/dispatchCliCommand, CliIO)
```

## Seams this track consumes (not duplicates)

- `gdalCapabilityMatrix()` for GDAL version/driver facts (env-doctor GDAL section)
- `offline_gate::{enabled, enabledFromEnv, applyGdalNetworkDeny}` for the offline-state report
- `CliIO::finish` + `exprs::ExitCode` for the CLI envelope/exit contract
- `data/help/diagnostics.json` + `diagnostic.<family>.<code>` id scheme for prose
- `scripts/bundle_manifest.ps1` Get-RelPath/Test-Bundle conventions (PS 5.1 constraints)
- `exp.diag.v1` / `DoctorReport {check,severity,message,detail}` shapes for report records
