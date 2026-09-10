# REVIEW LOG — Cartography Platform 7.0

Running log of self-review findings and the final adversarial review dispositions.

## Self-review during development

(appended per milestone)

## Final adversarial review

(plan: ≤2 read-only subagents; dispositions recorded here with verdicts before/after)

## Self-review during development (running log)

- M1 solver: fixed before commit — soft-rank comparison originally used the
  declaration index instead of the canonical-order position (wrong winner
  named under mixed priorities); rect snapshots now iterate collections
  (not the id map) so id-less items restore correctly; anchor_wins
  decisions deduped across the unsat-core restore resweep; specific Failed
  diagnostics preserved (pinned by 6.0 tests); blocked softs surfaced in
  `unsatisfied` so preflight sees them.
- M1 unsat cores: removed misleading anchor pseudo-members (the
  counterfactual never disables anchors); unused locals removed.
- M4 typography: degenerate ellipsis boxes degrade to the ellipsis marker
  with an explicit diagnostic instead of an empty (silent) text.
- Baseline gaps corrected by the audit: preflight already shipped
  uncertainty-note, off-page, overlap, tiny-text, clipped-title rules
  (6.0); M7 scope reduced to the genuinely missing rules. Drift suite
  already covered solution→recipe/template/style, template→component,
  style→token, recipe→operator; M9 added component→token, inheritance
  parents, modality vocabulary and mutation proofs.
- Known risk pinned for the build: M5 compat test validates all 17 shipped
  styles; verified none carries scheme/nodata/uncertainty fields that the
  new validator would reject.

## Local validation evidence (rounds 0-4, Windows headless)

- Baseline build: configure (Ninja, shared vcpkg installed dir, winflexbison)
  + full `test_mapspec` target chain — green.
- DLL root-cause for the local 0xc0000135 failures: PATH gaps (Qt debug
  binaries, qca/kc bins, shared vcpkg debug bins), NOT a code defect. Fixed
  in run-tests.cmd; this also un-blocked the PNG cases 6.0 had to skip.
- `[platform7]`: 45 cases / 283 assertions — ALL PASSED.
- Full suite `~[visual]`: 145 cases / 1293 assertions — ALL PASSED.
- Remediation rounds fixed, with root causes:
  1. resolved-template identity (jsoncpp payload sharing corrupted the raw
     catalog through the `self` return; fixed with copyPayload + id re-stamp),
  2. kinsoku condition inversion (tail-fit also added),
  3. applicability early-return short-circuiting the 7.0 checks,
  4. chart validation else-if chain broken by the axes insertion
     (matrix/accuracy Summary:family demanded inline data / layer wrongly),
  5. accuracy_summary renderer gate + inline-only check keyed on mode,
  6. test fixture defects (missing style envelopes, text on titles,
     anchor-resolved expectation, provenance semantics).
