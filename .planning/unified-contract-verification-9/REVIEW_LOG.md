# REVIEW_LOG — adversarial review dispositions

Format: `[P0-P3] finding → disposition`. P0/P1 must be fixed; P2 fixed or
strong follow-up; P3 fixed or accepted-with-reason.

## Live findings from the M2 scanner (first full-tree run)

- [P1] `rs:recode`: run() accepts `map`/`recode`/`recode_map`; schema
  declared only `recode_map` (the exact #872/#880 class, live on master).
  → FIXED here: schema declares the legacy spellings as documented aliases
  (file unowned by any open -9 PR). Guard now green.
- [P1] `rs:sar_terrain_masks`: run() accepts `lookAzimuth` +
  `look_azimuth`; schema declared only `lookAzimuthDeg`.
  → FIXED here (additive schema alias declarations; file unowned).
- [P1] `rs:sar_terrain_flatten`: run() accepts `look_azimuth`; schema does
  not declare it. **File is modified by open PR #883** → NOT fixed here to
  avoid cross-track conflict; recorded as allow-list entry
  "schema catch-up owned by #883" + this entry. The contract test stays
  green via the allow-list but the finding is on record.
- [P2] `rs:segment`/`rs:detect`/`rs:embedding`: schema uses the
  addCommonProps(props) container-helper idiom. Scanner extended to follow
  container-argument helpers (no product change). Guard now covers them.
- [P3] Scanner over-approximation: rs:obia_classify reads feature keys via
  a helper whose Json::Value parameter is a sub-object — attributed to the
  params root. Allow-listed with reason (over-approximation direction only:
  it can only over-report reads, never hide schema omissions in fully
  covered operators).

## Self-review round 1

- [P1] dangling `std::string_view` in the schema walk (substr of a
  std::string member temporary) → FIXED (owning std::string copy).
- [P1] literal-start filter rejected every match containing quotes →
  FIXED (start-position classification only).
- [P2] `findMatchesWithPos` allocated a full classify() vector per call —
  acceptable for bounded tooling inputs (files ≤ 2 MB); noted, accepted.
- [P2] helper-recursion budget: operators-tree recursion is bounded by the
  visited set but has no explicit budget counter; accepted with the 2 MB
  per-file cap and depth ≤ 2 (bounded in practice on the 141-operator tree;
  the earlier processing-tree experiment that OOM'd was reverted).
- PENDING: reviewer rounds after first green build.

## Reviewer A (architecture / correctness / concurrency / security)

- PENDING

## Reviewer B (tests / performance / portability / docs-vs-code)

- PENDING
