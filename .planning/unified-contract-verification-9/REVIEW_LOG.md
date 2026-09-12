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

Verdict: REQUEST-CHANGES (P0:0, P1:3, P2:8, P3:8). Dispositions:

- [P1] duplicate_node finding dead code → FIXED (addNode records rejected
  re-registrations; findings surface them).
- [P1] run-side helper depth truncation silent → FIXED (loud unresolved
  entry when a params-consuming helper is cut by the budget).
- [P1] kAllowedUnresolved wildcard breadth + missing rot guards → FIXED
  (rot guards implemented for undeclared/dead/unresolved lists; every
  entry must match a live finding in the same run).
- [P2] recode legacy aliases typed string but read as object → FIXED
  (declared object-typed).
- [P2] edge comparator not total (origin tie) → FIXED (origin in key).
- [P2] unreadable operator source silent → FIXED (loud <unreadable>
  finding, mirroring oversize).
- [P2] non-literal helper key silent → FIXED (dynamic-key-argument
  unresolved entry).
- [P2] helpers taking Json::Value by value skipped → FIXED
  (argument-anchored signature regex).
- [P2] shared visited set across schema/run passes → FIXED (run pass uses a
  copy).
- [P2] in-file overload shadowing in indexFreeFunctions → ACCEPTED with
  note (no live instance; documented limitation, loud direction preserved
  via unresolved elsewhere).
- [P2] params key-iteration unflagged → ACCEPTED with note (no live
  instance; follow-up).
- [P3] cmath/string_view-iterator regex, callArgIdentifier comment
  sensitivity, relativize separator, fromJson asString throws, enum token
  find, findMatches re-regex → FIXED where touched (cmatch → std::string,
  relativize separator check, fromJson validation, findMatches reuse);
  ACCEPTED: callArgIdentifier (downstream failure is loud), enum token find
  (single definition, live-tree-anchored).
- [P3] jsoncpp-version dependence of byte-compare → ACCEPTED; noted in the
  snapshot header doc.

## Reviewer B (tests / performance / portability / docs-vs-code)

Verdict: REQUEST-CHANGES (P0:0, P1:3, P2:4, P3:8). Dispositions:

- [P1] relativize not Windows-safe → FIXED (backslash canonicalization +
  separator check).
- [P1] TEST_MATRIX overstated (phantom test_contract_mutation_9, wrong
  attributions) → FIXED (matrix rewritten to the actual suites).
- [P1] rot guards claimed but missing → FIXED (see Reviewer A; guards now
  implemented in projection/command/platform suites).
- [P2] duplicate-node detection vacuous → FIXED (same as Reviewer A P1).
- [P2] unreadable source silent → FIXED.
- [P2] classify() raw strings → FIXED (R"delim(...)" support).
- [P2] diagnostics mutation test is helper-level → CLAIM REWORDED (test
  header now says helper-level; live census is the gate).
- [P3] <map>/<cctype> includes → FIXED. [P3] tautological CHECK → REMOVED.
  [P3] PERFORMANCE doc vs benchmark fields → FIXED. [P3] benchmark "build"
  hardcoded → FIXED (NDEBUG-derived). [P3] regex recompilation → FIXED in
  findMatches. [P3] OWNED-BY-885 list duplicated in two files → ACCEPTED
  with cross-reference comment (single source would need a shared test
  header; tracked as follow-up).
