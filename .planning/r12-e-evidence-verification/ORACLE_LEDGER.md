# R12-E Oracle Ledger (init from audit swarm T1–T7, base 28abef3582)

Legend: WP / root cause / verdict / fix owner / RED→GREEN evidence / status.

## ALREADY_FIXED (evidence preserved, no rework)
| Item | Evidence | Status |
|---|---|---|
| #1464 grader recordedAtUtc | parseSequencingStamp grader_matcher.cpp:113-217, instant compare; regressions test_grader_engine.cpp:296-430; fix commit 9bdd1bcfcf | ALREADY_FIXED |
| #1464 faultlab cell_size inert | fault_transforms.cpp:367-453 consumes+validates; observables consume extras; commits 9bdd1bcfcf, 7430953dcc | ALREADY_FIXED |
| #1465 std::equal OOB ×4 | length gates asset_state_json.cpp:162/196/235/274; commit 9052cb5d82; test_scientific_state_nul_tokens_r8.cpp | ALREADY_FIXED |
| #1466 suitability int fail-open | boundedIntegral suitability_goal.cpp:55-72; commit 08de379dae; test_suitability_adversarial.cpp:846-880 | ALREADY_FIXED |
| #1450 suitability digests | canonicalizeJsonRfc8785 suitability_report.cpp:73, suitability_goal.cpp:341; commit d91a669e52 | ALREADY_FIXED |
| #1450 faultlab locale digest | ClassicNumericLocale canonical_json.cpp:80-146 | ALREADY_FIXED |
| #1447 faultlab transforms untyped | type gates fault_transforms.cpp:93-135; tests test_faultlab.cpp:1568+ | ALREADY_FIXED |
| #1447 capability_mirror unguarded parse | try/catch+stackLimit capability_mirror.cpp:23-35 | ALREADY_FIXED |
| E02 lattice fail-closure | Pass<Indeterminate<Fail worst-wins, empty→Indeterminate; strict wire spelling status_lattice.cpp:43-61 | VERIFIED_SOUND |

## LIVE defects confirmed (assigned)
| ID | WP | Defect | Seveity | Owner |
|---|---|---|---|---|
| D1 | E05 | faultlab loadFaultScenarioFile: no stackLimit, no try/catch (CharReader throws on >1000 nesting, grader_json.cpp:224 documents), unbounded whole-file read, allowComments default-on — violates fault_types.h:53 never-throws | P1 | F1 |
| D2 | E04 | faultlab wrong_scale_offset silently coerces mistyped optional params (offset non-numeric→0.0 :157-159; metadata non-bool→false :168-169) | P2 | F1 |
| D3 | E10 | verify_adapters parseJsonBounded (bounded_io.cpp:77-85): stackLimit=128 set but no try/catch → Json::Exception escapes "Never throws" headers (checkpoint_state_view.h:75, provenance_sidecar_view.h:54, bounded_io.h:54); engine misclassifies as Fail/invalid-spec instead of Indeterminate/artifact-unreadable | P1 | F2 |
| D4 | E09 | scientific_state readInt (asset_state_json.cpp:96-109): isIntegral() then asInt() throws Json::LogicError for out-of-int-range (e.g. 3000000000) → exception escapes typed StateError contract | P1 | F3 |
| D5 | E07 | suitability DatasetFacts: counts via toDouble(-1.0) with no type/non-negativity check (dataset_facts.cpp:54,157,163,169); "100"→-1-as-absent; negative accepted | P2 | F4 |
| D6 | E07 | SceneCandidate::fromJson silently drops wrong-typed numerics (scene_candidate.cpp:170-196; bare toInt for band_count/width/height) — parity break with goal parser | P2 | F4 |
| D7 | E08 | capability_mirror forward-referenced extends chains bypass load-time depth check (capability_mirror.cpp:228-248 sees only loaded entries); mergeEntry :323-331 silently truncates at 4 hops → policies vanish → bandRoleEvaluate "pass" — silent policy narrowing | P1 | F5 |
| D8 | E08 | preflight report/request digest via private jsoncpp writer precision=12 (report.cpp:178-192, engine.cpp:96-105), not RFC8785 authority; only tests consume today; goldens pin current bytes | P2 | F5 |
| D9 | E08 | render projections can_proceed inconsistency: teaching requires findings.empty() (render.cpp:62), agent only verdict=="ok" (:109); unreachable with builtin rules | P2 | F5 |
| D10 | E02 | verification buildReport drops results whose checkId is unmapped in nodeChecks → report.status==Pass while results contain Fail (report.cpp:569-578, 310-355); public runSpec(nodeChecks) entry; latent (no in-repo caller passes partial map) | P1 | F6 |
| D11 | E02 | runSpec hoists VERIFY.BUDGET_EXCEEDED for missing-required-evidence (check_runner.cpp:201,286-292) — mislabeled cause at outcome level | P2 | F6 |
| D12 | E03 | grader rubric validation accepts self/cyclic orderedAfter (grader_types.cpp:594-610,687-694) — vacuously satisfiable | P2 | F7 |
| D13 | E11 | explain: empty execution status passes evidenceFor (provenance_file_evidence.cpp:243) → builder → validator never checks status → renders "状态: " SystemFact badge; ExecutionFacts::toJson writes "status" unconditionally but fromJson requires non-empty (step_explanation.cpp:363 vs :395) — builder output its own strict parser refuses | P1 | F8 |
| D14 | E11 | explain skipped-step trust note asserts authored skipConsequence IS the run outcome (explanation_builder.cpp:319) — overclaim; dormant (projectEngine2Step test-only) | P2 | F8 |
| D15 | E11 | explanation_validator.h:41-44 declares guidance cross-check; member stored, never read — doc/impl mismatch | P2 | F8 |
| D16 | E06 | four SHA-256 forks byte-equivalent today (full-read confirmed) but no parity gate; grader Sha256 update-after-finalize silent no-op (grader_sha256.cpp:86-87, latent) | P2 | F9 |
| D17 | E12 | behavior benchmark: verify→grader→explain teaching scenario with destructive variants — coverage to add | P2 | F10 |

## Documented, no code change (DECISIONS.md)
- verify vs verification NaN verdict-class divergence (Fail vs Indeterminate) — two deliberate lattices (verify_status.h:13-17); needs owner ruling, recorded.
- verification::VerificationReport::fromJson lenient (trusts digest) vs verify strict — tamper-evidence design; recorded.
- verify_adapters: no path containment/symlink policy, GDAL remote open without timeout — adapters dumb-by-design, no production wiring; recorded as follow-up.
- faultlab extras unguarded as* (fault_transforms.cpp:400,436; fault_observables.cpp:205) — internal-only producers today; harden cheaply in F1 if zero-risk, else recorded.
- canonical JSON pair (faultlab jsoncpp leaf vs Qt RFC8785) diverge on reals/key-order/big-int — two authorities, never cross-compared; aligning would reseal faultlab digests (forbidden); documented.
- preflight sha256 padded-copy 2× memory — perf nit, recorded.
- explain findings 4/5/6 (validateGuidance no production caller; panel skips validator; first-artifact citation; runId-from-filename) — recorded.
- Out-of-scope cross-domain notes forwarded: radiometric vocabulary (A), workflow checkpoint reader (C), help/science_context json sites (C/D), teaching_admin items (D), lab_pack CRLF-normalized digests (C), ~9 sha256 forks repo-wide (B/C/D).
