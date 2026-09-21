# Progress — RS14-17-reproducibility-capsule

- 2026-09-21 Slice A GREEN: `sicnu.capsule` v1 document contract, canonical
  bytes, self digest (excludes digest member), shape gates with typed issue
  codes. RED evidence: 12/13 cases failed against hollow stub before
  implementation. `./build-dev/tests/test_experiment_capsule` — 13 cases,
  35 assertions, all pass.
- 2026-09-21 Slice B GREEN: CapsuleBuilder projects goal/lab (id join),
  software, capabilities (hook-pinned descriptor digest; unwired ⇒
  source=record, no fabricated digest), inputs (dataset pin with manifest
  digest + unresolved state), parameters (secret-key pass), plan
  (definition digest via hook), environment (re-redacted). RED evidence:
  7/8 builder cases failed against stub; store identity pins turned out
  immutable after first insert — fixtures now record final identity up
  front. 21 cases / 84 assertions all pass.
