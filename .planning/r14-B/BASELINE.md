# R14-B Baseline — 既有 AI 模型推理、GPU/CPU 运行时与分块流水线加固

- **Date:** 2026-10-15
- **Base SHA:** `f72f0c11731361488a384a085797aae64efaf7ee` (= `origin/master` at start; verified via `git fetch origin --prune` + `git rev-parse origin/master`)
- **Worktree:** `/home/kevin/project/exp-rs-r14-b-model-runtime`
- **Branch:** `hardening/r14-B-model-runtime` (base=origin/master)
- **Host:** ZCode (NOT Google Antigravity — see DEGRADATION note)

## DEGRADATION note (host honesty)

`/teamwork-preview` and `/goal-loop` are not registered slash commands/skills on this
host (ZCode). Per the prompt §1.1 degradation clause, the same
orchestrator/implementer/independent-reviewer structure is realized with ZCode's native
multi-Agent tooling:

* Orchestrator = this main agent (reads baseline, assigns files, guards scope).
* Implementer = this main agent + scoped subagents where useful.
* Independent Reviewer = a separate subagent with no authorship of the core diffs;
  it writes its own risk predictions before reading implementation conclusions.

## Network reality on this host

* `git`-level GitHub access via `~/.gitconfig` proxy `socks5h://127.0.0.1:7890` is DOWN.
* `api.github.com` (and therefore `gh`) works directly; `github.com:443` (git smart
  HTTP) does not. Workarounds used:
  * `git fetch` run with `-c http.https://github.com/.proxy=` (proxy bypass) — works.
  * Catch2 FetchContent: local override `FETCHCONTENT_SOURCE_DIR_CATCH2=/home/kevin/.cache/exp-rs-fdeps/catch2-v3.7.1`
    (copy of the v3.7.1 tree already fetched by an older build on this machine —
    version matches the pinned `GIT_TAG v3.7.1` exactly). No CMakeLists changes.
* GDAL/PROJ/GEOS resolved from `/home/kevin/wp2-prefix/usr` (CMAKE_PREFIX_PATH).

## Open PRs at start (dedup inputs)

| PR | Branch | Files touching this track | Disposition |
|----|--------|---------------------------|-------------|
| #1504 | hardening/r12-b-geodata-durability | none (src/geospatial, src/data, src/dataset, src/experiment) | READ-ONLY for B |
| #1505 | hardening/r13-j-security-concurrency-fixes | `src/runtime/gpu/gpu_plane.cpp`, `tests/test_gpu_plane.cpp` (warm-session eviction in `ModelSessionPool::acquireSession`) | DO NOT duplicate; any gpu_plane.cpp change must be based on post-#1505 shape |

## Recently merged (dedup inputs)

#1483 (rs science), #1484 (agent/workflow), #1496 (r12-f model runtime — process
control, cancel, VRAM ledger), #1497 (r12-e verification), #1498 (r12-g build),
#1500 (r13-h scientific planning), #1501 (fix/all-open-issues — 29-issue sweep),
#1502 (r13-i build/test), #1503 (r13-g cartography).

Notable verified facts:

* **#1499** (master compile break in `src/python/isolated/python_worker_process_pool.cpp`)
  is **FIXED_ON_MASTER** at `f72f0c117` — verified by
  `c++ -fsyntax-only -std=gnu++20 ...` exit 0 on the current file.
* **#1455 [P1]** corpse-session VRAM ledger bug: the swarm-review text describes
  `model_runtime.cpp:941` assuming the cache holds the last corpse reference.
  Current master ALREADY contains a per-acquisition `holderTicket` ledger holder
  (see `model_runtime.cpp:918-929`), added by #1501. MUST re-verify by
  reproduction whether the fix is complete — especially the ABA / delayed-deleter
  paths — before claiming ALREADY_FIXED.

## Build environment

* Preset-equivalent manual configure (no preset reuse): Ninja, Release,
  `ENABLE_TESTS=ON`, `CMAKE_PREFIX_PATH=/home/kevin/wp2-prefix/usr`.
* Build dir: `build-r14-B` (worktree-local, not shared).
* Parallelism: `-j2` builds, `-j1` ctest, `QT_QPA_PLATFORM=offscreen`,
  cross-track lock `/tmp/exp-rs-r14-global-build.lock`, git-metadata lock
  `/tmp/exp-rs-r14-git-meta.lock`.
