# R14-B Final Report — 既有 AI 模型推理、GPU/CPU 运行时与分块流水线加固

- **Branch:** `hardening/r14-B-model-runtime` (worktree `/home/kevin/project/exp-rs-r14-b-model-runtime`)
- **Base:** `origin/master` = `f72f0c11731361488a384a085797aae64efaf7ee`
- **Commits:** 5 (c47d8cedb detection · 649fa3249 tile-engine · f5888fa2b providers · 0aa21ec37 docs · 6e1c9d547 review-2)
- **Diff:** src/operators/runtime/** (10 files) + tests (3 new files + 1 additive CMake block + .gitignore whitelist)

## Host reality (disclosed, not hidden)

This ran on ZCode, NOT Google Antigravity: `/teamwork-preview` and
`/goal-loop` are not registered on this host. Per the task's own §1.1
degradation clause the same orchestrator → implementer → INDEPENDENT
REVIEWER structure ran on native multi-agent tooling; the reviewer never
authored any diff (round 1: BLOCK with 1 P1 + 2 P2; round 2: PASS, 0/0).
Git-over-HTTPS to github.com is blocked on this host (a stale
`socks5h://127.0.0.1:7890` gitconfig proxy); `gh`/api.github.com works.
`ctest` is not installed — verification used the direct executables under
the repo's generated harness environment.

## Dedup (runtime state, not the prompt's snapshot)

* **#1505 (open)** touches `src/runtime/gpu/gpu_plane.cpp` (warm-session
  eviction) — NOT duplicated; no B change overlaps that function.
* **#1504 (open)** — read-only for B (src/geospatial,data,dataset,experiment).
* **#1455 P1** corpse-session VRAM ledger — verified FIXED_ON_MASTER by
  #1501 (per-instance holder tickets + wrapper deleter + regression test
  test_model_failure_matrix.cpp:827). R14-B re-ran that suite (2158
  assertions) and added a randomized conservation stress case.
* **#1459** scratch_registry streaming digest — verified FIXED_ON_MASTER
  (#1501 `streamFnv1a`); independently re-audited correct.
* **#1499** master compile break — verified FIXED_ON_MASTER (syntax check
  exit 0 at the base SHA).
* **#1447** jsoncpp cluster — all 9 sites in other tracks' ownership.

## Work packages

| WP | Subject | Status |
|----|---------|--------|
| A1 | NaN geometry boxes shipped → published NaN coords + sort/lround UB | **FIXED** (decode gate + NMS/dedup sanitize incl. NaN confidence) |
| A2 | batched head tensor decoded sample 0 silently | **FIXED** (typed refusal) |
| A3 | square head + `tensor_layout:"auto"` could decode transposed values | **FIXED** (typed refusal; registration-time fix flagged for the framework owner — outside B ownership) |
| B1 | strided-head halo misalignment (output shifted by halo·core/fed; product depended on seam placement) | **FIXED** both engines (halo-margin crop through window→output scale; declared interpolation honored) |
| B2 | morphology re-tally ADDED to pre-morphology counts (published sidecar misreported) | **FIXED** (replace + width floor) |
| B3 | stitched Mask products never tallied class counts (header contract promises them) | **FIXED** |
| B4 | >512-band input wrapped the cv::Mat channel field → heap corruption (reproduced as SIGSEGV) | **FIXED** (typed refusals ×3) |
| B5 | multi-input batch-dimension mismatch escaped a raw cv::Exception | **FIXED** (typed check) |
| B6 | failed/cancelled morphology stranded `.morph~` + staged temp | **FIXED** (in-pass cleanup + call-site catch, incl. non-RSOperatorError paths) |
| C1 | OpenCV inferMulti returned net-internal buffers (shared-session race); setInput outside try; no health accounting; float32 declaration unenforced | **FIXED** |
| C2 | HTTP provider hung FOREVER on threads without a Qt event dispatcher (timeout contract void) | **FIXED** (typed refusal; RED measured the 8s watchdog) |
| C3 | OpenVINO: no infer mutex (shared InferRequest race), memcpy without dtype check (heap overread), catch-all rebranding typed errors | **FIXED** (compile-only lane — disclosed) |
| C4 | TRT: no mutex, device pinned only on the loading thread (thread-local CUDA device!), bindings resolved by index (output-first engines write output into the input buffer), no dtype check; ≥10 branch unbuildable on master | **FIXED** incl. the ≥10 API port (compile-only lane — disclosed) |
| C5 | python worker live-timeout misclassified as crash AND left a live worker whose late stale answer would be served to the next request | **FIXED** (stopWorker + permanentlyDead + Timeout taxonomy) |
| C6 | TensorBlob::fromMat dropped 2-D channel axis; isValid() trusted an overflowing product | **FIXED** |
| D1 | center exactly on a shared core edge owned by two tiles | **FIXED** (half-open ownership) |
| D2 | maxDetections enforced on the raw pre-NMS count (legal runs failed after full inference cost) | **FIXED** (8× raw memory ceiling, exact post-NMS product cap) |
| D3 | negative classId OOB read in the shared vector writer | **FIXED** (clamp both sides) |
| E1 | VRAM ledger conservation under randomized pressure | **ADDED** stress case (master GREEN — #1501 fix; guard) |
| 01/02/04/05/06/07/08/09/11/13/15 | topology/provider-consistency/pool/device/ledger/tile/chunk/merge/types/manifest/cross-platform/observability breadth audits | **DONE** (3 hostile audits + orchestrator line-by-line verification + round-2 sweep of src/runtime/**; no new P0/P1 beyond the table above; clean areas recorded in the matrices) |
| 03 | session-pool lifecycle (gpu_plane) | **AUDITED CLEAN** (use_count==1 + released guards; #1505 eviction not touched) |
| 10 | model publish atomicity | **AUDITED CLEAN** (park/adopt crash ladder + existing fault-injection suite) |
| 12 | HTTP retry/timeout | **AUDITED CLEAN** (single attempt, enforced transfer+wall timeout, mid-stream body cap) |
| 14 | load performance | **NOT CLAIMED** — no benchmark change was made; existing bench targets rebuilt, no regressions |

## Evidence

* **RED** (fixes stashed, tests kept): 12/15 new cases fail — 7/7 decode
  cases (incl. the OOB writer crash), the HTTP 8s-hang reproduction, and
  4/4 tile-engine cases with a **SIGSEGV** on the 513-band input
  (16126 assertions red).
* **GREEN ×3** + 21 regression binaries green (test_model_runtime 100394
  assertions incl. the #1455 corpse-session ledger case; failure-matrix
  2158; model_tasks 12802; multimodal 8324; stress; semantic-equivalence;
  chunk 11s; gpu_plane; providers).
* Two independent review rounds: round 1 BLOCK (1 P1, 2 P2, 7 P3) — all
  fixed and re-verified; round 2 PASS with 0 P0 / 0 P1 (4 more P2/P3
  items fixed; 1 disclosed as out-of-ownership).
* `git diff --check` clean; all changes inside B ownership.

## Unresolved / disclosed

* TRT + OpenVINO lanes are **compile-only** on this host (no CUDA/TRT/
  OpenVINO toolchain) — code-reviewed, not compiled here; TRT ≥10 could
  not compile on master either, so no lane regresses.
* Registration-time square-head refusal (model_catalog.cpp) and the
  maxDetections comment drift (model_catalog.h) belong to the framework
  owner — flagged, not touched.
* `models/**` unchanged: 26/26 manifests parse as valid JSON; the library
  gate suite covers them.
* CI was NOT waited for (per the task's stop conditions); every claim
  above is local-verified or explicitly marked unverifiable.
