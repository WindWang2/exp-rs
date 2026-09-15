# MIGRATION — offline bundles, upgrades, rollback

**Status**: guidance · **Introduced by**: Deployment 11.0 (F19) · **Companion
contracts**: `packaging/OFFLINE_BUNDLE.md` (schema `/2`), `docs/deployment/env-doctor.md`.

## Principles

1. **No auto-update, ever.** The application and the bundle contain no update
   check and no phone-home (audited; see `.planning/deployment-packaging-11/
   EVIDENCE.md`). Upgrades are deliberate, operator-driven, and verifiable.
2. **Side-by-side installs.** Every bundle is a self-contained directory
   `sicnu-lab-<version>/`. Upgrading means unpacking the new directory next
   to the old one — the old bundle keeps working untouched.
3. **Verify before run.** On any machine, run the bundle's own integrity
   check (`VERIFY.cmd` / `VERIFY.ps1` / `VERIFY.sh`, or
   `tools/verify_bundle_manifest.py`) after unpacking and after any copy.
   A FAIL names the offending file — do not run a failed bundle.
4. **Env-check before first lab.** Run `bin/sicnu_geo_rs_cli env-doctor`
   (Windows: `VERIFY.ps1 -Runtime`) once per machine; a broken environment
   is reported with the exact missing item before students hit it.

## Upgrade flow (operator)

1. Unpack `sicnu-lab-<new>` next to `sicnu-lab-<old`.
2. Verify integrity of the new bundle (`VERIFY.*`; exit 0 expected).
3. Run the new bundle's `env-doctor` (`VERIFY.ps1 -Runtime` on Windows).
4. Smoke lab 1 from inside the new bundle (`RUN.cmd`, or the pipeline from
   `labs/lab1/`) against one machine.
5. Point teachers/students at the new directory; keep `<old>` until the
   course cohort is done with it.

## Rollback

Because installs are side-by-side directories, rollback is: remove the new
directory (or leave it), re-point everyone at `sicnu-lab-<old>` — whose
integrity you can re-verify at any time with its shipped verifier. There is
no shared state to roll back: bundles write outputs under their own
`output/` directories and read everything else from their own tree.

## What the manifest guarantees across versions

- `schema` — the manifest format. Readers built since F19 accept `/1` and
  `/2`; a bundle with a **newer major** schema is refused with exit 2 naming
  the supported majors (never half-verified). `compat.min_reader_schema` in
  `/2` declares the oldest reader schema the bundle's semantics need — a
  reader below that refuses with exit 2 as well.
- `bundle_version` / `created_utc` — which build the bundle came from and
  when it was assembled; the identity for side-by-side coexistence.
- `components` / `build_options` — the dependency versions and configure
  options the bundle was built against (declared provenance; structure is
  verified, values are never fabricated by the builder).
- Per-file `sha256` — tamper detection; the shipped verifier recomputes
  every file, flags unlisted files, and rejects unsafe paths (and, since
  F19, symlinks that escape the bundle).

## Settings/cache compatibility

The CLI/bundle write no machine-global settings. Per-run state lives in the
run's output directory; workflow checkpoints live in the run store under the
build/install tree (`--list-runs` lists them; see `docs/deployment/lab-offline.md`
for the offline grading flow). Crossing a bundle boundary is a fresh start
by design — there is nothing to migrate, and the manifest's per-file digests
make "which files am I actually running" verifiable at any time.

## Known limitations (honest)

- No delta/incremental upgrade: a new version is a fresh directory copy
  (the classroom bundles are sized for USB distribution; delta plumbing
  would add failure modes the offline contract does not need).
- No signed manifests: the contract is completeness, not authenticity — a
  malicious actor with filesystem access can rebuild a consistent bundle.
  The threat model is damaged/stale copies, not adversaries.
- The AppImage payload carries the same manifest family
  (`bundle_kind: appimage-payload`) but the AppImage toolchain hashes are
  maintained separately (`packaging/appimage-tool-checksums.txt`).
