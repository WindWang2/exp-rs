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
- 2026-09-21 Slice C GREEN: outputs (portable refs, digest-pinned, no-digest
  labeled honestly), evidence (EvidenceProjector completeness verbatim,
  artifacts/env blocks deduped away from the document, verifier hook
  embedded or empty — never faked), provenance (recorded lineage slice +
  slice digest). Absolute-path leak asserted absent over canonical bytes.
  Portability core (toPortableRef) landed. 26 cases / 121 assertions pass.
- 2026-09-21 Slice D GREEN: CapsuleIO export (refuses unverified digest),
  load (parse → canonical-form gate → shape gates; reformatting is refused
  so integrity stays byte-checkable), validate (shape + secret denylist +
  absolute-path scan, fail closed with typed codes). RunEnvironment
  denylist matchers exposed additively (single source of truth, no second
  pattern list). 34 cases / 158 assertions pass.
- 2026-09-21 Slice E GREEN: CapsuleReadiness::assess replays the document
  against the LOCAL machine (dataset/split store lookups, capability
  descriptor digest match, plan definition digest, software revision, model
  + output availability hooks). Rollup mirrors ReplayReadiness (Impossible
  on missing/mismatched REQUIRED pins, else BestEffort on Unknown, else
  Exact); unwired hooks never fake Exact. Fix found by tests: the report
  struct defaults to Impossible — rollup must roll DOWN from Exact.
  40 cases / 180 assertions pass.
- 2026-09-21 Slice F GREEN: CapsuleDiffReport::diff — fast-path digest
  equality, depth-first section diffs with stable paths, identity vs
  reported classification (ADR 0137). Levels: Identical /
  EquivalentRerun (environment, evidence, created_utc, capsule_id only) /
  IdentityBreak (any pin section). Direction only swaps left/right.
  45 cases / 210 assertions pass.
