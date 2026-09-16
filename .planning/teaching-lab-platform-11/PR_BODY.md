# PR_BODY — feat(labs): teaching lab, grading & reproducibility platform 11.0

(drafted at the end; keeps required sections: baseline SHA, dedupe/ownership,
architecture decisions, delivered capabilities, compatibility, local tests,
resource evidence, review findings, known limitations, follow-ups,
"Local evidence only; no online CI dependency")

## Baseline

`origin/master@a5b11b7f10fa010c1c060864fb427d777ba9a4aa` (2026-09-16; PRs
#991/#992 already merged into it, re-audited as master fact).

## Dedupe / parallel ownership (at start)

- Open PR #1009 (execution runtime) and #1008 (radiometric/spectral): zero
  changed-file overlap with this track's business files; shared integration
  files (`tests/CMakeLists.txt`, `src/*/CMakeLists.txt`, `.gitignore`,
  `CHANGELOG.md`) edited append-only/minimal.
- Open issues #1001–#1007: platform-domain defects (io/workflow/dataset/
  georef) owned by other tracks — recorded in BASELINE.md, NOT fixed here.
- `ISSUES.md` (old D3 operator-gap backlog): not treated as live backlog.

## Delivered capabilities (work packages A–H)

(final numbers filled at PR creation)

- A — lab data pack contract (`sicnu.lab-pack/1`): 17 per-lab manifests,
  loader/verifier, authoring tool with drift gate, docs.
- B — grader 2.0: six new kernels behind the unchanged gradeArtifact seam +
  rules files for labspec labs 8–11 + deterministic fixtures + corpus.
- C — batch classroom 2.0: roster, identity/duplicates, caps, cancel,
  deterministic JSON/HTML summaries.
- D — headless `lab --report` with RECORDED grade embedding.
- E — injection/leak corpus for the teaching copilot.
- F — `lab --self-check` offline environment diagnostics.
- G — pack/lab coverage + fixture checksum + zero-diff drift guards.
- H — classroom scale suite (bounded gate + opt-in 1000 run).

## Architecture decisions

See DECISIONS.md (D1–D17).

## Compatibility

Additive CLI flags; grading CSV schema unchanged; rules schema accepts
`artifact.kind: "file"` additively; no `src/app` / D18 UI / D19 foundry
changes; no open-PR file territory touched.

## Local tests

`Local evidence only; no online CI dependency.` — targeted suites run twice
before PR creation (Phase 8); results in EVIDENCE.md / REVIEW_LOG.md.

## Known limitations / follow-ups

- Derived-artifact rules (trend/phenology/anomaly) follow the primary-artifact
  decision D16 and can be added as `<lab>_<artifact>.rules.json`.
- Host without GDAL python bindings: new fixture regeneration is the C++ tool
  (D15); python generators remain for hosts that have them.
