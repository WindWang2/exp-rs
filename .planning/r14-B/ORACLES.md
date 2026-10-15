# R14-B Oracles — per work package

Format: `Oracle → RED evidence → Fix → GREEN evidence (×2) → residual risk`.

| ID | Oracle | RED (master) | Fix | GREEN ×2 | Status |
|----|--------|--------------|-----|----------|--------|
| WP-A1 | NaN-geometry candidates never reach published boxes; decode drops them | test_r14_B_detection_decode "decode drops NaN geometry" fails on master (NaN box present) | isfinite gate in decode loop | same test passes ×2 | code done, run pending |
| WP-A2 | batched head tensor (size[0]>1) refused typed | "refuses a batched head tensor" fails (no error returned) | size[0] check | pending | code done |
| WP-A3 | auto layout refuses C==N ambiguity | "refuses ambiguous auto layout" fails | C==N refusal | pending | code done |
| WP-NMS | public NMS/dedup tolerate NaN boxes (no sort UB / no lround UB) | "public NMS tolerates NaN" fails/crashes on master | sanitize at NMS + dedup entry | pending | code done |
| WP-D1 | center on shared core edge owned by exactly one tile | engine-level; covered by seam invariance + unit clamp tests | `>=` upper bounds | pending | code done |
| WP-D2 | maxDetections enforced on post-NMS kept count; raw guard at 8× for memory | new engine cap logic | both guards | pending | code done |
| WP-D3 | negative classId never OOB in writeDetectionVector | "clamps a negative classId" crashes on master | std::clamp both sides | pending | code done |
| WP-B1 | strided-head stitch tile-count invariant + even-column exact | "strided-head stitching..." mismatches on master | halo-margin crop before resize (both engines) | pending | code done |
| WP-B2 | morphology re-tally REPLACES pre-morph counts | "morphology re-tally replaces" reads 10 on master | assign+floor width | pending | code done |
| WP-B3 | stitched Mask tallies class counts | "stitched Mask products tally" reads zeros on master | tally in Mask branch | pending | code done |
| WP-B4 | >512-band input typed refusal | crash/garbage on master (CV_CN_MAX wrap) | 3 refusal sites | pending | code done |
| WP-B5 | multi-input batch-mismatch typed refusal | raw cv::Exception on master | size[0]==B check | pending (E2E test deferred — see RISK_REGISTER) | code done |
| WP-B6 | no `.morph~`/stage residue on morphology failure/success | residue on master | failMorphology cleanup + call-site catch | pending | code done |
| WP-C1 | inferMulti returns detached outputs; setInput typed; failures recorded | "inferMulti returns detached outputs" tears on master | clone under lock + try-wrapped setInput + health parity | pending | code done |
| WP-C2 | HTTP inferNamed on dispatcher-less thread returns typed error ≤ timeout | hangs forever on master (8s watchdog FAIL) | eventDispatcher() guard | pending | code done |
| WP-C3 | OpenVINO serializes forwards; typed dtype refusal; typed errors pass through | review-verified (lane not compilable locally) | mutex + dtype check + RSOperatorError rethrow | BLOCKED_WITH_EVIDENCE (no OpenVINO toolchain on host) | code done |
| WP-C4 | TRT serializes; device pinned per forward; dtype checked; binding-by-direction; ≥10 API port | review-verified (lane not compilable locally) | mutex + cudaSetDevice + checks + #if port | BLOCKED_WITH_EVIDENCE (no CUDA/TRT toolchain on host) | code done |
| WP-C5 | live-worker timeout classified Timeout not ProviderCrash | message pin | Running-state branch | pending | code done |
| WP-C6 | fromMat keeps 2-D channel axis; isValid refuses overflow shapes | unit tests fail on master | channel append + countOrZero gate | pending | code done |
| WP-E1 | randomized acquire/release pressure ends with ledger == 0 holders == 0 | conservation stress (expected GREEN on master — #1501 fix; guard test) | none needed | pending | test written |

## Verification runs

(to be filled in TEST_RESULTS.md as runs complete; commands documented there)
