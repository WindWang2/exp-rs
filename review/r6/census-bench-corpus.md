# WP-C — Bench corpus census (`data/agent/bench`), @ 1e28de867

## 1. Layout and suite

```
data/agent/bench/
├── README.md      # workflow: regenerate + repin procedure (quoted in §4)
├── suite.json     # sicnu.agentbench.suite/v1, suite_id "rs14-agent-bench-core", version "1.0.0"
├── cases/         # 24 files, sicnu.agentbench.case/v1
├── scripts/       # deterministic fake-agent policies, sicnu.agentbench.script/v1
└── traces/        # 4 recorded replays (rogue, inefficient, silent, change-clean), sicnu.agentbench.trace/v1
```

- `suite.json` pins, per entry, one graded source (script XOR trace; uniqueness on (case, source)
  pairs — `tests/test_agentbench_corpus.cpp:114-131`). 24 entries across six families
  (`optical|classification|change|temporal|model|map_delivery`), floor ≥20 cases and ≥3 per family
  enforced at `tests/test_agentbench_corpus.cpp:97-112`.
- Case document = goal + allowed_tools + hidden invariants (grading oracle) + evidence + budget +
  optional fault schedule + `redundant_tools` (redundancy probes).

## 2. Generator

`scripts/bench/generate_agent_bench_corpus.py` (479 lines, deterministic: sorted keys, indent 2,
trailing newline; `write_json` at :464-470). Run from repo root:

```
python3 scripts/bench/generate_agent_bench_corpus.py
```

- Output root is hard-coded relative to the script (`ROOT = …/data/agent/bench`, :19) — it writes
  in-repo; there is no CLI flag, no dry-run, no `--out`.
- The SPEC table (`PACK`, appended :150-439 + `TRACES` :402-447) is the single source; every case /
  script / trace file on disk is generated (byte-stable).
- `main()` (:448-476) rewrites `cases/`, `scripts/`, `traces/` and `suite.json` (`version: "1.0.0"`,
  description "RS14 starter pack: 24 goal-level agent benchmark cases …").
- **No generator-time drift checker exists**: the script never validates tool ids against
  `RSOperatorRegistry`, capabilities, or any live list. The only id-consistency checks anywhere are
  (a) `src/agentbench/fake_agent.cpp:334` — a script step's `tool` must be in the case allow-list
  (opaque string compare, no registry) and (b) fault-tool matching at :367.

## 3. Digest / fingerprint pinning (`tests/test_agentbench_corpus.cpp`)

Computation: the suite runner (`src/agentbench/suite.cpp`) deterministically serializes each parsed
case/script/trace (`digest()` per doc; source digests at suite.cpp:240,254), builds the report shell,
sets `document["pack_digest"] = report.packDigest` where `packDigest =
deterministicSerialize(pack)` and `pack` maps `suite → suite.digest()` plus per-entry
`case_digest`/`source_digest` pins keyed by (casePath, sourcePath) (:36-57,174-177,297-301). Entry
ORDER moves the pack digest (:177 comment). The test hashes `packDigest` with local FNV-1a 64
(`fnv1a64`, test :29-41) — the pin is of the hash of the serialized digest string, not a hash of the
files.

Pinned values (verbatim):

```cpp
// test_agentbench_corpus.cpp:180-187
// Version pin: bump suite.json's version consciously when these change.
CHECK( suite().version == "1.0.0" );
CHECK( fnv1a64( first.report->packDigest ) == 0xcf506e2fb0a9db91ull ); // rs14 starter pack v1.0.0 (to_chars serializer)
for ( const SuiteCaseResult &entry : first.report->cases )
{
    if ( entry.caseId == "optical/ndvi-basic" && entry.sourceKind == "script" )
        CHECK( fnv1a64( entry.evaluationDigest ) == 0xac4b5112a5cb8b90ull ); // optical/ndvi-basic reference evaluation v1.0.0
}
```

Also pinned (same file): summary counts `passCount == 20`, `warningsCount == 0`, `failCount == 4`
(:144-146) and the four failure classes (:154-157):
`classification/supervised-basic|trace → scope_violation`,
`model/inference-basic|trace → silent_failure`,
`change/service-unavailable|script → verification_failed`,
`temporal/budget-tight|script → budget_exhausted`;
plus recovery_quality == 1.0 for `optical/ndvi-transient`, `temporal/gap-fill-transient`,
`map/publisher-transient`, `classification/ensemble-transient` (:165-168).

## 4. What a regenerate requires (exact procedure)

README (`data/agent/bench/README.md` §Regeneration) states it explicitly:

> after ANY content change, rerun the generator, then update the pinned fingerprints and
> `suite.json.version` in the SAME commit.

So an id rename (e.g. the six stale ids from census-vocabulary.md §3) requires:

1. Edit the SPEC table in `scripts/bench/generate_agent_bench_corpus.py`:
   - line 165 `NDVI_TOOLS + ["rs:pan_sharpen"]` and 169 `redundant=["rs:pan_sharpen"]`
   - lines 173 `CLS_TOOLS` / 175 `CLS_STEPS` (`rs:train_classifier`)
   - line 218 `CH_TOOLS` (`rs:co_register`, `rs:change_detect`), 220-221 `CH_STEPS`, 231
     step_order invariant `steps=[...]`
   - line 266 `TMP_TOOLS` (`temporal:smooth`, `temporal:gap_fill`), 268 `TMP_STEPS`, 311
     gap-fill retry override `tool="temporal:gap_fill"`
2. `python3 scripts/bench/generate_agent_bench_corpus.py` → rewrites 24 cases + 24 scripts + 4 traces + suite.json.
3. Bump `suite.json` `"version"` (generator line 470) — e.g. `1.0.0 → 1.1.0` (content-shape
   compatible, ids changed).
4. Rerun `test_agentbench_corpus` and update in the SAME commit:
   - the pack-digest pin `0xcf506e2fb0a9db91ull` (:182) with the new FNV-1a of `packDigest`,
   - the `optical/ndvi-basic` evaluation-digest pin `0xac4b5112a5cb8b90ull` (:186) — NOTE: this case
     is untouched by the renames, but the pin is over the serialized evaluation which embeds the
     case digest; verify whether it moves (mutation test :190-210 proves any case-content change
     moves packDigest; evaluationDigest moved only if ndvi-basic content changes — expected stable,
     but the pack pin WILL move).
   - counts (:144-146) and failure classes (:154-157) should NOT move for pure id renames — the
     verdicts depend on payloads/invariants, not id spellings. Re-verify by running the suite.
5. Consistency constraint inside the corpus: **traces must stay id-aligned with their cases** —
   `recorded-change-clean.json` (replay of `change/bi-temporal-basic`) repeats the same tool ids at
   trace `:32,:42`; the suite grades trace steps against the case allow-list, so a case renamed
   without its trace fails grading. The generator handles this because `TRACES["recorded-change-clean"]`
   reuses `tstep(..., "rs:co_register"/"rs:change_detect", ...)` (:431-437) — rename both.

## 5. Complete stale-id inventory a regenerate would change

| Id | Corpus files (occurrences) | Generator lines | Replacement (see census-vocabulary.md §3) |
|---|---|---|---|
| `rs:change_detect` | cases change-{bi-temporal-basic:4,43, serial-threshold:4, service-unavailable:4, order-recorded:4}; scripts change-{bi-temporal-basic,serial-threshold,service-unavailable}:38; traces/recorded-change-clean.json:42 (9) | 218, 221, 231 | `rs:change_detection` |
| `rs:co_register` | same cases :3 (+bi-temporal:42); same scripts :24; traces/recorded-change-clean.json:32 (9) | 218, 220, 231 | `rs:align` (rec.) / `rs:register_images` |
| `rs:train_classifier` | cases classification-{supervised-basic:3, ensemble-transient:3, rogue-recorded:3}; scripts classification-{supervised-basic,ensemble-transient}:22; traces/recorded-rogue.json:45 (6) | 173, 175 | `rs:supervised_classification` (train mode; `rs:classify` step stays) |
| `rs:pan_sharpen` | cases/optical-redundant-ndvi.json:5,47 (2) | 165, 169 | `gdal:pansharpen` |
| `temporal:smooth` | cases temporal-{smoothing-basic:3, phenology-explain:3, gap-fill-transient:3, budget-tight:3}; scripts temporal-{smoothing-basic:25, phenology-explain:25, gap-fill-transient:40, budget-tight:29,58,73} (10) | 266, 268 | `rs:temporal_smooth` |
| `temporal:gap_fill` | cases temporal-{budget-tight:4, gap-fill-transient:4, phenology-explain:4, smoothing-basic:4}; scripts temporal-gap-fill-transient:25 (5) | 266, 311 | `rs:temporal_gap_fill` |
| `rs:forbidden_augment` | traces/recorded-rogue.json:34 (1) | 410 | optional: keep (deliberate rogue tool) or map to a real-but-disallowed id |
| `model:run_inference`, `model:run_ensemble_vram`, `map:export_geotiff` | model/map family cases+scripts | 321-343, 368-437 | DECISION: no real `model:`/`map:` tool prefix exists; either accept as bench-internal opaque ids (document in README) or map (`rs:infer` / cartography export tool) |

## 6. Consumers of suite.json / cases / scripts / traces

| Consumer | Site | Nature |
|---|---|---|
| Corpus validation test | `tests/test_agentbench_corpus.cpp` (loads via `CMAKE_SOURCE_DIR/data/agent/bench`, :43) | schema, floors, digest pins, end-to-end grading |
| Suite runner library | `src/agentbench/suite.cpp` (`runSuite`), case_schema/evaluator/invariants/failure_taxonomy/fake_agent/trace | the pure-C++ grading harness; fake agent executes scripts; traces replayed |
| Unit tests of the harness | `tests/test_agentbench_{suite,case,trace,fake,evaluator,invariants,core,report}.cpp` | synthetic docs, do not read data/ except corpus test |
| Docs | `docs/agent/benchmark-harness.md` | harness contract |
| CMake wiring | `tests/CMakeLists.txt` (references data/agent/bench) | test data path |
| README | `data/agent/bench/README.md` | authoring + regenerate workflow |

No runtime (non-test) code reads `data/agent/bench` — it is a graded data pack, not a runtime input.

## 7. Trace ↔ case/script id consistency (must-hold rule)

- Script steps: tool must be in case allow-list (`fake_agent.cpp:334-340`); faults may target a tool
  (:367). Trace replay performs the same allow-list check in suite evaluation.
- `recorded-change-clean` / `recorded-rogue` embed operator ids (§5) — they must be renamed in
  lockstep with their case ids (generator guarantees this when SPEC+TRACES are edited together).
- `recorded-inefficient` (`rs:ndvi` twice) and `recorded-silent` (`model:run_inference`) use
  already-valid ids and are unaffected.

## 8. Existing gates touching the corpus (do not duplicate)

Only the corpus test (§3) + README procedure. **No gate cross-checks bench tool ids against the
live registry** — a future gate could compare `allowed_tools` (minus `harness:*` and the deliberate
decoys) against `RSOperatorRegistry::operatorNames()` at test time; that belongs in
`test_agentbench_corpus.cpp` rather than the generator (which is host-python and registry-blind).
