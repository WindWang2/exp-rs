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

## Final verification

Two consecutive passes of the targeted selection (to be appended):

```
ctest -R "test_lab_runtime|test_curriculum|test_lab_document|test_lab_source|test_recipe_|teaching_foundation_e2e|test_sample_fixtures|test_labspec|test_lab_data_pack"
```
