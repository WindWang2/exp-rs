// workspace_service.cpp — see workspace_service.h for the contract.
#include "workspace_service.h"

#include "asset_types.h"
#include "artifact_store.h"
#include "data_asset.h"
#include "data_manager.h"
#include "derivation_record.h"
#include "source_descriptor.h"

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace sicnu::workspace
{

using sicnu::data::AssetId;
using sicnu::data::AssetKind;
using sicnu::data::AssetSnapshot;
using sicnu::data::AssetState;
using sicnu::data::DataManager;
using sicnu::data::PersistencePolicy;

namespace
{

QString assetKindToString( AssetKind kind )
{
    switch ( kind )
    {
        case AssetKind::Raster: return QStringLiteral( "raster" );
        case AssetKind::Vector: return QStringLiteral( "vector" );
        case AssetKind::RemoteMap: return QStringLiteral( "remote_map" );
        case AssetKind::VirtualRaster: return QStringLiteral( "virtual_raster" );
    }
    return QStringLiteral( "raster" );
}

QString assetStateToString( AssetState state )
{
    switch ( state )
    {
        case AssetState::Registered: return QStringLiteral( "Registered" );
        case AssetState::Resolving: return QStringLiteral( "Resolving" );
        case AssetState::Ready: return QStringLiteral( "Ready" );
        case AssetState::Missing: return QStringLiteral( "Missing" );
        case AssetState::UnavailableSource: return QStringLiteral( "UnavailableSource" );
        case AssetState::Offline: return QStringLiteral( "Offline" );
        case AssetState::AuthenticationRequired: return QStringLiteral( "AuthenticationRequired" );
        case AssetState::Error: return QStringLiteral( "Error" );
        case AssetState::Stale: return QStringLiteral( "Stale" );
    }
    return QStringLiteral( "Registered" );
}

QString persistenceToString( PersistencePolicy policy )
{
    switch ( policy )
    {
        case PersistencePolicy::ProjectPersistent: return QStringLiteral( "project" );
        case PersistencePolicy::SessionTemporary: return QStringLiteral( "session" );
        case PersistencePolicy::TaskTemporary: return QStringLiteral( "task" );
    }
    return QStringLiteral( "project" );
}

QString tagsToText( const QStringList &tags )
{
    return tags.join( QLatin1Char( ',' ) );
}

QStringList tagsFromText( const QString &text )
{
    QStringList out;
    for ( const QStringView part : QStringView( text ).split( u',' ) )
    {
        const QString trimmed = part.trimmed().toString();
        if ( !trimmed.isEmpty() )
            out.append( trimmed );
    }
    return out;
}

// Bands metadata (roles) → compact summary used by the enrichment columns.
QString bandRolesSummary( const sicnu::data::RasterStructure &raster )
{
    QStringList roles;
    for ( const sicnu::data::RasterBandStructure &band : raster.bands )
    {
        if ( band.role != sicnu::data::BandRole::Unknown )
            roles.append( sicnu::data::bandRoleToString( band.role ) );
    }
    return roles.join( QLatin1Char( ',' ) );
}

} // namespace

WorkspaceService::WorkspaceService( QObject *parent )
    : QObject( parent )
{
}

WorkspaceService::~WorkspaceService()
{
    closeStore();
}

bool WorkspaceService::openStore( const QString &dbPath, QString *errorOut )
{
    if ( m_store.isOpen() && m_store.meta( QStringLiteral( "db_path" ) ) != dbPath )
        m_store.close();  // path change: never bleed governed state across projects
    if ( !m_store.open( dbPath, errorOut ) )
        return false;
    m_store.setMeta( QStringLiteral( "opened_at" ),
                     QString::number( QDateTime::currentMSecsSinceEpoch() ) );
    return true;
}

void WorkspaceService::closeStore()
{
    if ( m_dataManager )
        bindDataManager( nullptr );
    m_store.close();
}

QString WorkspaceService::defaultStorePathFor( const QString &projectFile )
{
    QFileInfo info( projectFile );
    return info.dir().filePath( info.completeBaseName() + QStringLiteral( ".governance.db" ) );
}

// --- DataManager mirroring ---------------------------------------------------

void WorkspaceService::bindDataManager( DataManager *manager )
{
    if ( m_assetAddedConn )
        QObject::disconnect( m_assetAddedConn );
    if ( m_assetChangedConn )
        QObject::disconnect( m_assetChangedConn );
    if ( m_assetRemovedConn )
        QObject::disconnect( m_assetRemovedConn );
    m_dataManager = manager;
    if ( !m_dataManager )
        return;

    m_assetAddedConn = connect( m_dataManager, &DataManager::assetAdded, this,
                                &WorkspaceService::onAssetAdded );
    m_assetChangedConn = connect( m_dataManager, &DataManager::assetChanged, this,
                                  &WorkspaceService::onAssetChanged );
    m_assetRemovedConn = connect( m_dataManager, &DataManager::assetRemoved, this,
                                  &WorkspaceService::onAssetRemoved );
}

qint64 WorkspaceService::mirrorAllAssets( bool reconcileGhosts )
{
    if ( !m_dataManager )
        return 0;
    const QVector<AssetSnapshot> snapshots = m_dataManager->assets();
    qint64 mirrored = 0;
    // Review-3 (N+1): batch store writes — one transaction per 256-row chunk
    // keeps bulk mirroring of large catalogs fast and memory bounded.
    constexpr int kBatchSize = 256;
    QVector<GovernedAsset> batch;
    QVector<GovernanceStore::LineageEdge> edgeBatch;
    QVector<QPair<QString, QString>> runOutputBatch;
    QHash<QString, RunRecord> runBatch;
    batch.reserve( kBatchSize );
    auto flush = [ & ]() {
        if ( !batch.isEmpty() )
        {
            ( void ) m_store.upsertAssets( batch );
            batch.clear();
        }
        if ( !edgeBatch.isEmpty() )
        {
            ( void ) m_store.addLineageEdges( edgeBatch );
            edgeBatch.clear();
        }
        for ( auto it = runBatch.constBegin(); it != runBatch.constEnd(); ++it )
            ( void ) m_store.upsertRun( it.value() );  // runs are few; deduped per mirror
        runBatch.clear();
        if ( !runOutputBatch.isEmpty() )
        {
            ( void ) m_store.addRunOutputs( runOutputBatch );
            runOutputBatch.clear();
        }
    };
    for ( const AssetSnapshot &snapshot : snapshots )
    {
        const std::optional<GovernedAsset> row = governedRowForSnapshot( snapshot );
        if ( row )
        {
            ++mirrored;
            batch.append( *row );
        }
        // Run anchors collected into the same batch (review P1-13: per-asset
        // transactions defeated the bulk path).
        if ( m_dataManager )
        {
            const std::optional<sicnu::data::DerivationRecord> record =
                m_dataManager->provenance( snapshot.id() );
            if ( record && !record->workflowRunId.isEmpty() )
            {
                RunRecord run;
                run.id = record->workflowRunId;
                run.workflowId = record->workflowId;
                run.state = QStringLiteral( "Completed" );
                if ( record->completedAtUtc.isValid() )
                    run.finishedMs = record->completedAtUtc.toMSecsSinceEpoch();
                run.header.name = record->workflowId.isEmpty() ? record->workflowRunId : record->workflowId;
                runBatch.insert( run.id, run );
                runOutputBatch.append( qMakePair( record->workflowRunId, snapshot.id().toString() ) );
            }
        }
        if ( batch.size() >= kBatchSize )
        {
            flush();
            batch.reserve( kBatchSize );
        }
    }
    flush();

    // Review P2-6: reconcile ghost rows — assets removed while the service
    // was unbound would otherwise live forever in the durable index. Row-only
    // deletion; opt-in so a shared headless store never loses other sessions.
    if ( reconcileGhosts )
    {
        QSet<QString> liveIds;
        liveIds.reserve( snapshots.size() );
        for ( const AssetSnapshot &snapshot : snapshots )
            liveIds.insert( snapshot.id().toString() );
        for ( const QString &storedId : m_store.assetIds() )
        {
            if ( !liveIds.contains( storedId ) )
                ( void ) m_store.removeAsset( storedId );
        }
    }
    return mirrored;
}

bool WorkspaceService::mirrorAsset( const AssetId &id )
{
    if ( !m_dataManager )
        return false;
    const std::optional<AssetSnapshot> snapshot = m_dataManager->asset( id );
    if ( !snapshot )
        return false;
    const bool ok = mirrorSnapshot( *snapshot );
    syncDerivationLineage( *snapshot );
    return ok;
}

void WorkspaceService::onAssetAdded( const AssetId &id )
{
    if ( mirrorAsset( id ) )
        emit entityChanged( QStringLiteral( "asset" ), id.toString() );
}

void WorkspaceService::onAssetChanged( const AssetId &id )
{
    if ( mirrorAsset( id ) )
        emit entityChanged( QStringLiteral( "asset" ), id.toString() );
}

void WorkspaceService::onAssetRemoved( const AssetId &id )
{
    // Catalog removal only — never touches payload bytes.
    ( void ) m_store.removeAsset( id.toString() );
    emit entityChanged( QStringLiteral( "asset" ), id.toString() );
}

bool WorkspaceService::mirrorSnapshot( const AssetSnapshot &snapshot )
{
    const std::optional<GovernedAsset> row = governedRowForSnapshot( snapshot );
    if ( !row )
        return false;
    return m_store.upsertAsset( *row ).operator bool();
}

std::optional<GovernedAsset> WorkspaceService::governedRowForSnapshot( const AssetSnapshot &snapshot ) const
{
    GovernedAsset row;
    row.assetId = snapshot.id().toString();
    // SourceKey itself is opaque; the identity text pairs provider and
    // canonical locator so the catalog can dedup on the same material.
    row.sourceKey = snapshot.source().providerKey + QLatin1Char( '|' ) + snapshot.source().canonicalSource;
    row.canonicalSource = snapshot.source().canonicalSource;
    row.kind = assetKindToString( snapshot.kind() );
    row.state = assetStateToString( snapshot.state() );
    row.persistence = persistenceToString( snapshot.persistence() );
    row.displayName = snapshot.displayName();
    if ( snapshot.parentCollectionId() )
        row.parentCollectionId = snapshot.parentCollectionId()->toString();
    if ( snapshot.acquisitionTime() )
        row.acquisitionMs = snapshot.acquisitionTime()->toMSecsSinceEpoch();
    row.revision = snapshot.revision().value();

    // Structure-derived enrichment when available.
    if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &snapshot.structure() ) )
    {
        row.format = raster->driverName;
        row.crs = raster->crsWkt;
        row.bandCount = raster->bandCount;
        row.bandRoles = bandRolesSummary( *raster );
        QJsonObject extent;
        if ( raster->extent.valid )
        {
            extent.insert( QStringLiteral( "minX" ), raster->extent.minimumX );
            extent.insert( QStringLiteral( "minY" ), raster->extent.minimumY );
            extent.insert( QStringLiteral( "maxX" ), raster->extent.maximumX );
            extent.insert( QStringLiteral( "maxY" ), raster->extent.maximumY );
        }
        row.metadata.insert( QStringLiteral( "extent" ), extent );
        row.metadata.insert( QStringLiteral( "width" ), raster->width );
        row.metadata.insert( QStringLiteral( "height" ), raster->height );
    }
    else if ( const auto *vector = std::get_if<sicnu::data::VectorStructure>( &snapshot.structure() ) )
    {
        row.format = vector->driverName;
        if ( !vector->layers.isEmpty() )
        {
            row.crs = vector->layers.first().crsWkt;
            QJsonObject layers;
            layers.insert( QStringLiteral( "count" ), vector->layerCount );
            row.metadata.insert( QStringLiteral( "layers" ), layers );
        }
    }

    // Preserve enrichment columns the DataManager does not own when re-mirroring.
    if ( const std::optional<GovernedAsset> existing = m_store.assetById( row.assetId ) )
    {
        row.contentFingerprint = existing->contentFingerprint;
        row.sizeBytes = existing->sizeBytes;
        row.mtimeMs = existing->mtimeMs;
        row.verifiedMs = existing->verifiedMs;
        row.sensor = existing->sensor;
        row.modality = existing->modality;
        row.availability = existing->availability;
        row.bandCount = row.bandCount >= 0 ? row.bandCount : existing->bandCount;
        row.bandRoles = row.bandRoles.isEmpty() ? existing->bandRoles : row.bandRoles;
        row.format = row.format.isEmpty() ? existing->format : row.format;
        row.crs = row.crs.isEmpty() ? existing->crs : row.crs;
    }

    return row;
}

void WorkspaceService::collectDerivationLineage( const AssetSnapshot &snapshot,
                                                 QVector<GovernanceStore::LineageEdge> &edges )
{
    if ( !m_dataManager )
        return;
    const std::optional<sicnu::data::DerivationRecord> record =
        m_dataManager->provenance( snapshot.id() );
    if ( !record || record->inputs.isEmpty() )
        return;

    for ( const sicnu::data::DerivationInput &input : record->inputs )
    {
        GovernanceStore::LineageEdge edge;
        edge.outputAssetId = snapshot.id().toString();
        edge.inputAssetId = input.assetId.toString();
        edge.inputRevision = input.revision.value();
        edge.operatorId = record->algorithmId;
        edge.runId = record->workflowRunId;
        edge.stepId = record->stepId;
        edge.fingerprint = record->executionFingerprint;
        edges.append( edge );
    }
    // Run anchors are recorded by the caller: batched in mirrorAllAssets,
    // direct in syncDerivationLineage.
    if ( !record->workflowRunId.isEmpty() )
        syncRunAnchors( *record );
}

void WorkspaceService::syncDerivationLineage( const AssetSnapshot &snapshot )
{
    if ( !m_dataManager )
        return;
    QVector<GovernanceStore::LineageEdge> edges;
    collectDerivationLineage( snapshot, edges );
    if ( edges.isEmpty() )
        return;
    ( void ) m_store.addLineageEdges( edges );
}

void WorkspaceService::syncRunAnchors( const sicnu::data::DerivationRecord &record )
{
    // Producer anchor: workflow/run bookkeeping.
    if ( record.workflowRunId.isEmpty() )
        return;
    RunRecord run;
    run.id = record.workflowRunId;
    run.workflowId = record.workflowId;
    run.state = QStringLiteral( "Completed" );
    if ( record.completedAtUtc.isValid() )
        run.finishedMs = record.completedAtUtc.toMSecsSinceEpoch();
    run.header.name = record.workflowId.isEmpty() ? record.workflowRunId : record.workflowId;
    ( void ) m_store.upsertRun( run );
    m_store.linkRunOutput( record.workflowRunId, record.outputAssetId.toString() );
}

// --- asset enrichment ----------------------------------------------------------

bool WorkspaceService::setAssetTags( const QString &assetId, const QStringList &tags, const QString &actor )
{
    const bool ok = m_store.setTags( QStringLiteral( "asset" ), assetId, tags ).operator bool();
    if ( ok )
    {
        audit( actor, QStringLiteral( "asset.set_tags" ), QStringLiteral( "asset" ), assetId,
               QJsonObject{ { QLatin1String( "tags" ), QJsonArray::fromStringList( tags ) } } );
        emit entityChanged( QStringLiteral( "asset" ), assetId );
    }
    return ok;
}

bool WorkspaceService::bulkTagAssets( const QStringList &assetIds, const QString &tag, const QString &actor )
{
    QVector<QString> ids;
    ids.reserve( assetIds.size() );
    for ( const QString &id : assetIds )
        ids.append( id );
    const sicnu::data::Result<qint64> result = m_store.bulkTag( ids, QStringLiteral( "asset" ), tag );
    if ( result )
    {
        audit( actor, QStringLiteral( "asset.bulk_tag" ), QStringLiteral( "asset" ), QString(),
               QJsonObject{ { QLatin1String( "tag" ), tag },
                            { QLatin1String( "touched" ), result.value() },
                            { QLatin1String( "requested" ), ( qint64 ) assetIds.size() } } );
        emit entityChanged( QStringLiteral( "asset" ), QString() );
        return true;
    }
    return false;
}

bool WorkspaceService::noteAssetVerified( const QString &assetId, const QString &fingerprint,
                                          qint64 sizeBytes, qint64 mtimeMs )
{
    std::optional<GovernedAsset> row = m_store.assetById( assetId );
    if ( !row )
        return false;
    const bool contentChanged = !row->contentFingerprint.isEmpty()
                                && !fingerprint.isEmpty()
                                && row->contentFingerprint != fingerprint;
    const QString previousFingerprint = row->contentFingerprint;
    row->contentFingerprint = fingerprint.isEmpty() ? row->contentFingerprint : fingerprint;
    row->sizeBytes = sizeBytes;
    row->mtimeMs = mtimeMs;
    row->verifiedMs = QDateTime::currentMSecsSinceEpoch();
    // Content change invalidates the freshness stamp and ADVANCES the asset
    // revision (identity contract: observed change => new revision).
    row->availability = contentChanged ? QStringLiteral( "stale" ) : QStringLiteral( "fresh" );
    if ( contentChanged )
        row->revision = row->revision + 1;
    const bool ok = m_store.upsertAsset( *row ).operator bool();
    if ( ok && contentChanged )
        audit( QStringLiteral( "system" ), QStringLiteral( "asset.content_changed" ),
               QStringLiteral( "asset" ), assetId,
               QJsonObject{ { QLatin1String( "previousFingerprint" ), previousFingerprint },
                            { QLatin1String( "newFingerprint" ), row->contentFingerprint } } );
    return ok;
}

// --- datasets --------------------------------------------------------------------

DatasetId WorkspaceService::createDataset( const QString &name, DatasetKind kind,
                                           const QStringList &memberAssetIds, const QString &actor )
{
    DatasetRecord record;
    record.id = DatasetId::generate();
    record.kind = kind;
    record.header.name = name;
    record.header.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    record.header.updatedAtMs = record.header.createdAtMs;
    record.memberAssetIds = memberAssetIds;
    if ( updateDataset( record, actor ) )
        return record.id;
    return DatasetId();
}

bool WorkspaceService::updateDataset( const DatasetRecord &record, const QString &actor )
{
    if ( !m_store.upsertDataset( record ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "dataset.upsert" ), QStringLiteral( "dataset" ), record.id.toString(),
           QJsonObject{ { QLatin1String( "name" ), record.header.name },
                        { QLatin1String( "kind" ), datasetKindToString( record.kind ) },
                        { QLatin1String( "members" ), ( qint64 ) record.memberAssetIds.size() } } );
    emit entityChanged( QStringLiteral( "dataset" ), record.id.toString() );
    return true;
}

bool WorkspaceService::removeDataset( const QString &datasetId, const QString &actor )
{
    if ( !m_store.removeDataset( datasetId ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "dataset.remove" ), QStringLiteral( "dataset" ), datasetId );
    emit entityChanged( QStringLiteral( "dataset" ), datasetId );
    return true;
}

// --- results ------------------------------------------------------------------------

ResultId WorkspaceService::registerResult( ResultRecord record, const QString &actor )
{
    if ( record.id.isNull() )
        record.id = ResultId::generate();
    record.header.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    record.header.updatedAtMs = record.header.createdAtMs;
    // Lineage: result inputs mirror into the asset-level graph only when they
    // reference the produced artifact asset; results link producer anchors via
    // producer JSON (runs table), not via asset edges.
    if ( !m_store.upsertResult( record ).operator bool() )
        return ResultId();
    audit( actor, QStringLiteral( "result.register" ), QStringLiteral( "result" ), record.id.toString(),
           QJsonObject{ { QLatin1String( "semanticType" ), resultSemanticTypeToString( record.semanticType ) },
                        { QLatin1String( "name" ), record.header.name } } );
    emit entityChanged( QStringLiteral( "result" ), record.id.toString() );
    return record.id;
}

bool WorkspaceService::changeResultStatus( const QString &resultId, ResultStatus to,
                                           const QString &actor, const QString &notes )
{
    std::optional<ResultRecord> record = m_store.resultById( resultId );
    if ( !record )
        return false;
    if ( !isLegalResultTransition( record->status, to ) )
        return false;
    record->status = to;
    record->header.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    record->header.revision = record->header.revision + 1;
    if ( !notes.isEmpty() )
        record->validationNotes = notes;
    if ( !m_store.upsertResult( *record ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "result.status" ), QStringLiteral( "result" ), resultId,
           QJsonObject{ { QLatin1String( "to" ), resultStatusToString( to ) },
                        { QLatin1String( "notes" ), notes } } );
    emit entityChanged( QStringLiteral( "result" ), resultId );
    return true;
}

QVector<ResultRecord> WorkspaceService::resultsDependingOnAsset( const QString &assetId ) const
{
    return m_store.resultsDependingOnAsset( assetId );
}

QVector<ResultRecord> WorkspaceService::orphanResults() const
{
    return m_store.orphanResults();
}

// --- runs ---------------------------------------------------------------------------

void WorkspaceService::recordRun( const RunRecord &run )
{
    ( void ) m_store.upsertRun( run );
    emit entityChanged( QStringLiteral( "run" ), run.id );
}

// --- experiments ----------------------------------------------------------------------

ExperimentId WorkspaceService::createExperiment( const QString &name, const QString &objective,
                                                 const QString &actor )
{
    ExperimentRecord record;
    record.id = ExperimentId::generate();
    record.header.name = name;
    record.objective = objective;
    record.header.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    record.header.updatedAtMs = record.header.createdAtMs;
    if ( !m_store.upsertExperiment( record ).operator bool() )
        return ExperimentId();
    audit( actor, QStringLiteral( "experiment.create" ), QStringLiteral( "experiment" ),
           record.id.toString(), QJsonObject{ { QLatin1String( "name" ), name } } );
    emit entityChanged( QStringLiteral( "experiment" ), record.id.toString() );
    return record.id;
}

bool WorkspaceService::updateExperiment( const ExperimentRecord &record, const QString &actor )
{
    if ( !m_store.upsertExperiment( record ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "experiment.update" ), QStringLiteral( "experiment" ),
           record.id.toString() );
    emit entityChanged( QStringLiteral( "experiment" ), record.id.toString() );
    return true;
}

bool WorkspaceService::removeExperiment( const QString &experimentId, const QString &actor )
{
    if ( !m_store.removeExperiment( experimentId ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "experiment.remove" ), QStringLiteral( "experiment" ), experimentId );
    emit entityChanged( QStringLiteral( "experiment" ), experimentId );
    return true;
}

// --- smart collections ------------------------------------------------------------------

SmartCollectionId WorkspaceService::createSmartCollection( const QString &name,
                                                           const QVector<SmartPredicate> &predicates,
                                                           const QString &actor )
{
    SmartCollectionRecord record;
    record.id = SmartCollectionId::generate();
    record.header.name = name;
    record.predicates = predicates;
    if ( !m_store.upsertSmartCollection( record ).operator bool() )
        return SmartCollectionId();
    audit( actor, QStringLiteral( "smart.create" ), QStringLiteral( "smart" ), record.id.toString(),
           QJsonObject{ { QLatin1String( "name" ), name },
                        { QLatin1String( "predicates" ), ( qint64 ) predicates.size() } } );
    emit entityChanged( QStringLiteral( "smart" ), record.id.toString() );
    return record.id;
}

bool WorkspaceService::removeSmartCollection( const QString &collectionId, const QString &actor )
{
    if ( !m_store.removeSmartCollection( collectionId ).operator bool() )
        return false;
    audit( actor, QStringLiteral( "smart.remove" ), QStringLiteral( "smart" ), collectionId );
    emit entityChanged( QStringLiteral( "smart" ), collectionId );
    return true;
}

WorkspacePage WorkspaceService::evaluateSmartCollection( const QString &collectionId,
                                                         qint64 offset, qint64 limit ) const
{
    const std::optional<SmartCollectionRecord> record = m_store.smartCollectionById( collectionId );
    if ( !record )
        return {};
    WorkspaceQuery q;
    q.set = EntitySet::Assets;
    q.offset = offset;
    q.limit = limit;
    for ( const SmartPredicate &p : record->predicates )
    {
        const QString field = p.field;
        if ( field == QLatin1String( "sensor" ) ) q.sensor = p.value;
        else if ( field == QLatin1String( "modality" ) ) q.modality = p.value;
        else if ( field == QLatin1String( "crs" ) ) q.crs = p.value;
        else if ( field == QLatin1String( "kind" ) ) q.kind = p.value;
        else if ( field == QLatin1String( "state" ) ) q.state = p.value;
        else if ( field == QLatin1String( "tag" ) ) q.tag = p.value;
        else if ( field == QLatin1String( "text" ) ) q.text = p.value;
        else if ( field == QLatin1String( "year" ) )
        {
            bool ok = false;
            const int year = p.value.toInt( &ok );
            if ( !ok )
                continue;
            QDateTime start = QDateTime( QDate( year, 1, 1 ), QTime( 0, 0 ), Qt::UTC );
            QDateTime end = start.addYears( 1 );
            if ( p.op == QLatin1String( "eq" ) )
            {
                q.acquiredFromMs = start.toMSecsSinceEpoch();
                q.acquiredToMs = end.toMSecsSinceEpoch() - 1;
            }
            else if ( p.op == QLatin1String( "gte" ) )
            {
                q.acquiredFromMs = start.toMSecsSinceEpoch();
            }
            else if ( p.op == QLatin1String( "lte" ) )
            {
                q.acquiredToMs = end.toMSecsSinceEpoch() - 1;
            }
        }
        // Unsupported field/op combos are ignored (never fail the query).
    }
    return m_store.query( q );
}

// --- lineage -----------------------------------------------------------------------------

QVariantMap WorkspaceService::impactAnalysis( const QString &assetId, int maxDepth ) const
{
    QVariantMap out;
    const QVector<QVariantMap> downstream = m_store.lineageDownstream( assetId, maxDepth );
    QStringList affectedAssets;
    for ( const QVariantMap &row : downstream )
        affectedAssets.append( row.value( QStringLiteral( "assetId" ) ).toString() );

    // Single join over the affected set (review P2-16: the per-asset query +
    // linear dedup loop stalled the store mutex on hub assets).
    QStringList affectedList;
    for ( const QString &asset : affectedAssets )
        affectedList.append( asset );
    QVariantList affectedResults;
    QSet<QString> seen;
    for ( const ResultRecord &result : m_store.resultsDependingOnAssets( affectedList ) )
    {
        const QString id = result.id.toString();
        if ( !seen.contains( id ) )
        {
            seen.insert( id );
            affectedResults.append( id );
        }
    }
    out.insert( QStringLiteral( "assetId" ), assetId );
    out.insert( QStringLiteral( "affectedAssets" ), affectedAssets );
    out.insert( QStringLiteral( "affectedResults" ), affectedResults );
    out.insert( QStringLiteral( "affectedAssetCount" ), ( qint64 ) affectedAssets.size() );
    out.insert( QStringLiteral( "affectedResultCount" ), ( qint64 ) affectedResults.size() );
    return out;
}

QVariantMap WorkspaceService::producerOf( const QString &assetId ) const
{
    QVariantMap out;
    out.insert( QStringLiteral( "assetId" ), assetId );
    const QVector<GovernanceStore::LineageEdge> edges = m_store.directEdges( assetId, true );
    if ( !edges.isEmpty() )
    {
        const GovernanceStore::LineageEdge &edge = edges.first();
        out.insert( QStringLiteral( "operatorId" ), edge.operatorId );
        out.insert( QStringLiteral( "runId" ), edge.runId );
        out.insert( QStringLiteral( "stepId" ), edge.stepId );
        out.insert( QStringLiteral( "executionFingerprint" ), edge.fingerprint );
    }
    if ( const std::optional<RunRecord> run = m_store.runById( edges.isEmpty() ? QString() : edges.first().runId ) )
    {
        out.insert( QStringLiteral( "workflowId" ), run->workflowId );
        out.insert( QStringLiteral( "runState" ), run->state );
    }
    return out;
}

// --- audit ---------------------------------------------------------------------------------

bool WorkspaceService::audit( const QString &actor, const QString &action,
                              const QString &entityKind, const QString &entityId,
                              const QJsonObject &detail )
{
    return m_store.appendAudit( actor, action, entityKind, entityId, detail ).operator bool();
}

// --- project document ------------------------------------------------------------------------

QJsonObject WorkspaceService::toProjectJson() const
{
    QJsonObject root;
    root.insert( QStringLiteral( "schemaVersion" ), 1 );

    QJsonArray datasetArray;
    for ( const DatasetRecord &ds : m_store.datasets( -1 ) )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), ds.id.toString() );
        o.insert( QStringLiteral( "kind" ), datasetKindToString( ds.kind ) );
        o.insert( QStringLiteral( "name" ), ds.header.name );
        o.insert( QStringLiteral( "revision" ), ( qint64 ) ds.header.revision );
        o.insert( QStringLiteral( "tags" ), QJsonArray::fromStringList( ds.header.tags ) );
        o.insert( QStringLiteral( "metadata" ), ds.header.metadata );
        QJsonArray members;
        for ( const QString &member : ds.memberAssetIds )
            members.append( member );
        o.insert( QStringLiteral( "members" ), members );
        datasetArray.append( o );
    }
    root.insert( QStringLiteral( "datasets" ), datasetArray );

    QJsonArray resultArray;
    for ( const ResultRecord &r : m_store.results( -1 ) )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), r.id.toString() );
        o.insert( QStringLiteral( "semanticType" ), resultSemanticTypeToString( r.semanticType ) );
        o.insert( QStringLiteral( "status" ), resultStatusToString( r.status ) );
        o.insert( QStringLiteral( "name" ), r.header.name );
        o.insert( QStringLiteral( "revision" ), ( qint64 ) r.header.revision );
        o.insert( QStringLiteral( "producer" ), r.producer );
        o.insert( QStringLiteral( "metrics" ), r.metrics );
        o.insert( QStringLiteral( "quality" ), r.quality );
        o.insert( QStringLiteral( "metadata" ), r.header.metadata );
        o.insert( QStringLiteral( "tags" ), QJsonArray::fromStringList( r.header.tags ) );
        o.insert( QStringLiteral( "supersededBy" ), r.supersededBy );
        o.insert( QStringLiteral( "validationNotes" ), r.validationNotes );
        QJsonArray inputs;
        for ( const ResultInput &in : r.inputs )
        {
            QJsonObject io;
            io.insert( QStringLiteral( "assetId" ), in.assetId );
            io.insert( QStringLiteral( "revision" ), ( qint64 ) in.revision );
            io.insert( QStringLiteral( "role" ), in.role );
            inputs.append( io );
        }
        o.insert( QStringLiteral( "inputs" ), inputs );
        QJsonArray artifacts;
        for ( const ResultArtifact &art : r.artifacts )
        {
            QJsonObject ao;
            ao.insert( QStringLiteral( "path" ), art.path );
            ao.insert( QStringLiteral( "role" ), art.role );
            ao.insert( QStringLiteral( "digest" ), art.contentDigest );
            ao.insert( QStringLiteral( "size" ), art.sizeBytes );
            artifacts.append( ao );
        }
        o.insert( QStringLiteral( "artifacts" ), artifacts );
        resultArray.append( o );
    }
    root.insert( QStringLiteral( "results" ), resultArray );

    QJsonArray runArray;
    for ( const RunRecord &r : m_store.runs( -1 ) )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), r.id );
        o.insert( QStringLiteral( "workflowId" ), r.workflowId );
        o.insert( QStringLiteral( "state" ), r.state );
        o.insert( QStringLiteral( "name" ), r.header.name );
        o.insert( QStringLiteral( "startedMs" ), r.startedMs );
        o.insert( QStringLiteral( "finishedMs" ), r.finishedMs );
        o.insert( QStringLiteral( "summary" ), r.summary );
        QJsonArray outputs;
        for ( const QString &output : r.outputAssetIds )
            outputs.append( output );
        o.insert( QStringLiteral( "outputs" ), outputs );
        runArray.append( o );
    }
    root.insert( QStringLiteral( "runs" ), runArray );

    QJsonArray experimentArray;
    for ( const ExperimentRecord &e : m_store.experiments( -1 ) )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), e.id.toString() );
        o.insert( QStringLiteral( "name" ), e.header.name );
        o.insert( QStringLiteral( "objective" ), e.objective );
        o.insert( QStringLiteral( "tags" ), QJsonArray::fromStringList( e.header.tags ) );
        QJsonArray variants;
        for ( const ExperimentVariant &v : e.variants )
        {
            QJsonObject vo;
            vo.insert( QStringLiteral( "key" ), v.key );
            vo.insert( QStringLiteral( "value" ), v.value );
            variants.append( vo );
        }
        o.insert( QStringLiteral( "variants" ), variants );
        o.insert( QStringLiteral( "runs" ), QJsonArray::fromStringList( e.runIds ) );
        experimentArray.append( o );
    }
    root.insert( QStringLiteral( "experiments" ), experimentArray );

    QJsonArray smartArray;
    for ( const SmartCollectionRecord &c : m_store.smartCollections() )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), c.id.toString() );
        o.insert( QStringLiteral( "name" ), c.header.name );
        QJsonArray predicates;
        for ( const SmartPredicate &p : c.predicates )
        {
            QJsonObject po;
            po.insert( QStringLiteral( "field" ), p.field );
            po.insert( QStringLiteral( "op" ), p.op );
            po.insert( QStringLiteral( "value" ), p.value );
            predicates.append( po );
        }
        o.insert( QStringLiteral( "predicates" ), predicates );
        smartArray.append( o );
    }
    root.insert( QStringLiteral( "smartCollections" ), smartArray );

    QJsonArray exportArray;
    for ( const ExportRecord &e : m_store.exports( -1 ) )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "id" ), e.id.toString() );
        o.insert( QStringLiteral( "kind" ), e.kind );
        o.insert( QStringLiteral( "target" ), e.target );
        o.insert( QStringLiteral( "resultId" ), e.resultId );
        o.insert( QStringLiteral( "name" ), e.header.name );
        exportArray.append( o );
    }
    root.insert( QStringLiteral( "exports" ), exportArray );

    QJsonArray tagArray;
    for ( const GovernanceStore::TagRow &row : m_store.allTags() )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "entityKind" ), row.entityKind );
        o.insert( QStringLiteral( "entityId" ), row.entityId );
        o.insert( QStringLiteral( "value" ), row.tag );
        tagArray.append( o );
    }
    root.insert( QStringLiteral( "tags" ), tagArray );

    QJsonArray mappingArray;
    for ( const PathMapping &m : m_store.pathMappings() )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "kind" ), m.kind );
        o.insert( QStringLiteral( "from" ), m.fromPath );
        o.insert( QStringLiteral( "to" ), m.toPath );
        mappingArray.append( o );
    }
    root.insert( QStringLiteral( "pathMappings" ), mappingArray );

    // Downgrade guard (review P0): remember the last serialized document so a
    // later save with the store unavailable can re-persist governed state.
    m_cachedProjectJson = root;
    return root;
}

QVector<Diagnostic> WorkspaceService::fromProjectJson( const QJsonObject &root )
{
    QVector<Diagnostic> diagnostics;
    // The project document is authoritative on read: stale local entity rows
    // (deleted elsewhere, DB restored from backup) must not resurrect.
    m_cachedProjectJson = root;

    // Review P2-12: unknown top-level sections are reported, then skipped —
    // forward tolerance is explicit, not silent.
    static const QStringList kKnownSections = {
        QStringLiteral( "schemaVersion" ), QStringLiteral( "datasets" ), QStringLiteral( "results" ),
        QStringLiteral( "runs" ), QStringLiteral( "experiments" ), QStringLiteral( "smartCollections" ),
        QStringLiteral( "exports" ), QStringLiteral( "pathMappings" ), QStringLiteral( "tags" ),
    };
    for ( const QString &key : root.keys() )
    {
        if ( !kKnownSections.contains( key ) )
            diagnostics.append( Diagnostic{ QStringLiteral( "workspace.unknown_section" ),
                                            QStringLiteral( "skipped unknown workspace section '%1'" ).arg( key ),
                                            DiagnosticSeverity::Warning } );
    }

    auto skipUnknown = [ & ]( const QString &section ) {
        diagnostics.append( Diagnostic{ QStringLiteral( "workspace.unknown_section" ),
                                        QStringLiteral( "skipped unknown workspace section '%1'" ).arg( section ),
                                        DiagnosticSeverity::Warning } );
    };

    // Datasets.
    for ( const QJsonValue &v : root.value( QLatin1String( "datasets" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const std::optional<DatasetId> id = DatasetId::fromString( o.value( QLatin1String( "id" ) ).toString() );
        const std::optional<DatasetKind> kind = datasetKindFromString( o.value( QLatin1String( "kind" ) ).toString() );
        if ( !id || !kind )
        {
            diagnostics.append( Diagnostic{ QStringLiteral( "workspace.dataset_invalid" ),
                                            QStringLiteral( "dataset entry skipped: malformed id/kind" ),
                                            DiagnosticSeverity::Warning } );
            continue;
        }
        DatasetRecord ds;
        ds.id = *id;
        ds.kind = *kind;
        ds.header.name = o.value( QLatin1String( "name" ) ).toString();
        ds.header.revision = ( quint64 ) qMax<qint64>( 1, o.value( QLatin1String( "revision" ) ).toInt( 1 ) );
        ds.header.tags = o.value( QLatin1String( "tags" ) ).toVariant().toStringList();
        ds.header.metadata = o.value( QLatin1String( "metadata" ) ).toObject();
        for ( const QJsonValue &m : o.value( QLatin1String( "members" ) ).toArray() )
            ds.memberAssetIds.append( m.toString() );
        ( void ) m_store.upsertDataset( ds );
    }

    // Results.
    for ( const QJsonValue &v : root.value( QLatin1String( "results" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const std::optional<ResultId> id = ResultId::fromString( o.value( QLatin1String( "id" ) ).toString() );
        if ( !id )
        {
            diagnostics.append( Diagnostic{ QStringLiteral( "workspace.result_invalid" ),
                                            QStringLiteral( "result entry skipped: malformed id" ),
                                            DiagnosticSeverity::Warning } );
            continue;
        }
        ResultRecord r;
        r.id = *id;
        r.semanticType = resultSemanticTypeFromString( o.value( QLatin1String( "semanticType" ) ).toString() )
                             .value_or( ResultSemanticType::Other );
        r.status = resultStatusFromString( o.value( QLatin1String( "status" ) ).toString() )
                       .value_or( ResultStatus::Draft );
        r.header.name = o.value( QLatin1String( "name" ) ).toString();
        r.header.revision = ( quint64 ) qMax<qint64>( 1, o.value( QLatin1String( "revision" ) ).toInt( 1 ) );
        r.producer = o.value( QLatin1String( "producer" ) ).toObject();
        r.metrics = o.value( QLatin1String( "metrics" ) ).toObject();
        r.quality = o.value( QLatin1String( "quality" ) ).toObject();
        r.header.metadata = o.value( QLatin1String( "metadata" ) ).toObject();
        r.header.tags = o.value( QLatin1String( "tags" ) ).toVariant().toStringList();
        r.supersededBy = o.value( QLatin1String( "supersededBy" ) ).toString();
        r.validationNotes = o.value( QLatin1String( "validationNotes" ) ).toString();
        for ( const QJsonValue &iv : o.value( QLatin1String( "inputs" ) ).toArray() )
        {
            const QJsonObject io = iv.toObject();
            ResultInput in;
            in.assetId = io.value( QLatin1String( "assetId" ) ).toString();
            in.revision = ( quint64 ) io.value( QLatin1String( "revision" ) ).toInt( 0 );
            in.role = io.value( QLatin1String( "role" ) ).toString( QStringLiteral( "input" ) );
            r.inputs.append( in );
        }
        for ( const QJsonValue &av : o.value( QLatin1String( "artifacts" ) ).toArray() )
        {
            const QJsonObject ao = av.toObject();
            ResultArtifact art;
            art.path = ao.value( QLatin1String( "path" ) ).toString();
            art.role = ao.value( QLatin1String( "role" ) ).toString( QStringLiteral( "primary" ) );
            art.contentDigest = ao.value( QLatin1String( "digest" ) ).toString();
            art.sizeBytes = ao.value( QLatin1String( "size" ) ).toInt( -1 );
            r.artifacts.append( art );
        }
        ( void ) m_store.upsertResult( r );
    }

    // Runs.
    for ( const QJsonValue &v : root.value( QLatin1String( "runs" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        RunRecord run;
        run.id = o.value( QLatin1String( "id" ) ).toString();
        if ( run.id.isEmpty() )
            continue;
        run.workflowId = o.value( QLatin1String( "workflowId" ) ).toString();
        run.state = o.value( QLatin1String( "state" ) ).toString();
        run.header.name = o.value( QLatin1String( "name" ) ).toString();
        run.startedMs = o.value( QLatin1String( "startedMs" ) ).toInt( 0 );
        run.finishedMs = o.value( QLatin1String( "finishedMs" ) ).toInt( 0 );
        run.summary = o.value( QLatin1String( "summary" ) ).toObject();
        ( void ) m_store.upsertRun( run );
    }

    // Experiments.
    for ( const QJsonValue &v : root.value( QLatin1String( "experiments" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const std::optional<ExperimentId> id = ExperimentId::fromString( o.value( QLatin1String( "id" ) ).toString() );
        if ( !id )
            continue;
        ExperimentRecord e;
        e.id = *id;
        e.header.name = o.value( QLatin1String( "name" ) ).toString();
        e.objective = o.value( QLatin1String( "objective" ) ).toString();
        e.header.tags = o.value( QLatin1String( "tags" ) ).toVariant().toStringList();
        for ( const QJsonValue &vv : o.value( QLatin1String( "variants" ) ).toArray() )
        {
            const QJsonObject vo = vv.toObject();
            e.variants.append( ExperimentVariant{ vo.value( QLatin1String( "key" ) ).toString(),
                                                  vo.value( QLatin1String( "value" ) ).toObject() } );
        }
        for ( const QJsonValue &rv : o.value( QLatin1String( "runs" ) ).toArray() )
            e.runIds.append( rv.toString() );
        ( void ) m_store.upsertExperiment( e );
    }

    // Smart collections.
    for ( const QJsonValue &v : root.value( QLatin1String( "smartCollections" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const std::optional<SmartCollectionId> id =
            SmartCollectionId::fromString( o.value( QLatin1String( "id" ) ).toString() );
        if ( !id )
            continue;
        SmartCollectionRecord c;
        c.id = *id;
        c.header.name = o.value( QLatin1String( "name" ) ).toString();
        for ( const QJsonValue &pv : o.value( QLatin1String( "predicates" ) ).toArray() )
        {
            const QJsonObject po = pv.toObject();
            c.predicates.append( SmartPredicate{ po.value( QLatin1String( "field" ) ).toString(),
                                                  po.value( QLatin1String( "op" ) ).toString(),
                                                  po.value( QLatin1String( "value" ) ).toString() } );
        }
        ( void ) m_store.upsertSmartCollection( c );
    }

    // Exports.
    for ( const QJsonValue &v : root.value( QLatin1String( "exports" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const std::optional<ExportId> id = ExportId::fromString( o.value( QLatin1String( "id" ) ).toString() );
        if ( !id )
            continue;
        ExportRecord e;
        e.id = *id;
        e.kind = o.value( QLatin1String( "kind" ) ).toString();
        e.target = o.value( QLatin1String( "target" ) ).toString();
        e.resultId = o.value( QLatin1String( "resultId" ) ).toString();
        e.header.name = o.value( QLatin1String( "name" ) ).toString();
        ( void ) m_store.upsertExport( e );
    }

    // Path mappings.
    for ( const QJsonValue &v : root.value( QLatin1String( "pathMappings" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        PathMapping m;
        m.kind = o.value( QLatin1String( "kind" ) ).toString( QStringLiteral( "externalRoot" ) );
        m.fromPath = o.value( QLatin1String( "from" ) ).toString();
        m.toPath = o.value( QLatin1String( "to" ) ).toString();
        if ( !m.fromPath.isEmpty() )
            ( void ) m_store.upsertPathMapping( m );
    }

    // Tags of every entity kind.
    for ( const QJsonValue &v : root.value( QLatin1String( "tags" ) ).toArray() )
    {
        const QJsonObject o = v.toObject();
        const QString entityKind = o.value( QLatin1String( "entityKind" ) ).toString();
        const QString entityId = o.value( QLatin1String( "entityId" ) ).toString();
        const QString tag = o.value( QLatin1String( "value" ) ).toString();
        if ( entityKind.isEmpty() || entityId.isEmpty() || tag.isEmpty() )
            continue;
        ( void ) m_store.addTag( entityKind, entityId, tag );
    }

    Q_UNUSED( skipUnknown );
    return diagnostics;
}

QString WorkspaceService::contentFingerprint( const QString &path )
{
    QString error;
    const QString digest = sicnu::data::artifactContentDigest( path, &error );
    return error.isEmpty() ? digest : QString();
}

} // namespace sicnu::workspace
