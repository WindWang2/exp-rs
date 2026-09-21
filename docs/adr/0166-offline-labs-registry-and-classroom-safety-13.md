# ADR 0166: Offline Labs Registry, Canonical Lab Identity and Classroom CSV Safety 13.0

## Status

Accepted (classroom-safety 13.0 / offline labs completion)

## Context

Three problems in the teaching surface were structural, not cosmetic.

1. **Two generations of lab documents, one of them invisible.** `data/labs/`
   holds 11 D1-era `*.lab.json` documents (LabSpec 2, ids `lab01_…`–`lab11_…`)
   and 4 D3-era `*.labspec.json` authoring files (`sicnu.labspec.v1`, thematic
   ids `sar_processing`, `hyperspectral_analysis`, `cartographic_mapping`,
   `temporal_analysis`). The strict loader globs `*.lab.json` non-recursively
   and requires `^lab[0-9]{2}_[a-z][a-z0-9_]*$`, so the D3 files — which carry
   all the structured Chinese teaching content (principles, glossary, expected
   results, thinking questions) — were read by nothing except
   `test_lab_chains.cpp` and `test_lab_data_pack.cpp`. `scripts/upgrade_labspec.py`,
   the tool written to merge them, iterated `*.lab.json` only and so never saw
   them: it printed `--check ok` while the merge had never happened.

2. **The numeric slots collide.** The D3 files are named `lab8`–`lab11` and
   their titles read 实验8–实验11, but `lab08_atmospheric_correction`,
   `lab09_pca_analysis`, `lab10_mosaic` and `lab11_obia_classification` already
   occupy those slots, each with a pack and 6–7 referencing files. The thematic
   ids, by contrast, carry 19–21 references each. Any fix had to preserve both
   identities without renaming 17 packs.

3. **The in-process CSV had no formula-injection defence.**
   `scripts/run_classroom_batch.py::csv_safe()` neutralizes a leading
   `= + - @ TAB CR`, but `src/cli/lab_batch_runner.cpp::appendCsvField()` — a
   separate implementation of the same export — did not. Student ids come from
   filenames, so a student can name a submission `=cmd|'/c calc'!A1` and have
   it execute when a teacher opens the grades CSV.

Additionally, `tests/test_lab_data_pack.cpp`'s pack/lab parity assertion
(`REQUIRE(packIds == labIds)`) was already red at baseline:
`temporal_phenology_timeline` has a pack and no lab document.

## Decision

### 1. `data/labs/lab-registry.json` is the single authority

Identity resolution is one direction only:

```
lab document id  →  registry (canonical + aliases)  →  pack basename
```

The registry records, per canonical lab: `course_index`, `title`/`title_zh`,
the D3 `source`, its `aliases`, and the shipped `grading_rules` / `pipeline` /
`data_spec` pointers. `alias_packs` maps legacy pack basenames onto canonical
ids; `non_lab_packs` declares deployment units that are not labs
(`grading_corpus`). `scripts/check_lab_registry.py` re-derives every projection
from the tree and fails with a named reason per divergence.

### 2. Canonical ids take the first free slots: lab12–lab14

| canonical | course | legacy aliases |
| --- | --- | --- |
| `lab12_sar_processing` | 9 | `sar_processing`, `lab9_sar_processing` |
| `lab13_hyperspectral_analysis` | 10 | `hyperspectral_analysis`, `lab10_hyperspectral_analysis` |
| `lab14_cartographic_mapping` | 11 | `cartographic_mapping`, `lab11_cartographic_mapping` |

Course numbering survives in the documents' Chinese titles and
`course_index`; the ~60 existing references keep resolving through aliases; no
existing file is renamed. `lab8_temporal_analysis` / `temporal_analysis` is
explicitly `out_of_scope` — the temporal track (PR #1135) owns it and its own
DECISIONS D16 says the labspec stays untouched. `temporal_phenology_timeline`
is registered only as an alias pack of `temporal_analysis`, which is what
closes the pre-existing parity defect **without** deleting the assertion.

### 3. The canonical documents are generated, not hand-written

`scripts/upgrade_labspec.py` reads the registry, and for each canonical entry
folds its D3 source into a LabSpec 2 document (objective_zh, principles,
glossary, expected_artifacts, thinking_questions) over a skeleton that only
points at files that already ship (data-spec prerequisite, pipeline
`grading_ref`, rules-file `grading_rules`). Re-running it is byte-idempotent,
and `--check` now fails when a counterpart is missing — the vacuous green of
the previous version is gone. The four `.labspec.json` files stay in place:
they are cited by `test_lab_chains.cpp:97-100` and
`test_drift_projection_10.cpp:84`.

### 4. A missing capability is a typed refusal, never a zero

`LabBatchRow` gains `unavailable` + `unavailableReason`, and the runner treats
a grader verdict of `unavailable` as its own state: no score is written (the
`score` key is absent from the JSON summary), the reason is mandatory, the row
still occupies exactly one CSV row, and the batch exit code is non-zero. This
reuses the repository's existing vocabulary
(`LabGradeEmbedding::unavailable()`), and is deliberately distinct from
`error` (the grader threw) and from `unverifiable`.

### 5. The C++ CSV neutralizer mirrors the Python one, and a golden keeps them honest

`csvSafeCell()` prefixes `'` to any cell whose first character is
`= + - @ TAB CR`, applied before RFC 4180 quoting. The JSON summary — the
canonical truth — is not touched. `scripts/gen_csv_golden.py` turns the Python
`csv_safe()` into `tests/data/csv_injection_golden.json`, which the Catch2 lane
replays against the C++ side, so the two implementations cannot drift apart
silently.

### 6. Launcher parity is enforced, and its Windows half is honestly labelled

`scripts/check_launcher_parity.py` parses the `.cmd` / `.ps1` / `.sh`
launchers and asserts exit-code propagation (`tools/verification12/run_tests.sh`
and `test_wb7.cmd` both used to exit 0 over a red suite), `SICNU_NO_PAUSE`
guarding, and `--out` spelling parity. It prints `NOT EXECUTED — static only`
for the Windows lanes: this host has no Windows shell in the loop, and calling
that "verified" would be false.

## Consequences

- Three new canonical lab documents become loadable by the strict loader, so
  the D3 teaching content finally reaches the app, the graders and the bundle.
- Pack/lab parity is green for the first time, and the fix is a declaration
  plus a resolver, not a deleted assertion.
- Two implementations of one safety rule now have a shared golden; the corpus
  also pins the counter-examples, because over-neutralizing would corrupt real
  student ids and real scores.
- A class in which nothing could be graded now exits non-zero instead of
  looking like a success.

## Non-goals / known limitations

- The strict loader still has no runtime alias resolution; the registry is
  enforced by script. Wiring `LabSpecCatalog` to the registry is follow-up
  work.
- D3 `steps[]` has a different shape and is not migrated; the operator
  sequence remains authoritative in `data/labs/pipelines/*.pipeline.json`.
- `LabBatchRunner::run()` is sequential by design, so the in-process form of a
  grading deadline is the cooperative `cancelled()` probe. Pre-empting a hung
  grader requires a subprocess timeout, which only the Python driver can do.
- Re-running a batch inside the submissions directory grades the previous
  run's `grades.csv` as a submission, because the runner excludes only its own
  run's outputs. Documented, not changed: altering discovery would change a
  contract other lanes rely on. Write summaries to a separate directory.
