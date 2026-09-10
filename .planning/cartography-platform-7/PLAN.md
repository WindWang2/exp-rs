# PLAN — execution notes (living document)

Milestone execution order: M0 → M11 (see MILESTONES.md for gates).

## Working rules

- Small commits, one milestone per commit series; branch `feat/cartography-platform-7`.
- Build: `cmd //c "build.cmd --target test_mapspec"` (log to `.build-*.log`, -j2).
- Test: `build-dev\test_mapspec.exe "[tag]"` directly; ctest `-j1` for full runs.
- Contract first: JSON schema + validation + docs draft, then behavior, then tests.
- Every new behavior lands with its TEST_MATRIX row implemented.

## M1 design notes (solver 7.0)

Read-first: `src/agent/cartography/composition.cpp` (952 lines), the constraint
runtime construction in `mapspec_compiler.cpp` + `mapspec.cpp` (validation of
constraints), report consumers (`quality.cpp` preflight, `cartography_tools.cpp`
compose/preflight/repair tools).

Additive MapSpec v4 surface on constraint items: `hardness` ("hard"|"soft"),
`priority` (0..100, default 50), `weight` (soft only, 0..1000, default 1).
v4 = strict superset of v3 (mapspec.h version history contract).

Report extension (CompositionResult::toJson): `decisions[]`
({constraint, outcome, reason, order_key}), `violated[]` replaces stringly
`unsatisfied` (kept for compat), `unsat_core[]`, `objective {satisfied_weight,
violated_weight, total_weight}`. Anchors/size clamps keep existing counters.

Determinism: all orderings by explicit total order (hardness, priority desc,
weight desc, canonical index asc) — never by JSON object iteration order.

## Open decisions

- (resolved) Template extends stays left-to-right with child last; facets union
  for `tasks`, child override for scalar facets; keyed arrays (variants) deep-merge.
- (resolved) Dual-axis charts: reject at validation without explicit
  `justification` string; no silent single-axis downgrade.

## Progress log

- M0 done: worktree `exp-rs-cartography-platform-7` @ origin/master c731e3e7;
  configure: Ninja + vcpkg installed dir shared from platform-6 worktree +
  winflexbison; baseline full build running (-j2).
- M1..M9 implemented and committed (see git log). Remaining: build green,
  index/gallery regeneration (SICNU_CARTOGRAPHY_REGENERATE_INDEX=1),
  pin the structural-digest golden, headless PNG RCA, docs drift check,
  full local regression, adversarial review, PR.
