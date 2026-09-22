# Hardening 10/20 — scientific-state-suitability-preflight

## Goal

Harden the existing scientific fact chain — `src/scientific_state` (passport),
`src/suitability` (assessor), the preflight execution-gate layers and their
read-only adapters — so the same asset facts cannot contradict each other
across surfaces. No new product directions, no second authority.

## Oracles (this slice)

1. Every confirmed defect in this slice's ownership has a regression oracle
   that fails on `origin/master` (a9dc33fa73) and passes on this branch.
   Because the host operator directed **no local build/test execution**, the
   RED side of each oracle is documented as a source-level execution trace
   (file:line, why the old code cannot produce the asserted outcome); the
   GREEN side is first verified by CI.
2. Key boundary/failure paths are covered, not only happy paths (hostile
   JSON shapes, non-finite numbers, empty fact objects, unresolved inputs).
3. An independent adversarial review re-reads the full diff and its
   P0/P1/P2 findings are fixed before the PR is created.
4. Dedup against all open PRs / issues / remote branches at PR time.
5. PR created, **not merged**, online CI not awaited.

## Directive deviation (recorded)

The operator instructed mid-run: stop local builds/tests; review, then
submit the PR. All changes are therefore verified by reading, by the
existing green suites' shape-compatibility analysis, and by CI after the PR
opens. This file and `02-test-ledger.md` carry the evidence that would
otherwise come from a local RED→GREEN run.
