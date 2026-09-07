// dataset_store.h — persistent store for the dataset foundation (ADR 0134).
//
// One SQLite database (WAL) holding datasets, dataset versions, label
// schemas, samples, annotations, split manifests, quality reports and the
// dataset-side lineage edges. The store applies the platform's data-plane
// playbook (ADR 0130) from day one:
//
//   - `schema_version` in a meta table; a NEWER schema opens READ-ONLY
//     (forward tolerance, writes fail with `dataset.store_read_only`);
//   - every write is checked; multi-statement writes run in a transaction
//     with rollback-on-failure (a failed commit can never half-land);
//   - queries are paged and bounded (`kMaxPageSize`), no all-rows paths;
//   - `checkpointForBackup()` folds the WAL so the DB file alone is copyable;
//   - thread-affine: one connection guarded by a mutex (callers may marshal
//     off-thread batches through the same object).
//
// Version lifecycle (ADR 0134): `createDraftVersion` → (mutate draft) →
// `stageVersion` (validate + write canonical manifest, still Draft) →
// `commitVersion` (one atomic transaction: status → Committed + fingerprint
// stamp). A crash can only ever leave a Draft; `staleStagedDrafts()`
// reports drafts left staged-but-uncommitted by an interrupted process.
#pragma once

#include "dataset_ids.h"
#include "dataset_manifest.h"
#include "dataset_types.h"
#include "dataset_version.h"

#include <QPair>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <optional>

namespace sicnu::dataset
{

class AnnotationRecord;
class LabelSchema;
class SampleRecord;

class DatasetStore
{
  public:
    static constexpr qint64 kMaxPageSize = 500;

    DatasetStore() = default;
    ~DatasetStore();
    DatasetStore( const DatasetStore & ) = delete;
    DatasetStore &operator=( const DatasetStore & ) = delete;

    /// Opens (creating if needed) the store. Forward tolerance: a newer
    /// schema_version opens READ-ONLY (writes fail with
    /// `dataset.store_read_only`).
    bool open( const QString &dbPath, QString *errorOut = nullptr );
    void close();
    bool isOpen() const { return m_impl != nullptr; }
    bool isReadOnly() const;
    QString schemaVersion() const;
    QString storePath() const { return m_storePath; }
    /// Folds the WAL into the main DB file so the file alone is safe to copy.
    bool checkpointForBackup();

    // --- datasets -----------------------------------------------------------
    /// Creates the logical dataset row. Idempotency: re-creating an existing
    /// id is a `dataset.conflict` failure, not a silent reuse.
    sicnu::data::Result<QString> createDataset( const DatasetId &datasetId,
                                                const QString &name,
                                                const QString &description = QString() );
    std::optional<QVariantMap> datasetById( const DatasetId &datasetId ) const;
    /// Paged listing ordered by name. `total` is the full row count.
    sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>> listDatasets(
        qint64 offset = 0, qint64 limit = kMaxPageSize ) const;
    qint64 datasetCount() const;
    sicnu::data::Result<void> deleteDataset( const DatasetId &datasetId );

    // --- versions -----------------------------------------------------------
    /// Creates a DRAFT version row. @p manifest must carry the version id
    /// (and, for child versions, the parent id); it is stored as-is and
    /// validated by stageVersion before commit. The dataset must exist.
    sicnu::data::Result<DatasetVersionRecord> createDraftVersion(
        const DatasetManifest &manifest, const QString &note = QString() );
    /// Validates + canonicalizes the draft's manifest, stamps the
    /// fingerprint and persists the staged document. Still Draft — staging
    /// is recoverable work, not a commitment.
    sicnu::data::Result<DatasetVersionRecord> stageVersion( const DatasetVersionId &versionId );
    /// One atomic transaction: Draft → Committed with the staged manifest and
    /// fingerprint. A committed version is immutable by contract; committing
    /// anything but a staged draft fails with `dataset.not_draft`.
    sicnu::data::Result<DatasetVersionRecord> commitVersion( const DatasetVersionId &versionId );
    /// Committed → Deprecated (mark-only; referenced versions stay readable).
    sicnu::data::Result<DatasetVersionRecord> deprecateVersion( const DatasetVersionId &versionId );

    std::optional<DatasetVersionRecord> versionById( const DatasetVersionId &versionId ) const;
    /// Versions of one dataset, oldest first (parent before child).
    QVector<DatasetVersionRecord> versionsOfDataset( const DatasetId &datasetId ) const;
    std::optional<DatasetVersionRecord> latestCommittedVersion( const DatasetId &datasetId ) const;
    /// Drafts that were staged but never committed (interrupted processes).
    /// Recovery decision (resume/cleanup) belongs to the caller; the store
    /// only reports the truth.
    QVector<DatasetVersionRecord> staleStagedDrafts() const;

    // --- lineage edges (dataset-side; goal §28) ------------------------------
    /// Adds a directed edge `from → to`. Both endpoints are (kind, id) pairs
    /// with kind one of: asset, dataset, dataset_version, sample, annotation,
    /// split, experiment, run, artifact, metric. Loose refs by design —
    /// missing endpoints are visible to queries as dangling, never dropped.
    sicnu::data::Result<void> addLineageEdge( const QString &fromKind, const QString &fromId,
                                              const QString &edgeKind,
                                              const QString &toKind, const QString &toId );
    struct LineageEdge
    {
        QString fromKind;
        QString fromId;
        QString edgeKind;
        QString toKind;
        QString toId;
    };
    /// Outgoing edges of one node (bounded by @p limit).
    QVector<LineageEdge> outgoingEdges( const QString &kind, const QString &id,
                                        qint64 limit = 1000 ) const;
    /// Incoming edges of one node (bounded by @p limit).
    QVector<LineageEdge> incomingEdges( const QString &kind, const QString &id,
                                        qint64 limit = 1000 ) const;
    /// Whole-table edge scan (graph assembly input; bounded by @p limit).
    QVector<LineageEdge> allLineageEdges( qint64 limit = 100000 ) const;

    // --- maintenance ----------------------------------------------------------
    qint64 versionCount() const;
    qint64 lineageEdgeCount() const;

    // --- label schemas (document rows; ADR 0135) ------------------------------
    /// Saves a validated schema document. Re-saving the same (id, version)
    /// with DIFFERENT content fails with `dataset.conflict` — published
    /// vocabularies are immutable.
    sicnu::data::Result<void> saveLabelSchema( const LabelSchema &schema );
    std::optional<LabelSchema> labelSchema( const QString &schemaId, quint64 version ) const;
    /// (version, fingerprint-hex) pairs for one schema id, ascending.
    QVector<QPair<quint64, QString>> labelSchemaVersions( const QString &schemaId ) const;

    // --- samples (ADR 0135) ---------------------------------------------------
    /// Batch-inserts samples into a DRAFT version (one transaction). The
    /// version's committed/deprecated states refuse with `dataset.not_draft`.
    /// Duplicate (version, sample) ids fail the whole batch.
    sicnu::data::Result<void> addSamples( const QVector<SampleRecord> &samples );
    std::optional<SampleRecord> sampleById( const DatasetVersionId &versionId,
                                            const SampleId &sampleId ) const;
    /// Paged samples of one version in insertion order (bounded memory at
    /// 100k+ rows; the goal §37 contract).
    sicnu::data::Result<QPair<qint64, QVector<SampleRecord>>> samplesPage(
        const DatasetVersionId &versionId, qint64 offset = 0,
        qint64 limit = kMaxPageSize ) const;
    qint64 sampleCount( const DatasetVersionId &versionId ) const;
    /// Distinct group ids of one version (bounded by @p limit; ordered).
    QVector<QString> sampleGroupIds( const DatasetVersionId &versionId,
                                     qint64 limit = 10000 ) const;
    /// Draft-only removal (typo fix before commit; committed versions are
    /// frozen).
    sicnu::data::Result<void> removeSample( const DatasetVersionId &versionId,
                                            const SampleId &sampleId );

    // --- annotations (ADR 0135) -------------------------------------------------
    /// Appends one revision. Chain integrity is enforced: revision 1 must
    /// not have a parent; revision n>1 must continue the stored n-1 record
    /// (`dataset.annotation_chain_broken` otherwise).
    sicnu::data::Result<void> addAnnotation( const AnnotationRecord &annotation );
    std::optional<AnnotationRecord> annotationTip( const QString &annotationId ) const;
    /// Full revision history, revision 1 first.
    QVector<AnnotationRecord> annotationHistory( const QString &annotationId ) const;
    /// Current (tip) annotations targeting one sample, revision-ascending.
    QVector<AnnotationRecord> annotationsOfSample( const QString &sampleId,
                                                   qint64 limit = 1000 ) const;

  private:
    struct Impl;
    Impl *m_impl = nullptr;
    QString m_storePath;
};

/// Schema version of the dataset store (bump only with a migration story).
inline constexpr const char *kDatasetStoreSchemaVersion = "1";

} // namespace sicnu::dataset
