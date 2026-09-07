// workspace_service.h — Workspace Governance 3.0 platform facade.
//
// Single service object that the GUI, the agent tools and the CLI all drive.
// It owns the GovernanceStore, mirrors the in-memory DataManager (which stays
// the runtime asset authority) into the persistent index, synchronizes
// DerivationRecords into the queryable lineage graph, and exposes the bounded
// registry operations for datasets / results / runs / experiments / smart
// collections / tags / audit.
//
// Invariants enforced here (task §28):
//   - asset identity is the DataManager AssetId; a path change is a locator
//     change (relink), never a new identity;
//   - result lineage: every registered result keeps producer + input anchors;
//   - remove-from-project is a catalog operation; physical deletion never
//     happens implicitly inside this service.
//
// Threading: thread-affine like DataManager. Off-thread workers (metadata
// pipeline, import scan) marshal results back through queued invocations.
#pragma once

#include "data/governance/governance_store.h"
#include "data/governance/governance_types.h"

#include <QObject>
#include <QString>

#include <optional>

class QJsonDocument;

namespace sicnu::data
{
class DataManager;
class AssetId;
class AssetSnapshot;
struct DerivationRecord;
}

namespace sicnu::workspace
{

class WorkspaceService : public QObject
{
    Q_OBJECT

  public:
    explicit WorkspaceService( QObject *parent = nullptr );
    ~WorkspaceService() override;

    // --- store lifecycle ------------------------------------------------------
    bool openStore( const QString &dbPath, QString *errorOut = nullptr );
    void closeStore();
    bool isStoreOpen() const { return m_store.isOpen(); }
    /// True when the store can be trusted as a serialization source right
    /// now: open, writable, and passing an integrity probe. A corrupt store
    /// must never feed a save — the cached document is used instead
    /// (issue #746: corrupt-DB-while-open data-loss route).
    bool storeIntegrityOk() const;
    /// Path the store was opened from (empty when never opened). Remembered
    /// even when the open failed or the store is read-only, so the snapshot
    /// and diagnostics paths can locate the DB file.
    QString storePath() const { return m_storePath; }
    GovernanceStore &store() { return m_store; }
    const GovernanceStore &store() const { return m_store; }

    /// Conventional location: "<projectBase>.governance.db" next to the
    /// project file. @p projectFile may be .qgs or .qgz.
    static QString defaultStorePathFor( const QString &projectFile );

    // --- DataManager mirroring (Phase C/H wiring) ------------------------------
    /// Binds the runtime catalog and starts mirroring. Calling twice with the
    /// same manager is a no-op; rebinding disconnects the previous one.
    void bindDataManager( sicnu::data::DataManager *manager );
    sicnu::data::DataManager *dataManager() const { return m_dataManager; }
    /// Bulk mirror of every current asset (+ lineage). Used after project open
    /// / migration. Returns number of mirrored rows.
    qint64 mirrorAllAssets( bool reconcileGhosts = false );

    /// Re-derives the GovernedAsset row (and lineage edges) for one asset.
    /// Returns false when the asset is unknown to the DataManager.
    bool mirrorAsset( const sicnu::data::AssetId &id );

  signals:
    /// Emitted after any governed mutation; entityKind is "asset", "dataset",
    /// "result", "run", "experiment", "smart", "export" or "workspace".
    void entityChanged( const QString &entityKind, const QString &entityId );

  public:
    // --- asset enrichment (Phase C) ---------------------------------------------
    /// Stores/replaces the governance tags of an asset (audited).
    bool setAssetTags( const QString &assetId, const QStringList &tags, const QString &actor = QStringLiteral( "user" ) );
    bool bulkTagAssets( const QStringList &assetIds, const QString &tag, const QString &actor = QStringLiteral( "user" ) );
    /// Records a content verification result (fingerprint + size + mtime).
    bool noteAssetVerified( const QString &assetId, const QString &fingerprint, qint64 sizeBytes, qint64 mtimeMs );

    // --- datasets (Phase D) ------------------------------------------------------
    DatasetId createDataset( const QString &name, DatasetKind kind,
                             const QStringList &memberAssetIds = {},
                             const QString &actor = QStringLiteral( "user" ) );
    bool updateDataset( const DatasetRecord &record, const QString &actor = QStringLiteral( "user" ) );
    bool removeDataset( const QString &datasetId, const QString &actor = QStringLiteral( "user" ) );
    QVector<DatasetRecord> datasets() const { return m_store.datasets(); }
    std::optional<DatasetRecord> dataset( const QString &id ) const { return m_store.datasetById( id ); }

    // --- results (Phase M/N) -----------------------------------------------------
    ResultId registerResult( ResultRecord record, const QString &actor = QStringLiteral( "system" ) );
    /// Applies a lifecycle transition when legal; audits and returns false otherwise.
    bool changeResultStatus( const QString &resultId, ResultStatus to,
                             const QString &actor = QStringLiteral( "user" ), const QString &notes = QString() );
    QVector<ResultRecord> results() const { return m_store.results(); }
    std::optional<ResultRecord> result( const QString &id ) const { return m_store.resultById( id ); }
    QVector<ResultRecord> resultsDependingOnAsset( const QString &assetId ) const;
    QVector<ResultRecord> orphanResults() const;

    // --- runs (Phase L) ------------------------------------------------------------
    void recordRun( const RunRecord &run );
    std::optional<RunRecord> run( const QString &runId ) const { return m_store.runById( runId ); }
    QVector<RunRecord> runs() const { return m_store.runs(); }

    // --- experiments (Phase L) ------------------------------------------------------
    ExperimentId createExperiment( const QString &name, const QString &objective = QString(),
                                   const QString &actor = QStringLiteral( "user" ) );
    bool updateExperiment( const ExperimentRecord &record, const QString &actor = QStringLiteral( "user" ) );
    bool removeExperiment( const QString &experimentId, const QString &actor = QStringLiteral( "user" ) );
    QVector<ExperimentRecord> experiments() const { return m_store.experiments(); }

    // --- smart collections (Phase I) -------------------------------------------------
    SmartCollectionId createSmartCollection( const QString &name, const QVector<SmartPredicate> &predicates,
                                             const QString &actor = QStringLiteral( "user" ) );
    bool removeSmartCollection( const QString &collectionId, const QString &actor = QStringLiteral( "user" ) );
    QVector<SmartCollectionRecord> smartCollections() const { return m_store.smartCollections(); }
    /// Evaluates a saved predicate set against the live index. Returns an
    /// empty page when the collection or its predicates are malformed.
    WorkspacePage evaluateSmartCollection( const QString &collectionId, qint64 offset = 0, qint64 limit = 50 ) const;

    // --- unified query (Phase H) -----------------------------------------------------
    WorkspacePage query( const WorkspaceQuery &query, const QString &facetField = QString() ) const
    {
        return m_store.query( query, facetField );
    }

    // --- lineage (Phase J) -------------------------------------------------------------
    QVector<QVariantMap> lineageUpstream( const QString &assetId, int maxDepth = 25 ) const
    {
        return m_store.lineageUpstream( assetId, maxDepth );
    }
    QVector<QVariantMap> lineageDownstream( const QString &assetId, int maxDepth = 25 ) const
    {
        return m_store.lineageDownstream( assetId, maxDepth );
    }
    /// Impact analysis: transitive downstream assets plus the results that
    /// consume any of them. Bounded by maxDepth.
    QVariantMap impactAnalysis( const QString &assetId, int maxDepth = 25 ) const;
    /// Producer lookup for "which run/workflow produced this asset?".
    QVariantMap producerOf( const QString &assetId ) const;

    // --- audit (Phase V) -----------------------------------------------------------------
    bool audit( const QString &actor, const QString &action, const QString &entityKind,
                const QString &entityId, const QJsonObject &detail = {} );
    QVector<GovernanceStore::AuditEntry> auditTail( qint64 limit = 100 ) const { return m_store.auditTail( limit ); }

    // --- project document (Phase B) ---------------------------------------------------
    /// True when a governed snapshot is cached from a previous read/write and
    /// can be re-persisted even with the store unavailable (downgrade guard).
    bool hasCachedProjectJson() const { return !m_cachedProjectJson.isEmpty(); }
    /// True once a v3 workspace document has been read or serialized in this
    /// session. When set, a save may never silently rewrite the project as v1
    /// (issue #746 downgrade guard).
    bool isV3Seen() const { return m_v3Seen; }
    /// Drops the cached governed document and the v3-seen mark. Called on
    /// project transitions (clearProject) so governed state cached for one
    /// project can never bleed into another project's file.
    void clearCachedDocument();
    /// Records that the project file carried a v3 marker even when its
    /// workspace block was missing or unparseable (defense-in-depth for the
    /// downgrade guard: such a session must not silently save as v1).
    void markV3Seen() { m_v3Seen = true; }
    /// The last serialized (or restored) governed document.
    QJsonObject cachedProjectJson() const { return m_cachedProjectJson; }
    /// Serializes the governed (non-asset) workspace state for the v3 DOM block.
    QJsonObject toProjectJson() const;
    /// Restores governed state from the v3 DOM block. Unknown sections are
    /// skipped with diagnostics (forward tolerance); never fatal.
    QVector<Diagnostic> fromProjectJson( const QJsonObject &root );

    // --- helpers -----------------------------------------------------------------------
    static QString contentFingerprint( const QString &path );

  private:
    void onAssetAdded( const sicnu::data::AssetId &id );
    void onAssetChanged( const sicnu::data::AssetId &id );
    void onAssetRemoved( const sicnu::data::AssetId &id );
    std::optional<GovernedAsset> governedRowForSnapshot( const sicnu::data::AssetSnapshot &snapshot ) const;
    bool mirrorSnapshot( const sicnu::data::AssetSnapshot &snapshot );
    void syncDerivationLineage( const sicnu::data::AssetSnapshot &snapshot );
    void collectDerivationLineage( const sicnu::data::AssetSnapshot &snapshot,
                                   QVector<GovernanceStore::LineageEdge> &edges );
    void syncRunAnchors( const sicnu::data::DerivationRecord &record );

    GovernanceStore m_store;
    QString m_storePath;
    /// Last serialized workspace document — the downgrade guard (review P0):
    /// a project opened as v3 and saved with a broken/unavailable store keeps
    /// its governed state instead of being silently rewritten as v1.
    mutable QJsonObject m_cachedProjectJson;
    /// Set when a v3 governed document was read or written in this session;
    /// cleared with the cache on project transitions.
    mutable bool m_v3Seen = false;
    /// Epoch ms of the last PASSING integrity probe (-1 = none/stale); lets
    /// storeIntegrityOk throttle the per-save quick_check walk.
    mutable qint64 m_lastIntegrityOkMs = -1;
    sicnu::data::DataManager *m_dataManager = nullptr;
    QMetaObject::Connection m_assetAddedConn;
    QMetaObject::Connection m_assetChangedConn;
    QMetaObject::Connection m_assetRemovedConn;
};

} // namespace sicnu::workspace
