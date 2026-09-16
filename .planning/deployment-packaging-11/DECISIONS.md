# DECISIONS — deployment-packaging-11

D1. **Build strategy: copied build tree, not a fresh preset configure.**
    A fresh `dev-default` configure in the worktree rebuilds qgis_core (982 cpp in src/core)
    plus all sicnu libs (~2800 cpp total) at the mandated `-j2` — days of wall clock and the
    dominant failure risk of the whole track. Alternative chosen: copy main repo's
    `build-dev` (Unix Makefiles Debug, built 2026-08-30, 14 GB) into the worktree, drop
    top-level CMake cache, reconfigure with identical options against worktree sources, and
    normalize tracked-source mtimes (old for files unchanged since the Aug-30 build, new for
    master-drift + track files) so ~200 TUs rebuild instead of ~2800. Main repo `build-dev`
    is never written. Rejected: building in main `build-dev` (mixes branches in a shared
    tree); fresh Ninja preset build (correct but infeasible in budget).

D2. **Manifest schema `/2` is additive; `/1` remains valid.**
    `/2` adds optional `components` (toolchain/runtime dependency versions with source),
    `build_options` (declared configure options), `compat` (`min_reader_schema`,
    `bundle_kind`), and `VERIFY.sh` to `required`. Verifiers accept both, validate `/2`
    section shapes when present, and refuse any **future major** schema with a message
    naming supported versions (forward refusal — the G-package mechanism). Rationale:
    existing bundles in the field must keep verifying; three mirrored implementations make
    a breaking change dangerous. Alternative rejected: staying at `/1` (cannot express
    dependency inventory, compat, or build options — A/B/G packages dead).

D3. **Canonical verifier: `scripts/verify_bundle_manifest.py`, Python 3 stdlib only.**
    The three existing verifier implementations (`.sh` embedded python, `bundle_manifest.ps1`,
    in-bundle `VERIFY.ps1`) drift-prone duplicate the same rules. The `.sh` heredoc is
    replaced by delegation to the canonical file (single source on Linux/tests/in-bundle).
    The PS twin remains (Windows builders have no python3 assumption) but is constrained to
    mirror the canonical rules; cross-implementation conformance runs against identical
    golden fixtures. `pwsh` is absent on this host → the PS lane of the conformance test
    reports `not-executed` honestly and skips (recorded per evidence policy); the fixtures
    still pin the expected verdicts for any pwsh-capable host.

D4. **env-doctor lives in `src/geospatial/doctor/` (Qt-free core) + `src/cli/` (Qt checks).**
    `sicnu_geospatial` is the established Qt-free layer (GDAL+jsoncpp only) and owns
    `doctor/data_doctor.h`. env_doctor reads GDAL facts through the GDAL C API directly
    (GDALVersionInfo/GDALGetDriverCount/GDALGetDriverByName) rather than reusing
    `gdalCapabilityMatrix()`: that function returns a full per-profile capability matrix
    (create/copy/open flags for every profile), which is richer — and heavier — than the
    doctor's targeted version/driver-presence probes. (Amended during review: the original
    text wrongly claimed reuse.) Qt-dependent checks (qVersion, platform plugin discovery,
    SSL library probe via QLibrary) live in the CLI wrapper. Scope
    extension beyond the primary write list is justified because package E provably does not
    exist on master (BASELINE gap E) and cannot be implemented from scripts alone (it must
    report the runtime-resolved state of the process that will do the work). Rejected:
    PS/python-only env report (would report the *script's* environment, not the binary's).

D5. **env-doctor checks PROJ operationally, not just by file presence.**
    Two layers: (1) candidate-path scan for `proj.db` naming every path probed (diagnosis
    must point at the specific missing item — Oracle 3), (2) operational EPSG:4326
    import/export through GDAL OSR, which fails exactly when proj.db is missing/corrupt.
    Rejected: direct PROJ C API linkage (`proj_context_get_database_path`) — adds a PROJ
    include/link surface to the Qt-free geospatial lib that today reaches PROJ only through
    GDAL; OSR gives the same operational guarantee through the existing link closure.

D6. **Exit contract for `env-doctor`:** healthy=0; degraded (≥1 warning, no error) and
    broken (≥1 error) both map to `ValidationFailure`=2, with `data.verdict`
    ("degraded"/"broken") carrying the distinction (1 is deliberately not used — it is the
    bundle-verify "verified-and-failed" convention and a degraded environment is not a
    command failure); malformed arguments=invalid-input. Amended during review to match
    the shipped implementation (the original D6 text recorded degraded=1). Human text by default; `--json` emits the
    same envelope the other CLI 3.0 commands use. Findings map 1:1 to new
    `diagnostic.env.*` entries in `data/help/diagnostics.json` (appended; existing 104
    entries untouched) so prose never invents codes (DiagnosticCatalog rule).

D7. **Windows `--check-runtime` runs the bundled CLI's `env-doctor` after verify.**
    First-run diagnosis on the target machine reuses the same authority as developers get —
    no second checker. VERIFY integrity semantics unchanged; `-Runtime` is opt-in on
    VERIFY.ps1. All Windows script execution evidence is `not-executed` (no Windows host,
    no pwsh); scripts are statically checked (structure, quoting, PS 5.1 constraints) and
    their logic mirrored by the Linux-exercised paths.

D8. **AppImage: pin toolchain artifacts, do not download `-continuous`.**
    linuxdeploy + Qt plugin URLs become version-pinned with SHA256 verification and an
    optional `--tools-dir` for pre-fetched artifacts (offline reproducible). Payload
    verification reuses the canonical manifest verifier on the AppDir payload prefix. Local
    execution is `not-executed` if the sandbox has no network/where linuxdeploy is
    unavailable — recorded, not faked; static + partial-run checks still execute.

D9. **Linux bundle gains in-bundle `VERIFY.sh`** (calls the shipped canonical verifier
    copied to `tools/verify_bundle_manifest.py`): closes "verify the bundle on the target
    machine" for Linux without repo access. Declared verification dependency: python3
    (already the declared dep of the POSIX builder path). `required` for `/2` includes
    `VERIFY.sh`; `/1` bundles keep verifying unchanged.

D10. **Dependency inventory (`dependencies.json`) is a bundle file, not manifest schema.**
     The report (shipped binaries' direct library needs resolved to {shipped-in-bundle |
     host-required | unresolved}, with versions where readable) is written by
     `scripts/report_bundle_dependencies.py` into the bundle; the manifest then hashes it
     like any file, and `components` in `/2` carries the version summary. This keeps the
     manifest format stable and the inventory independently testable. Windows twin
     (`bundle_dependency_report.ps1`) prefers `dumpbin /DEPENDENTS`, degrades to a typed
     skip when the toolchain is absent.

D11. **`_env.cmd` hardwired default replaced** by `%SICNU_WORKSPACES%`-relative fallback +
     explicit warning when the fallback is used. Cannot be executed here (no Windows host);
     static review only — recorded as not-executed.

D12. **Cleanroom sim is a script, not a test binary**: `scripts/cleanroom/cleanroom_smoke.sh`
     with `--mode container|env-starve`. Container mode uses docker/podman with no-network
     flag when supported; `env-starve` mode needs no privileges (`env -i`, PATH limited to
     bundle bin + the shipped verifier's interpreter). Both reuse offline_smoke's
     zero-network discipline. Not promoted to ctest (needs docker + built tree; opt-in per
     envelope's "scale tests opt-in" rule).

D13. **Naming**: CLI command `env-doctor` (new, additive to the CLI 3.0 command list);
     C++ `sicnu::geo::envcheck` namespace in `geospatial/doctor/env_doctor.h`; test files
     `tests/test_env_doctor.cpp` (sicnu_add_test), conformance test registered as a plain
     `add_test` script test named `bundle_manifest_conformance`. No renames of existing
     symbols; schema string is the only versioned contract surface.

D14. **Skills**: verified to exist and loaded where used: `codebase-design`,
     `code-review`, `diagnosing-bugs`, `domain-modeling`, `ask-matt`, `implement-spec`,
     `implement`, `resolving-merge-conflicts` (all present under `.agents/skills/`).
     Host `goal-loop` skill: followed per GOAL's embedded protocol (`.goal-loop-ledger.md`).
