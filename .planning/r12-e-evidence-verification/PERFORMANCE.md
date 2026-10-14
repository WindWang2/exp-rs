# R12-E Performance / Resource Notes

Scope: all changes are correctness/typed-refusal fixes plus tests; no hot-path algorithmic change. No fabricated baselines — numbers below are measured on this host (65 GB RAM, builds under flock, --parallel 2).

## Build cost (this worktree, Debug)
- Cold configure (base-cache from main/build-dev): ~48 s.
- Per-target library-chain builds during waves: ~35–135 s each under the shared flock; agents observed MemAvailable 40–64 GB (≥61%), no --parallel 1 downgrade needed, no OOM.
- Full 19-target sweep: see FINAL_REPORT.md (measured at integration).

## Runtime cost of new/changed code paths
- D1 scenario loader: one seek/tell size probe + same parse as before; 4 MiB cap only refuses pathological files (scenario docs are ≤ tens of KB).
- D7 mirror validation: after every addDocument, all loaded entries re-walk their extends chain (≤4 hops each) — O(D·N·depth) for D documents and N≤2048 entries; trivial at the cap.
- D8 digest: canonicalizeJsonRfc8785 replaces a jsoncpp writer — same order of magnitude; report JSON is small.
- D12 cycle detection: iterative three-color DFS over orderedAfter adjacency — O(V+E) per validate(), rubric-sized graphs only.
- Parity gate test: 14 digests ×4 forks incl. one 1 MiB input — ~2 s measured (ctest 1.9–0.9 s).
- E12 benchmark: 6 cases ≈ 0.75 s total.
- Test process RSS observations by agents: test_faultlab full suite ~7 MB; explain suites ~5 MB. `/usr/bin/time` absent on host — RSS sampled via /proc where reported; otherwise NOT_RUN-style note.

## Regression risk to throughput
None expected: no locks added, no I/O added on hot paths, no new allocations in inner loops. Digest byte change (D8) affects equality comparisons only, all of which compare digests produced by the same code path.
