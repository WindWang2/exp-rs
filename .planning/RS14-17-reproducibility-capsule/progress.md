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
