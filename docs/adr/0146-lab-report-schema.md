# ADR 0146: Lab report schema (`sicnu.labreport.v1`) and the desktop lab recording surface

Date: 2026-09-12. Status: Accepted. Relates to ADR 0130 (truthful states),
0137 (run identity), 0138 (lineage/bundles), 0143 (workflow→experiment
auto-recording).

## Context

The platform exported a lab report (`Project ▸ 导出实验报告...`) that dumped the
raw `RSOperationLogger` records as JSON/CSV. Meanwhile the full experiment
governance stack (runs, identity pins, lineage, replay readiness, reproduction
bundles, secret filtering) existed but was invisible to the desktop lab path —
nothing in the app ever registered an experiment run. A teacher's report could
not answer "how was this result produced?". Separately, the D5 goal requires the
report to embed a grade; the grading module (D4, `LabGradeResult`) is not merged.

## Decision

1. **One schema, three renderings.** The lab report is a single versioned
   document, `sicnu.labreport.v1` (JSON-first), built by
   `LabReportBuilder` (`src/experiment/bridge/lab_report.h`). Markdown and
   self-contained printable HTML are renderings of that one document
   (`lab_report_writers.h`); no format carries information the JSON lacks, and
   all writers refuse documents that fail `LabReportBuilder::validate`.
   Determinism is a tested contract: identical inputs plus an identical
   injected `generatedAtUtc` produce identical bytes in all three formats —
   every array is explicitly ordered, and anything that passed a hash
   container is re-sorted. The generation timestamp is the one explicitly
   non-deterministic header field.

2. **The report projects recorded truth; it never computes or invents.**
   Runs come from the `ExperimentStore` (pins, fingerprints, artifacts,
   workflow evidence); statistics are stored `MetricRecord`s verbatim;
   lineage is a `LineageGraph` slice around the primary run (or the
   experiment-store edges alone when no dataset store is wired — labelled
   `existence: "not-checked"`, never asserted); replay is
   `ReplayReadiness::assess` output verbatim, blockers included; the
   environment is the run's `RunEnvironment.redacted()`, re-redacted at the
   export boundary (issue #789 defense-in-depth).

3. **Desktop auto-recording, opt-out.** A new `LabRunRecorder`
   (`src/experiment/bridge/lab_run_recorder.h`) consumes the coordinator's
   queued `runStateChanged` and drives the same `ExperimentRunBridge` as the
   ADR 0143 monitor, so every lab execution becomes a first-class experiment
   run by default. The consent SHAPE differs from the monitor (which opts in
   per submission from surfaces that own a submission path): the desktop has
   no such seam — the session controller is deliberately untouched — so the
   recorder opts a run in at the coordinator's own authoritative Running
   event (checkpoint ghost-suppression inherited). `setRecordingEnabled(false)`
   is the opt-out: no NEW stories; already-recorded stories still close
   truthfully. A recorder is bound to ONE experiment db per opened project
   (`.sicnu/lab/experiments.db`); opening another project creates a fresh
   recorder (the bridge refuses rebinding, by design).

4. **The operation trail joins at report time, with a declared policy.**
   Operator-level records carry no execution identity (`RSOperatorContext`
   has none), so the trail cannot be bound to runs at record time without
   touching out-of-scope callers. Instead: (a) the recorder embeds a bounded
   (≤256 records, ADR 0143 budget), secret-redacted trail snapshot into the
   run's evidence (`workflow.extra.operationTrail`) via a dependency-injected
   source — the science-side target never links the operator stack; (b) the
   report presents the trail as `steps[]`, each carrying
   `attribution{policy: "time-window+operator-name", quality}`. Windows that
   match exactly attribute; outside windows are `unattributed`; ambiguous
   windows are marked and never silently assigned.

5. **Grade embedding is a typed seam.** `grade.status` is `recorded` (with a
   required `gradingRef` + inline copy — validated, so an inline score can
   never be orphaned from its authority) or `unavailable` (with a reason).
   Until D4's `LabGradeResult` merges, every report is `unavailable`. The
   builder never fabricates a score.

6. **Thumbnails are bounded at the schema boundary.** Callers produce
   thumbnails through the existing bounded raster preview path
   (`renderRasterPreview`, overview-backed, no upsampling); the builder
   re-derives the real pixel size from the PNG IHDR and refuses anything over
   512 px on the long edge or not a PNG — with a warning in the document,
   never a silent shrink or a full-resolution embed. The printable HTML is a
   single self-contained file (inline A4 CSS, inline data URLs, no external
   resources).

## Consequences

- A lab report can always answer "how was this produced": registered run
  identity + lineage + replay blockers (explicitly listed) + operation steps
  with a declared attribution policy.
- `导出实验报告` no longer writes raw logger dumps; the trail is embedded in
  the report (`steps[]`) and in each run's evidence. The opt-out user gets an
  explicit "no experiment runs registered" message instead of a file.
- Reporting with no dataset store wired degrades honestly (unknown dataset
  checks, `best_effort` at best; experiment-edge lineage labelled as
  existence-unchecked). Wiring dataset pins into the desktop lab path remains
  open work for a future track.
- New files live in `src/experiment/bridge/` (GUI-free, layer-guarded) and
  `src/app/main_window_project.cpp` (the only GUI touchpoint); the workflow
  session controller is untouched.
