# Test ledger — hardening/curriculum-lab-recipes-foundry

## Oracles with pre-fix (RED) evidence

| oracle | kills | RED evidence on master behavior | status |
|---|---|---|---|
| `scripts/_old_gate_probe.py` transcript (master `check_lab_registry.py` vs new) | S2 | legal v3 lab → `spec_version must be 1 or 2` + `unknown top-level key 'runtime'` (exit 1); new gate passes the same doc | RED→GREEN recorded in-session |
| registry fork probe (`lab12 grading_rules → accuracy_agreement.rules.json`, an *existing* file) | S9 | all master checks green on the fork; new gate: `wrapper grading_rules … disagrees with registry …` exit 1 | mutation killed |
| local-generated-files probe (`data/labs/_tmp/sar/*` present, write-mode regen) | S11 | master generator adds `bytes:` to 5 packs (machine-dependent output); new generator: `git status data/labs/packs` empty | property proof |
| test_lab_runtime "runtime parsing never throws on hostile value shapes" | S3 | pre-fix binary: `unexpected exception with message: Type is not convertible to string` (jsoncpp LogicError escapes parseRuntimeBlock; aborts the run) | RED confirmed, GREEN after fix |
| test_lab_runtime "expected_numeric and hint level are never silently defaulted" | S4 | pre-fix: 4 failed assertions (result ok with silent 0.0 / level collapses to 1) | RED confirmed |
| test_lab_runtime "session envelope refuses unknown keys at every nesting level" | S5 | pre-fix: 5 failed assertions (smuggled fields adopted) | RED confirmed |
| test_lab_runtime "session store failures are typed" (new sections) | S6 | pre-fix: 20-digit seq file → unexpected failure (saturated overflow); file-as-lab-dir → success with seq 1 | RED confirmed |
| test_lab_runtime "session ledger seq is assigned by the recorder" | S8 | pre-fix: `500 == 1` forged seq accepted | RED confirmed |
| test_sample_fixtures "generate pins the GDAL environment only for its own scope" | S13 | pre-fix: `"NO" == "YES"`, `"1" == "4"` — pins leak into the host | RED confirmed |
| test_curriculum v3-resolves section | S14 | pre-fix: v3 → "unknown" (coded expectation inverted; section now proves v3 resolves) | by code inspection of `resolveLabReference` (spec_version 1\|2 guard) |
| test_recipe_registry "Registry scans never throw on hostile JSON shapes" | S15 | object-valued schema/recipe_id/stage-kind/hook-kind in the scan surface reach asString() unguarded | RED by code inspection; GREEN after guards |
| test_teaching_foundation_e2e | integration | new chain (foundry→lab→runtime→recipe→refs) over existing authorities only | new; green |

## Registered-into-build proof (S1)

- master: `ctest -N` has no `test_lab_runtime` / `test_curriculum` / `test_recipe_*`
  entries; `src/recipes` appears in no CMake target; `sicnu_lab_runtime` has no
  link consumer. Baseline transcript to be appended after first build.
- this branch: all of the above registered (`ctest -N` section below).

## Final verification (after rebase onto master abc07b715 + review fixes)

Two consecutive passes, every suite run directly (16 suites):

```
tests/test_lab_runtime              All tests passed (687 assertions in 25 test cases)
tests/test_lab_document             All tests passed (59 assertions in 5 test cases)
tests/test_lab_source               All tests passed (23 assertions in 6 test cases)
tests/test_recipe_compiler          All tests passed (81 assertions in 8 test cases)
tests/test_recipe_equivalence       All tests passed (79 assertions in 5 test cases)
tests/test_recipe_lookup            All tests passed (24 assertions in 6 test cases)
tests/test_recipe_registry          All tests passed (20 assertions in 4 test cases)
tests/test_recipe_schema            All tests passed (15 assertions in 6 test cases)
tests/test_recipe_validator         All tests passed (16 assertions in 8 test cases)
tests/test_curriculum               All tests passed (349 assertions in 10 test cases)
tests/test_labspec                  All tests passed (138 assertions in 9 test cases)
tests/test_lab_data_pack            All tests passed (176 assertions in 11 test cases)
tests/test_lab_grader_kernels       All tests passed (147 assertions in 11 test cases)
tests/test_build_wiring_drift       All tests passed (2 assertions in 1 test case)   [master #1246 oracle]
test_teaching_foundation_e2e        All tests passed (211 assertions in 4 test cases)
test_sample_fixtures                All tests passed (26132 assertions in 28 test cases)
===== PASS 1: ALL 16 SUITES PASSED
===== PASS 2: ALL 16 SUITES PASSED
```

Gates on the same tree: `check_lab_registry.py --strict-data` ok ·
`check_curriculum.py` OK · `gen_lab_packs.py --check` packs in sync ·
`gen_lab_docs.py --check` 17 docs in sync.

The S2/S9/S11 probes in this ledger were run against the pre-rebase tree;
their code paths are untouched by the rebase (master's advance did not
modify any file this branch fixes — verified via
`git diff a9dc33fa7..origin/master -- <files>` = only src/recipes/CMakeLists.txt).

Note: commit 1a706b582's message mis-describes the README drift (the
out-of-sync column was bound operators 2→1, not thinking questions); the
committed regeneration is identical either way and the gate is green.
