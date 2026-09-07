# TEST_MATRIX — per-milestone test plan (failing-first where material)

Runner env: `QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib`, CTest parallel 1.

| Milestone | New/changed tests | Existing suites that must stay green |
|---|---|---|
| A | corrupt-DB save keeps v3 (or fails loudly); A/B project switch no bleed; clearProject clears cache; restore_failed diagnostics on newer-schema/read-only; write→snapshot→restore preserves all writes; concurrent-writer snapshot openable | test_workspace_project_v3, test_workspace_services, test_workspace_snapshot, test_governance_store |
| B | failed INSERT/COMMIT → Result error + rollback; alias owner collision symmetric; summary counts = real COUNT at >page-size; bulk path prepare/batch benchmark | test_governance_store, test_governance_tools, test_workspace_stress |
| C | pooled double-store eviction keeps shared object; intermediate unload keeps downstream lineage; bundle options not mutated | test_artifact_store, test_workflow_artifact_gc, test_workspace_services |
| D | register→run→same-size rewrite→rerun = miss; notifyExternalContentChange bumps revision + invalidates; coarse-mtime case | test_workflow_cache_e2e, test_workflow_incremental_cache, test_temporal_workspace |
| E | crash→replace completed output→resume re-executes step (or fails loudly); unchanged output resumes without re-exec | test_workflow_recovery, test_workflow_resume_provenance, test_workflow_run_coordinator |
| F | failed pipeline → runs.state=Failed; partial outputs orphan-classified; summary/search/bundle truthful | test_workspace_services, test_governance_tools, test_workflow_run_coordinator |
| G | cancel between batches → partial registered + cancelled=true | test_workspace_services (ImportCenter section) |
| H | worker crash→restart; per-job timeout escalation; shutdown quiesces | test_worker_host |
| I | validator/ETag change → miss; offline → miss not stale hit; quota bounded | test_remote_source_cache |
| J | FAULT_MATRIX rows executed in CI-capable subset; 100k routine; 1M opt-in (SICNU_WS4_STRESS=1) | test_workspace_stress |

Evidence: paste pass/fail counts into REVIEW_LOG.md per milestone.
