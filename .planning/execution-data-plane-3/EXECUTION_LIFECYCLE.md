# EXECUTION LIFECYCLE — as audited at bf26b74a34

```
 [GUI dialog | Agent tool | MCP | CLI | Workflow step]
        │  ExecutionRequest / submitPipeline
        ▼
 ExecutionPlane (facade; no threads)
        │
        ▼
 TaskCenter::enqueueTask/submitJob/submitPipeline ── fingerprint at submission
   (thread-affinity to catalog; determinism opt-in; fail-closed uncacheable)
        │
        ▼
 processNextQueuedTasks  [admission, m_mutex held]
   1. DAG parents Completed?
   2. priority sort (High→taskId)
   3. global concurrency cap
   4. per-profile cap (InProcess/CLI/QgsTask)
   5. RSS watermark (retry in 250ms)
   6. TaskResourceBudget::canLaunch (never-starve)
        │  admitted → Dispatching → m_pendingLaunches
        ▼
 flushPendingLaunches [outside lock]
   ├─ cache serve path: serveFromExecutionCache
   │    lookup (size+mtime validate) → temp copy → revalidate → rename
   │    → rewritten payload {"cache":"hit"} → markTaskCompleted
   ├─ placeholder substitution (applyPlaceholdersForTask)
   │    └─ verifyDispatchFingerprintLocked (drift ⇒ drop fingerprint)
   └─ JobEngine::submit(jobId)  [priority + exclusive pick]
        │
        ▼
 JobEngine::workerLoop → runOperatorJob
   executor = per-job | prefix(module:/processing:) | RSOperatorRegistry | fallback
   RSOperatorContext{cancel flag, progress throttle 2%, log delta stream}
   op->run(params, ctx)   ← cooperative cancellation point(s)
        │  progress/log records → single listener slot
        ▼
 TaskCenter::onJobRecord → processJobRecord (dedup progress, log offsets)
        │  terminal record
        ▼
 markTaskCompleted
   ├─ extract outputLayerPath from result payload
   ├─ storeExecutionResultLocked → ExecutionResultCache (in-memory)
   ├─ fireTaskCompletionCallbacks (exactly-once, thread-based)
   └─ layerAutoLoadRequested
        │
        ▼
 OutputCommitter::commit (caller side, e.g. workflow/agent seam)
   validate temp openable → atomic rename temp→stable → register DataAsset
   → attach DerivationRecord (lineage: assetId@revision inputs + fingerprint)
   failure ⇒ discardTemporary, nothing registered
        │
        ▼
 [Workflow v2 only] WorkflowRunCoordinator
   onTaskUpdated → persistRunLocked (atomic checkpoint, best-effort)
   finalize → run lock release → ArtifactGC (Completed runs, workspace-scoped)
            → checkpoint deleted
 [Crash] recoverAtStartup → recoverInterruptedRuns: flock probe ⇒ owner dead
   ⇒ reconcile Running→Pending, state→Interrupted; resumeRun skips steps whose
   outputLayerPath exists (no content validation)
```

## Gaps driving this epic

| # | Gap | Phase |
|---|-----|-------|
| G1 | No framework-level chunk/tile execution; file-level dependency only | B |
| G2 | Every step materializes a file; no fused tile pipelines | C |
| G3 | Cache is in-memory, path-stat-validated, no content digest, lost on restart | D,E |
| G4 | No artifact identity beyond "path is identity"; GC anchored on paths | D |
| G5 | Remote COG: no range/tile cache, no ETag, duplicate range requests uncoalesced | F |
| G6 | Resource model = primary RAM only; no cpu/vram/io/network dimensions; 3 coarse priorities, no starvation guard | G |
| G7 | Operators load/run/destroy model per call; no session pool / VRAM budget | H |
| G8 | DataManager linear scans + deep copies; GUI full rebuild; 100k assets unusable | I |
| G9 | Task/pipeline/cache state unpersisted; dispatch-before-persist window; naive resume validation; ghost-checkpoint double-exec window | J |
| G10 | Operators run in-process; operator crash = app crash; no worker isolation | K |
| G11 | No execution timeline/queue-wait/resource-wait metrics; no structured dumps | L |
| G12 | Fault behavior tested only via a few workflow tests; no chaos matrix | M |
