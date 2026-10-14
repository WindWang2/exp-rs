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

## LIVE defects confirmed (assigned) — all resolved; resolution column records fix commit and review state
| ID | WP | Defect | Severity | Owner | Resolution |
|---|---|---|---|---|---|
| D1 | E05 | faultlab loadFaultScenarioFile: no stackLimit, no try/catch (CharReader throws on >1000 nesting, grader_json.cpp:224 documents), unbounded whole-file read, allowComments default-on — violates fault_types.h:53 never-throws | P1 | F1 | FIXED c21f097a68 (cap+stackLimit+catch); TOCTOU hardening e067a523b2; R1/R2 verified |
| D2 | E04 | faultlab wrong_scale_offset silently coerces mistyped optional params (offset non-numeric→0.0; metadata non-bool→false) | P2 | F1 | FIXED c21f097a68; R1 verified |
| D3 | E10 | verify_adapters parseJsonBounded: stackLimit=128 set but no try/catch → Json::Exception escapes "Never throws" headers; engine misclassifies verdict class | P1 | F2 | FIXED d89a313bf0; R1 verified |
| D4 | E09 | scientific_state readInt: isIntegral() then asInt() throws for out-of-int-range → exception escapes typed StateError contract | P1 | F3 | FIXED 0344543bce; class closure extended to model_sidecar/asset_state_diff/fault_scenario.max_bytes 717f5537f4 and fault_transforms e067a523b2; R1/R2 verified |
| D5 | E07 | suitability DatasetFacts: counts via toDouble(-1.0) with no type/non-negativity check | P2 | F4 | FIXED 477ddd6683; R1 verified |
| D6 | E07 | SceneCandidate::fromJson silently drops wrong-typed numerics | P2 | F4 | FIXED 477ddd6683; integer-valued+range gate e067a523b2; R1/R2 verified |
| D7 | E08 | capability_mirror forward-referenced extends chains bypass depth refusal → silent policy truncation → pass | P1 | F5 | FIXED e15ff4f79a (load-order-independent validation); boundary false-positive fix 717f5537f4; R1/R2 verified |
| D8 | E08 | preflight report/request digest via private precision-12 writer, not RFC8785 authority | P2 | F5 | FIXED e15ff4f79a (+projection canonicalizer 717f5537f4); R1/R2 verified |
| D9 | E08 | render projections can_proceed inconsistency (teaching stricter than agent) | P2 | F5 | FIXED e15ff4f79a (fail-closed alignment); R1 verified |
| D10 | E02 | verification buildReport drops node-unmapped Fail results → report.status==Pass possible | P1 | F6 | FIXED 6709ff7d1d; policy-promotion parity 717f5537f4; R1/R2 verified |
| D11 | E02 | runSpec marks budgetUsage.exceeded on missing-required-evidence (untruthful cause) | P2 | F6 | FIXED 6709ff7d1d; R1 verified |
| D12 | E03 | grader rubric validation accepts self/cyclic orderedAfter | P2 | F7 | FIXED c05a350dba; R1/R2 verified |
| D13 | E11 | explain: empty execution status flows to a SystemFact badge; builder output its own strict parser refuses | P1 | F8 | FIXED 1a771c4b7a (adapter+builder+validator three-gate); R1 verified |
| D14 | E11 | explain skipped-step trust note asserts authored consequence IS run outcome | P2 | F8 | FIXED 1a771c4b7a (attribution reworded); R1 verified |
| D15 | E11 | explanation_validator.h declares guidance cross-check never implemented | P2 | F8 | DOC-FIXED 1a771c4b7a (header matches reality); follow-up recorded in DECISIONS |
| D16 | E06 | four SHA-256 forks unpinned; grader streaming update-after-finalize silent no-op unpinned | P2 | F9 | GATE ADDED 095614d9f7 (parity corpus+KATs+locale sweep; streaming pin); R1/R2 verified |
| D17 | E12 | behavior benchmark coverage for verify→grader→explain-adjacent teaching evidence | P2 | F10 | ADDED 959d19aae7 (faultlab leg + grader leg, precise localization, seed isolation, digest stability); link cleanup by main agent; R1/R2 verified |

## Documented, no code change (DECISIONS.md)
- verify vs verification NaN verdict-class divergence (Fail vs Indeterminate) — two deliberate lattices (verify_status.h:13-17); needs owner ruling, recorded.
- verification::VerificationReport::fromJson lenient (trusts digest) vs verify strict — tamper-evidence design; recorded.
- verify_adapters: no path containment/symlink policy, GDAL remote open without timeout — adapters dumb-by-design, no production wiring; recorded as follow-up.
- faultlab extras unguarded as* (fault_transforms.cpp:400,436; fault_observables.cpp:205) — internal-only producers today; harden cheaply in F1 if zero-risk, else recorded.
- canonical JSON pair (faultlab jsoncpp leaf vs Qt RFC8785) diverge on reals/key-order/big-int — two authorities, never cross-compared; aligning would reseal faultlab digests (forbidden); documented.
- preflight sha256 padded-copy 2× memory — perf nit, recorded.
- explain findings 4/5/6 (validateGuidance no production caller; panel skips validator; first-artifact citation; runId-from-filename) — recorded.
- Out-of-scope cross-domain notes forwarded: radiometric vocabulary (A), workflow checkpoint reader (C), help/science_context json sites (C/D), teaching_admin items (D), lab_pack CRLF-normalized digests (C), ~9 sha256 forks repo-wide (B/C/D).
