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
