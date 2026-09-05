# ADR 0129: Project Workspace, Data Governance & Reproducibility Platform 3.0

- Status: Accepted (Workspace Governance 3.0 goal series)
- Context: exp-rs assets were identified by file paths + session state; provenance was
  one-hop; the SQLite Data Plane 3.0 (WorkspaceCatalog/ArtifactStore) existed but was
  unwired; the project format had no migration ladder and saves were not crash-safe;
  results, experiments, smart collections, audits and relocation support did not exist.
- Decision:
  1. **Identity**: every governed entity (Workspace/Dataset/Result/Experiment/
     SmartCollection/Export) carries a stable UUID identity that never derives from a
     path; paths are storage locators (`src/data/governance/governance_types.h`).
  2. **Two-plane authority**: DataManager stays the in-memory runtime authority; a new
     SQLite WAL `GovernanceStore` (`<project>.governance.db`, schema_version 1, forward
     read-only tolerance) is the durable, queryable index — assets are mirrored with
     governance enrichment (content fingerprint, sensor, modality, CRS, availability),
     not owned.
  3. **Project Format v3**: `<sicnuDataManager version="3">` keeps the v1 blocks
     byte-compatible and adds one `<workspace>` JSON block for governed state. Readers
     accept {1,3}; v1 files migrate in memory (M1 = full mirror) and upgrade only on
     the next save; unknown sections/fields are reported then skipped.
  4. **Atomic save**: `QgsProject::writeProjectFile` writes a temp file in the target
     directory, fsyncs, and POSIX-replaces the target — a crash leaves the old or the
     new file, never a truncated one.
  5. **Services over registries**: one `WorkspaceService` facade drives GUI, agent
     tools (`project:/asset:/collection:/lineage:/result:/run:`) and CLI
     (`project validate|search|migrate|relink|lineage|export-manifest|audit`) — no
     second project parser anywhere.
  6. **Lineage as a graph**: DerivationRecords synchronize into `lineage_edges`;
     transitive upstream/downstream queries are cycle-safe recursive CTEs with depth
     bounds; impact analysis joins results in one query.
  7. **Remove ≠ delete**: catalog removals never unlink payload bytes; cleanup plans
     are non-destructive and execution only drops rows whose payloads are gone AND
     that nothing downstream references.
- Consequences:
  - 100k+ asset workspaces are queryable with bounded memory (paged model/view UI,
    ≤500-row pages, batched ingest ~1s/100k, point lookups ~42µs).
  - Reproducibility bundles (reference-only/metadata-only/portable) and snapshots
    (`<base>-<stamp>.snapshot/`) make experiments shareable without copying imagery.
  - The governance DB is regenerable (rebuild-from-document) and gitignored; the
    document remains the portable source of truth.
  - Old builds refuse the v3 custom-data block (strict v1 check) — documented one-way
    door; layers still load, governed state degrades to the previous session.
