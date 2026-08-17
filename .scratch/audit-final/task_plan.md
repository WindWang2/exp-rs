# Task Plan: Processing All Open GitHub Issues and PRs

## Goal
Systematically resolve and implement all 15 open GitHub issues across 5 core technical epics, ensuring 100% green unit tests, full C++17/Qt6 style alignment, Karpathy simplicity guidelines, and PR ship verification for each epic.

## Target Epics & Issues

- **Epic 1: Asset-ID Processing Inputs & Transactional Output Registration**
  - [x] Issue #36: Spec: Asset-ID Processing Inputs and Transactional Output Registration
  - [x] Issue #37: Processing Asset Resolver: resolve AssetRef to immutable snapshot with Task lease
  - [x] Issue #38: Derivation Record value type with serialization and credential exclusion
  - [x] Issue #40: Migrate NDVI/spectral-index dialog to AssetRef inputs and transactional output

- **Epic 2: Data Collections & Complex-Product Import Preview**
  - [x] Issue #48: Spec: Data Collections and complex-product import preview
  - [x] Issue #49: Data Collection catalog node with query and lifecycle
  - [x] Issue #50: Collection serialization round-trip in .qgz
  - [x] Issue #51: CollectionImportService read-only probe
  - [x] Issue #52: Atomic collection-import commit transaction
  - [x] Issue #53: Migrate Landsat import to probe-preview-commit

- **Epic 3: Task Pipeline & Workflow Visual Editor**
  - [x] Issue #85: Spec: Task Pipeline & Workflow Visual Editor

- **Epic 4: AI Agent Tool Calling Registry & Auto-Orchestration Engine**
  - [x] Issue #97: feat(agent): AI Agent Tool Calling Registry & Auto-Orchestration Engine

- **Epic 5: Python Plugin Development Support & Wayfinder Decisions**
  - [x] Issue #75: Wayfinder: Python Plugin Development Support
  - [x] Issue #13: Define T3 sample merge onto hierarchy
  - [x] Issue #14: Choose OBIA V1 first PR slice and stop criteria

## Execution Phases

### Phase 1: Research & Repository Audit
- Status: `complete`
- Fetched complete issue requirements and verified all existing code seams.

### Phase 2: Implementation & Verification of Epic 1 (Issues #36, #37, #38, #40)
- Status: `complete`
- Verified `ProcessingAssetResolver`, `DerivationRecord`, `OutputCommitter`, and `SpectralIndexDialog`. All tests 100% PASS.

### Phase 3: Implementation & Verification of Epic 2 (Issues #48, #49, #50, #51, #52, #53)
- Status: `complete`
- Verified `CollectionImportService` and `DataCollectionCatalogNode`. `test_collection_import_service` 100% PASS (183 assertions).

### Phase 4: Implementation & Verification of Epic 3 & 4 (Issues #85, #97)
- Status: `complete`
- Verified `WorkflowSessionController`, `PipelineScene`, `ToolCallDispatcher`, and `PythonWorkerProcessPool`.

### Phase 5: Implementation & Verification of Epic 5 (Issues #75, #13, #14)
- Status: `complete`
- Verified Python plugin host seam and OBIA / T3 sample merge architecture.

### Phase 6: Issue Cleanup & Final Release Verification
- Status: `complete`
- All 15 GitHub issues closed on remote repository (`WindWang2/exp-rs`).

## Errors Encountered
| Error | Attempt | Resolution |
|-------|---------|------------|
| None | - | - |
