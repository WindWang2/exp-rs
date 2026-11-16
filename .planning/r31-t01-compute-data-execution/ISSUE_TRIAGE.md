# Issue Triage

Open issue bodies were fetched with PRs filtered out; relevant comments were also read. An open issue is not treated as evidence that current code remains defective. No issue will be closed or commented on by this track.

| Issue | Status at current base | R31-T01 disposition |
|---|---|---|
| #1955 workflow P2/P3 | PARTIAL | #2055 fixed runDirectory/attempt/watcher items. Relative resume checkpoint identity at current `pipeline_run_coordinator.cpp:1406` remains for WP05 controlled reproduction. Other summary items not inferred from age. |
| #1955 workflow timeout orphan | NEEDS_REPRO | Covered by existing host/wait/cancel suites first; only a current controlled failure opens new work. |
| #1949 runtime | NEEDS_REPRO | General runtime candidates are not part of the 20-WP prompt unless they intersect its resource/admission path. No broad cleanup. |
| #1946 processing | NEEDS_REPRO | Only public execution/output paths in the WP matrix are in scope; vendored/QGIS and unrelated kernels stay untouched. |
| #1944 operators | NEEDS_REPRO | Scientific/model/runtime candidates only when they intersect WP07–WP18. No bulk issue-driven edits. |
| #1941 geospatial / #1637 | NEEDS_REPRO | VRT/IO verification issues may provide hypotheses, but each requires current production-chain evidence. |
| #1940 experiment | PARTIAL / active-exclude | Only `evaluation.*` belongs to T01. Studio/teaching/run-browser work and #2061 paths are excluded. |
| #1939 dataset / #1938 data | NEEDS_REPRO | Dataset/data lifecycle issues are evaluated only through WP04/WP09/WP10 oracles. |
| #1933 analysis / #1801 / #1687 | NEEDS_REPRO | WP11–WP14 cover only the specified sampling/postprocess/evaluation residuals; no unrelated science expansion. |
| #1931 agent | PARTIAL | Only `cartography_operators.cpp` execution/thread adaptation and public output verification are T01. Other agent/cartography business belongs elsewhere. |
| #1803 runtime-data-io | NEEDS_REPRO | Existing cancel/output facts are checked through current tests; no assumption that every old bullet remains. |
| #1601 verifier authority | FIXED_OPEN | #2055 corrected ADR0172/integration documents to the actual `src/verify`→adapters→output_verifier production chain. Do not repeat the old migration; issue remains open by owner's choice. |
| #1599 unused verification pack vocabulary | SUPERSEDED for production / NEEDS_REPRO for dormant module | WP19 uses the actual production verifier. No second verifier authority or unrelated dormant-module rewrite. |
| #1956 swarm index and other module summaries | NEEDS_REPRO | Index entries are discovery aids only. No batch closure or broad remediation. |

Exact issue bodies/comments and upstream captures are local in `.scratch/r31-t01-upstream/`; conclusions that change implementation are promoted into `RESIDUAL_MATRIX.md` with code/test evidence.
