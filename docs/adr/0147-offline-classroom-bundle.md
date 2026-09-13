# ADR 0147: Offline Classroom Bundle — Deployment & Batch Grading Contract

Date: 2026-09-13 · Status: Proposed (D7) · Owners: D7 track

## Context

Teaching labs run on classroom Windows machines that are almost always
offline. Three constraints shape deployment today:

1. The README covers Linux/macOS + AppImage only; the Windows build exists as
   private `*_wb7.cmd` scripts hardwired to one teacher machine.
2. Four verification baselines (`io-range-cache`, `io-remote-range`,
   `io-remote-validator`, `bench-scale8`) time out on network-hostile hosts —
   remote I/O is never far from the critical path.
3. Grading a class means grading every student's artifact; loading all
   submissions at once would scale memory with class size, and one corrupt
   file must not cost the whole evening.

D4's ADR 0146 fixed the grading seam; D7 fixes the deployment around it.

## Decision

1. **One bundle contract** (`packaging/OFFLINE_BUNDLE.md`):
   `sicnu-lab-<version>/{bin,data,labs,RUN.cmd,GENERATE_SAMPLES.cmd,
   GRADE_ALL.cmd,README-zh.md,manifest.json}`. The manifest
   (`sicnu.offline_bundle/1`) carries per-file SHA-256 + byte sizes, a
   `required` prefix list, and a **declared size ceiling** (default 250 MB) —
   verification re-walks the bundle and fails loudly on any mismatch. The
   same contract is produced by `scripts/build_offline_bundle.{sh,cmd}` and
   consumed by the target machine, with no network at any step.

2. **Offline is a typed runtime state, not a hope.** `--offline` /
   `SICNU_OFFLINE=1` engages a process-wide gate whose single source of truth
   lives at the lowest networking layer (geospatial/remote/offline_gate) and
   is enforced at three depths: the layer's one HTTP primitive throws a typed
   refusal; a CPL config deny makes every cloud /vsi* source "not exist"
   without I/O; the remote pool / source provider return refusals instead of
   probes. Startup makes zero network calls either way. Local /vsi handlers
   (/vsimem/, /vsizip/, ...) stay usable.

3. **Batch grading streams.** `lab --batch <dir>` grades one submission at a
   time over ADR 0146's `gradeArtifact` seam and appends one UTF-8-BOM CSV row
   (`student_id,lab_id,score,verdict,top_deduction,artifact_path`) to disk
   **before** the next submission is graded: memory is bounded by the largest
   single artifact, never by class size, and a crash keeps the rows already
   written. A throwing submission is isolated as an `error` row; the run
   never aborts. Exit contract: 0 = every submission graded (even if some
   score "fail"), 1 = isolated error rows, 2 = usage.

4. **Windows-first scripts, path-independent.** `scripts/windows/` reuses the
   proven wb7 toolchain (vcvars + Ninja, MSVC, vcpkg deps) but nothing is
   hardwired to one user profile: toolchain pieces are probed via vswhere /
   well-known paths and overridable through `SICNU_*` variables. The resource
   bounds are non-negotiable (`CMAKE_BUILD_PARALLEL_LEVEL=2`,
   `CTEST_PARALLEL_LEVEL=1`, `ninja -j2`).

5. **Determinism over convenience.** Sample data is generated at bundle time
   and on the target from a fixed-seed generator shipped in `bin/` — the
   bundle never trusts a network mirror, and identical inputs grade
   identically on any machine.

## Consequences

- Teachers get an unpack-and-run path; students never touch a network stack.
- The bundle is verifiable end-to-end on any machine (`--verify`), which is
  the acceptance seam for the builder.
- A merged D1 sample foundry replaces the interim generator behind the same
  executable target name; the bundle contract does not change.
- The gate's three enforcement depths are intentionally redundant: the typed
  refusal is for humans, the CPL deny is for physics.

## References

- ADR 0146 (grading seam), `packaging/OFFLINE_BUNDLE.md` (bundle contract),
  `docs/deployment/lab-offline.md` (operator runbook).
