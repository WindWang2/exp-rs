// governance_store.h — persistent workspace governance store (Platform 3.0).
//
// Single SQLite database (WAL) next to the project file that indexes and
// governs every first-class project entity: asset enrichment mirror, datasets,
// results (+ lifecycle), workflow-run index, experiments, queryable lineage
// edges, smart collections, exports, path mappings and the audit log.
//
// Design contracts:
//   - DataManager stays the runtime asset authority; the assets table here is
//     a durable mirror + governance enrichment (fingerprints, sensor, ...).
//   - every query is paged and bounded (kMaxPageSize, like WorkspaceCatalog);
//   - bulk mutations batch into single transactions (ingest 100k in batches);
//   - schema_version'd with forward tolerance: a NEWER schema opens read-only;
//   - thread-affine like DataManager (one connection, guarded by a mutex, so
//     off-thread pipeline batches may marshal through the same object).
#pragma once

#include "governance_types.h"

#include <QString>
#include <QVector>

#include <optional>

namespace sicnu::workspace
{

/// Governance enrichment row for one asset. Identity/structure fields mirror
/// sicnu::data::CatalogAsset so the mirror upsert is a single call.
struct GovernedAsset
{
    QString assetId;
    QString sourceKey;
    QString canonicalSource;
    QString kind;                 ///< raster|vector|remote_map|virtual_raster
    QString state;                ///< DataManager AssetState (string form)
    QString persistence;          ///< project|session|task
    QString displayName;
    QString parentCollectionId;
    qint64 acquisitionMs = 0;
    quint64 revision = 1;
    QJsonObject metadata;
    QStringList aliases;

    // --- governance enrichment ---
    QString contentFingerprint;   ///< content digest (empty = not computed)
    qint64 sizeBytes = -1;        ///< -1 = unknown
    qint64 mtimeMs = 0;
    qint64 verifiedMs = 0;        ///< last integrity verification stamp (0 = never)
    QString format;               ///< GDAL/OGR driver short name
    QString crs;
    qint64 bandCount = -1;
    QString bandRoles;            ///< comma-joined semantic roles
    QString sensor;
    QString modality;             ///< optical|sar|dem|...
    QString availability;         ///< fresh|stale|unverified
    QStringList tags;             ///< governance tags (entity_kind='asset')
};

class GovernanceStore
{
  public:
    static constexpr qint64 kMaxPageSize = 500;

    GovernanceStore() = default;
    ~GovernanceStore();
    GovernanceStore( const GovernanceStore & ) = delete;
    GovernanceStore &operator=( const GovernanceStore & ) = delete;

    /// Opens (creating if needed) the store. Forward tolerance: a newer
    /// schema_version opens READ-ONLY (writes fail with store.read_only).
    bool open( const QString &dbPath, QString *errorOut = nullptr );
    void close();
    bool isOpen() const { return m_impl != nullptr; }
    bool isReadOnly() const;
    QString schemaVersion() const;

    // --- assets -------------------------------------------------------------
    Result<void> upsertAsset( const GovernedAsset &asset );
    Result<void> upsertAssets( const QVector<GovernedAsset> &assets );  // one transaction
    Result<void> removeAsset( const QString &assetId );
    std::optional<GovernedAsset> assetById( const QString &assetId ) const;
    std::optional<GovernedAsset> assetByPath( const QString &path ) const;  // alias lookup
    /// Assets whose stored fingerprint equals @p digest (relink lookup).
    QVector<GovernedAsset> assetsByFingerprint( const QString &digest, qint64 limit = 32 ) const;
    QVector<GovernedAsset> allAssets( qint64 limit = -1 ) const;  // bounded callers only
    qint64 assetCount() const;

    // --- tags (any entity kind) ---------------------------------------------
    Result<void> setTags( const QString &entityKind, const QString &entityId, const QStringList &tags );
    Result<void> addTag( const QString &entityKind, const QString &entityId, const QString &tag );
    QStringList tagsOf( const QString &entityKind, const QString &entityId ) const;
    /// Bulk tag inside one transaction; returns number of rows touched.
    Result<qint64> bulkTag( const QVector<QString> &entityIds, const QString &entityKind, const QString &tag );

    // --- datasets -----------------------------------------------------------
    Result<void> upsertDataset( const DatasetRecord &dataset );
    Result<void> removeDataset( const QString &datasetId );
    std::optional<DatasetRecord> datasetById( const QString &datasetId ) const;
    QVector<DatasetRecord> datasets( qint64 limit = kMaxPageSize ) const;

    // --- results ------------------------------------------------------------
    Result<void> upsertResult( const ResultRecord &result );
    Result<void> removeResult( const QString &resultId );
    std::optional<ResultRecord> resultById( const QString &resultId ) const;
    QVector<ResultRecord> results( qint64 limit = kMaxPageSize ) const;
    /// Results consuming @p assetId as an input (impact analysis seed).
    QVector<ResultRecord> resultsDependingOnAsset( const QString &assetId ) const;
    /// Results without any live producing run/artifact anchor (orphans).
    QVector<ResultRecord> orphanResults() const;

    // --- runs ---------------------------------------------------------------
    Result<void> upsertRun( const RunRecord &run );
    std::optional<RunRecord> runById( const QString &runId ) const;
    QVector<RunRecord> runs( qint64 limit = kMaxPageSize ) const;
    void linkRunOutput( const QString &runId, const QString &assetId );

    // --- experiments --------------------------------------------------------
    Result<void> upsertExperiment( const ExperimentRecord &experiment );
    Result<void> removeExperiment( const QString &experimentId );
    std::optional<ExperimentRecord> experimentById( const QString &experimentId ) const;
    QVector<ExperimentRecord> experiments( qint64 limit = kMaxPageSize ) const;

    // --- lineage edges ------------------------------------------------------
    struct LineageEdge
    {
        QString outputAssetId;
        QString inputAssetId;
        quint64 inputRevision = 0;
        QString operatorId;
        QString runId;
        QString stepId;
        QString fingerprint;
    };
    Result<void> addLineageEdges( const QVector<LineageEdge> &edges );  // one transaction
    /// Transitive upstream (inputs) of @p assetId, breadth-ordered, cycle-safe.
    QVector<QVariantMap> lineageUpstream( const QString &assetId, int maxDepth = 25 ) const;
    /// Transitive downstream (outputs depending on @p assetId).
    QVector<QVariantMap> lineageDownstream( const QString &assetId, int maxDepth = 25 ) const;
    /// Direct edges only (graph export / bundle provenance).
    QVector<LineageEdge> directEdges( const QString &assetId, bool outgoing ) const;

    // --- smart collections --------------------------------------------------
    Result<void> upsertSmartCollection( const SmartCollectionRecord &collection );
    Result<void> removeSmartCollection( const QString &collectionId );
    QVector<SmartCollectionRecord> smartCollections() const;

    // --- exports / mappings -------------------------------------------------
    Result<void> upsertExport( const ExportRecord &record );
    QVector<ExportRecord> exports( qint64 limit = kMaxPageSize ) const;
    Result<void> upsertPathMapping( const PathMapping &mapping );
    QVector<PathMapping> pathMappings() const;

    // --- unified query (faceted, paged) --------------------------------------
    WorkspacePage query( const WorkspaceQuery &query, const QString &facetField = QString() ) const;

    // --- audit log ----------------------------------------------------------
    struct AuditEntry
    {
        qint64 seq = 0;
        qint64 tsMs = 0;
        QString actor;
        QString action;
        QString entityKind;
        QString entityId;
        QJsonObject detail;
    };
    Result<void> appendAudit( const QString &actor, const QString &action,
                              const QString &entityKind, const QString &entityId,
                              const QJsonObject &detail = {} );
    QVector<AuditEntry> auditTail( qint64 limit = 100 ) const;

    // --- meta ---------------------------------------------------------------
    void setMeta( const QString &key, const QString &value );
    QString meta( const QString &key ) const;
    /// Explicit integrity probe: runs PRAGMA quick_check and reports the result
    /// through GovernanceDiagnostics instead of throwing.
    GovernanceDiagnostic integrityCheck() const;
    /// Wipes all rows (schema retained) — used by rebuild-from-DOM recovery.
    Result<void> clearAll();

  private:
    struct Impl;
    Impl *m_impl = nullptr;
};

} // namespace sicnu::workspace
