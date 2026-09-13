# GOAL — eo-ai-model-runtime-foundation-10 · EO AI Model Runtime / Foundation Model Platform 10.0

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/eo-ai-model-runtime-foundation-10` (worktree
> `../exp-rs-eo-ai-model-runtime-foundation-10`, off `origin/master` @ `7d78059d1a`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block
> on, trigger, or cite online CI.
> **Write scope:** see `OWNERSHIP.md` (model runtime + manifest + inference operators +
> provider adapters + EO task semantics + their tests/docs/knowledge projection)
> **Read-only:** `master`; `src/geospatial/`, temporal operators, `src/experiment/`
> internals, `src/agent/` server internals, GUI shell, `docs/verification/*` (each
> attributed in OWNERSHIP.md)

## Mission

Model Runtime 9.0 delivered the execution seam (manifest 4.0 identity, provider matrix
with real CUDA, per-feed preprocessing, tile engine with feather blending, detection
decode, provenance verification) — evidence:
`.planning/model-runtime-multimodal-9/FINAL_REPORT.md`, code at
`src/operators/runtime/`. What master still lacks: an EO-domain manifest truth layer
(wavelengths, calibration requirements, CRS/grid assumptions, formal task taxonomy),
typed artifact contracts per EO task family (classification / change / regression /
instances have no task-shaped operator — only the generic rs:infer), optional TensorRT /
OpenVINO provider adapters, a model-selection knowledge projection that lets the Agent
choose models by manifest facts instead of name guessing, a tiled-inference resume seam,
and the MLOps benchmark/promotion seam. Three known defects from the merged whole-repo
line review sit inside this ownership and are in-scope fixes: F-OPS-5 (NMS O(n²) +
non-cancellable), F-OPS-1 (class_mapping output-encoding corruption), F-OPS-2
(TensorBlob ND non-continuous no-op copy) — `review/findings/operators.md`.

After this track: the manifest is the EO model truth layer; every EO task family has a
typed output artifact contract over ONE execution path; provider expansion is optional
and cannot break default builds; the Agent selects models from projected manifest facts;
large-raster inference stays bounded-memory, cancellable, provenance-carrying, and now
resumable after interruption.

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended; defaults recorded in `DECISIONS.md`.
- **Agent**: `zcode`. **Budget: 300,000,000** (allocation in PLAN.md).
- **Subagents: ≤ 2, read-only** — A: architecture + EO/scientific correctness;
  B: concurrency/lifecycle + test credibility + security. Main agent owns all writes.
- **No CI**: local evidence only → `EVIDENCE.md`.
- **Branching**: master read-only; all work in the worktree.
- **Build**: `cmake --preset dev-default`; targeted test targets; `CMAKE_BUILD_PARALLEL_LEVEL=2`,
  `CTEST_PARALLEL_LEVEL=1`, ninja/make `-j2` → `-j1` when RSS > 70% or load > 1.5× cores;
  `-j$(nproc)` forbidden; `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` first.
- **Exit**: PR created to `master`, NOT merged.

## Work packages

| ID | Package | Key deliverables |
|---|---|---|
| A | EO ModelManifest 10.0 | EO task taxonomy (closed, backward-compatible), `eo` domain extension: wavelengths, calibration requirements, CRS/grid assumptions, GSD; validation + effective projection |
| B | Task families + typed artifacts | rs:classify / rs:change / rs:regress / instance decode over the single execution seam; typed output artifact contracts |
| C | Provider matrix | TensorRT + OpenVINO optional adapters (default build unaffected), plugin provider seam, tests |
| D | Pre/post processing | preprocess offset, postprocess morphology + probability calibration; F-OPS-5/1/2 fixes |
| E | Large raster | resume seam for tiled inference; bounded-memory + cancellation regressions |
| F | Model selection knowledge | manifest-facts projection (capability JSON + catalog accessors); no name guessing |
| G | MLOps seam | benchmark set record, promotion evidence via model digest, provenance verification wiring |
| H | Security/isolation | python/external provider audit: bounded streams, timeout, cleanup, redaction, path trust |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M) |
|---|---|---:|
| 0 | Baseline / archaeology / dedupe / planning files | 18 |
| 1 | F-OPS fixes + manifest contract (WP-D fixes, WP-A) | 48 |
| 2 | Task families + typed artifacts (WP-B) | 54 |
| 3 | Pre/post + providers (WP-D rest, WP-C) | 46 |
| 4 | Knowledge projection + resume + MLOps + security (WP-F/E/G/H) | 38 |
| 5 | Tests + docs + ADR | 34 |
| 6 | Independent review (≤2 read-only subagents) | 24 |
| 7 | Review-fix loop | 20 |
| 8 | Rebase + final verification + PR | 18 |

Measurement: phase-end timestamp / tool-call count / files-touched appended to EVIDENCE.md.

## Autonomy defaults

1. **格式/来源**: EO manifest fields are additive extensions of the v5 manifest; every
   new field is optional, absent = historical behavior bit-identical; canonical sources
   are existing `ModelInfo` structures + `domain` section conventions.
2. **失败项处置**: a single failing case is fixed or, when environment-gated (no CUDA,
   no TensorRT), marked SKIP-with-capability-reason like the 9.0 suites; never a silent pass.
3. **命名/编号**: new operators `rs:<task>` matching existing style; new files snake_case
   under `src/operators/runtime/`; ADR number = next free (0149+); test files
   `test_<topic>.cpp` + CTest family names matching.
4. **资源与超时**: single build command ≤ 60 min wall before abort-and-shrink to -j1;
   single test binary ≤ 15 min; timeout ⇒ record + retry once at -j1.
5. **对外动作**: read-only git fetch/push of the track branch + `gh pr create` allowed;
   no issue close/reopen, no external upload.
6. **范围外发现**: OUT_OF_SCOPE section in EVIDENCE.md; P0 additionally at PR_BODY top.
7. **依赖新增**: none. TensorRT/OpenVINO adapters compile only when headers/libs are
   found at configure time; otherwise typed `DeviceUnavailable` refusal — default build
   identical to master.

## Completion gate

1. EO manifest extension parses + validates (test → exit 0).
2. Task adapters produce typed artifacts through runModelInference (test → exit 0).
3. F-OPS-5/1/2 fixed with regression tests green.
4. Optional providers absent ⇒ default configure+build identical; present ⇒ tests pass (capability-gated).
5. Whole model-runtime suite family green locally (`ctest -R "model_|multimodal" -j1`).
6. Review P0/P1 = 0; disposition log in REVIEW_LOG.md.
7. Final verification re-run at PR HEAD; PR created, not merged.
