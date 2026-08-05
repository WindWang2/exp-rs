# Findings & Discoveries

## Issue Overview & Target Architecture

### GitHub Open Issues Summary
- Total open issues: 15
- Open PRs: 0

### Key Seams & Dependencies
1. **Epic 1 (Issues #36, #37, #38, #40)**:
   - `ProcessingAssetResolver` implemented in `src/data/processing_asset_resolver.h/cpp`.
   - `DerivationRecord` implemented in `src/data/derivation_record.h/cpp`.
   - `OutputCommitter` implemented in `src/processing/framework/output_committer.h/cpp`.
   - Verified by `test_processing_asset_resolver`, `test_derivation_record`, `test_output_committer`, `test_spectral_index_asset_pipeline`.

2. **Epic 2 (Issues #48, #49, #50, #51, #52, #53)**:
   - `CollectionImportService` implemented in `src/processing/framework/collection_import_service.h/cpp`.
   - Verified by `test_collection_import_service` (183 assertions in 17 test cases).

3. **Epic 3 & 4 (Issues #85, #97)**:
   - Workflow & Pipeline Session: `WorkflowSession` & `PipelineStatusResolver` in `src/workflow/`.
   - Tool Call Dispatcher: `ToolCallDispatcher` & `PythonWorkerProcessPool` in `src/python/isolated/`.

4. **Epic 5 (Issues #75, #13, #14)**:
   - Python Plugin Isolation & Wayfinders.
