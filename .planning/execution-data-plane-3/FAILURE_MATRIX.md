# FAILURE MATRIX — expected behavior under faults

| Fault | Current behavior (baseline) | Required behavior (3.0) | Phase/M test |
|---|---|---|---|
| SIGKILL worker process | worker runs in-process ⇒ app dies | coordinator marks task Failed, app survives, artifacts not half-registered | K |
| SIGKILL coordinator (app) | in-memory task state lost; checkpoint may lag | resume from runs.sqlite + checkpoint; no double execution of completed steps; dispatch-before-persist window closed | J |
| Cancel mid-write | temp file may remain; destination untouched | temp cleaned on cancel; destination never partial; provenance records cancel | M (existing + test) |
| Disk full during write | unspecified | write fails ⇒ task Failed; temp removed; cache serve falls through; no corrupt cache entry | M |
| Destination permission denied | rename fails ⇒ commit fails | typed error; task Failed; nothing registered | M |
| Remote timeout | GDAL 30s timeout, error surfaces | typed failure; offline fallback for display; retry policy; no GUI block | F |
| Remote content changed (ETag) | undetected (mtime/size only, remote often constant) | ETag/LM revalidation ⇒ stale tile discarded, refetched | F |
| Checkpoint corruption (truncated/bad JSON) | load rejects ⇒ run unrecoverable | quarantine bad file, keep `.bak` of previous good save, resume from last good | J/M |
| Cache corruption (digest mismatch) | size+mtime only ⇒ possible stale serve | digest verify ⇒ self-heal erase + real execution | E |
| Missing artifact (deleted behind cache/GC) | lookup self-heals (erase) | same, plus entry GC'd; resume path validates existence+size+mtime+digest | E/J |
| GPU OOM | unspecified | session admission caps VRAM; OOM ⇒ retry smaller batch ⇒ CPU fallback | H |
| Stale model (file changed) | session may pin old bytes | session pool keyed by model content hash + mtime ⇒ recycle | H |
| Worker disconnect (pipe EOF) | n/a | task Failed with diagnostic; no hang; slot freed | K |
| High asset count (100k) | UI freeze, O(N) scans | paged model/view; sub-ms indexed lookups; no UI block | I |
| Concurrent identical workflow | run lock blocks second start (same machine) | second start fails with clear owner info; cross-process safe | existing + M |
| Cache serve destination already exists w/ newer mtime | path-claim eviction overwrites | identity check: only overwrite when destination belongs to this run (destination identity preserved) | E |
| Double serve of same fingerprint concurrently | fingerprint-isolated temp names | preserved (tests) | E |

Rule: every fault ⇒ predictable state, no silent wrong data, no app crash,
recoverable by resume/retry.
