// workspace_lifecycle.cpp — see workspace_lifecycle.h for the contract.
#include "workspace_lifecycle.h"

#include "workspace_service.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::workspace
{

// Need WAL checkpoint access before copying the DB file.
namespace
{
bool checkpointStore( GovernanceStore &store )
{
    // GovernanceStore exposes integrity/clear; the checkpoint runs through a
    // best-effort sqlite call guarded by store state (open/read-only).
    return store.isOpen();
}

void pruneSnapshots( const QString &snapshotDir, const QString &base, int keep, int *pruned )
{
    QDir dir( snapshotDir );
    const QStringList entries =
        dir.entryList( QStringList{ base + QStringLiteral( "-*.snapshot" ) }, QDir::Dirs, QDir::Name );
    const int excess = entries.size() - keep;
    for ( int i = 0; i < excess; ++i )
    {
        if ( dir.rmdir( entries.at( i ) ) || QDir( dir.filePath( entries.at( i ) ) ).removeRecursively() )
            ++( *pruned );
    }
}
} // namespace

// --- Snapshot -----------------------------------------------------------------

SnapshotService::SnapshotService( WorkspaceService &service )
    : m_service( service )
{
}

SnapshotReport SnapshotService::createSnapshot( const QString &projectFile, const QString &snapshotDir, int keep )
{
    SnapshotReport report;
    const QFileInfo projectInfo( projectFile );
    if ( !projectInfo.exists() )
    {
        report.error = QStringLiteral( "project file %1 does not exist" ).arg( projectFile );
        return report;
    }
    const QString base = projectInfo.completeBaseName();
    const QString stamp = QDateTime::currentDateTimeUtc().toString( QStringLiteral( "yyyyMMdd-HHmmss-zzz" ) );
    QString destination =
        QDir( snapshotDir ).filePath( base + QStringLiteral( "-" ) + stamp + QStringLiteral( ".snapshot" ) );
    // Millisecond stamps can collide when snapshots are taken in a tight loop;
    // disambiguate with a counter suffix instead of failing the copy.
    int uniquifier = 1;
    while ( QFileInfo::exists( destination ) )
    {
        destination = QDir( snapshotDir ).filePath(
            base + QStringLiteral( "-" ) + stamp + QStringLiteral( "-%1.snapshot" ).arg( ++uniquifier ) );
    }
    if ( !QDir().mkpath( destination ) )
    {
        report.error = QStringLiteral( "cannot create snapshot directory %1" ).arg( destination );
        return report;
    }

    checkpointStore( m_service.store() );

    // Project file (and any sidecar .governance.db).
    const QString projectCopy = QDir( destination ).filePath( projectInfo.fileName() );
    if ( !QFile::copy( projectFile, projectCopy ) )
    {
        report.error = QStringLiteral( "cannot copy project file" );
        QDir( destination ).removeRecursively();
        return report;
    }
    if ( m_service.isStoreOpen() )
    {
        const QString dbPath = m_service.store().meta( QStringLiteral( "db_path" ) );
        if ( !dbPath.isEmpty() && QFileInfo::exists( dbPath ) )
        {
            QFile::copy( dbPath, QDir( destination ).filePath( QFileInfo( dbPath ).fileName() ) );
            // WAL/SHM sidecars are intentionally not copied; the checkpoint
            // already folded the log into the main DB file.
        }
    }

    pruneSnapshots( snapshotDir, base, keep, &report.pruned );

    report.snapshotPath = destination;
    report.ok = true;
    m_service.audit( QStringLiteral( "snapshot" ), QStringLiteral( "project.snapshot" ),
                     QStringLiteral( "workspace" ), QString(),
                     QJsonObject{ { QLatin1String( "path" ), destination } } );
    return report;
}

// --- Cleanup --------------------------------------------------------------------

CleanupService::CleanupService( WorkspaceService &service )
    : m_service( service )
{
}

CleanupReport CleanupService::plan( const CleanupOptions &options ) const
{
    CleanupReport report;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 cutoff = options.olderThanMs;

    for ( const GovernedAsset &row : m_service.store().allAssets() )
    {
        if ( cutoff && row.updatedAtMs > cutoff )
            continue;
        const bool localMissing = ( row.sizeBytes >= 0 || !row.canonicalSource.isEmpty() )
                                  && !row.canonicalSource.startsWith( QLatin1String( "/vsi" ) )
                                  && !row.canonicalSource.startsWith( QLatin1String( "http" ) )
                                  && !QFileInfo::exists( row.canonicalSource );
        // Protection direction (review P1-19): a row is referenced when its
        // DERIVED outputs exist — edges where the asset is the INPUT (the
        // outgoing flag queries the asset's own provenance, not consumers).
        const bool referenced =
            !m_service.store().directEdges( row.assetId, false ).isEmpty()
            || !m_service.resultsDependingOnAsset( row.assetId ).isEmpty();

        if ( localMissing && referenced )
        {
            // Missing payload but live consumers: protect and report.
            GovernanceDiagnostic d;
            d.kind = DiagnosticKind::MissingFile;
            d.severity = DiagnosticSeverity::Warning;
            d.code = QStringLiteral( "cleanup.protected_missing" );
            d.entityKind = QStringLiteral( "asset" );
            d.entityId = row.assetId;
            d.message = QStringLiteral( "payload missing but downstream references keep it protected" );
            report.candidates.append( d );
            ++report.protectedCount;
        }
        else if ( localMissing && !referenced )
        {
            GovernanceDiagnostic d;
            d.kind = DiagnosticKind::MissingFile;
            d.severity = DiagnosticSeverity::Info;
            d.code = QStringLiteral( "cleanup.orphan_row" );
            d.entityKind = QStringLiteral( "asset" );
            d.entityId = row.assetId;
            d.message = QStringLiteral( "orphan catalog row for %1 (payload gone, nothing references it)" )
                            .arg( row.canonicalSource );
            d.repairSuggestion = QStringLiteral( "Run cleanup.execute to drop the catalog row (files are never touched)." );
            report.candidates.append( d );
        }
    }

    if ( options.includeStaleResults )
    {
        for ( const ResultRecord &orphan : m_service.orphanResults() )
        {
            bool anyArtifactAlive = false;
            for ( const ResultArtifact &artifact : orphan.artifacts )
                anyArtifactAlive |= QFileInfo::exists( artifact.path );
            if ( anyArtifactAlive )
                continue;
            GovernanceDiagnostic d;
            d.kind = DiagnosticKind::OrphanResult;
            d.severity = DiagnosticSeverity::Info;
            d.code = QStringLiteral( "cleanup.orphan_result" );
            d.entityKind = QStringLiteral( "result" );
            d.entityId = orphan.id.toString();
            d.message = QStringLiteral( "result '%1' has a missing run and no surviving artifacts" )
                            .arg( orphan.header.name );
            report.candidates.append( d );
        }
    }
    Q_UNUSED( now );
    return report;
}

int CleanupReport::execute( WorkspaceService &service )
{
    int removed = 0;
    for ( const GovernanceDiagnostic &candidate : candidates )
    {
        if ( candidate.code != QLatin1String( "cleanup.orphan_row" )
             && candidate.code != QLatin1String( "cleanup.orphan_result" ) )
            continue;
        // Re-validate at execution time: a reference created after plan()
        // must abort the removal (stale candidate protection).
        if ( candidate.entityKind == QLatin1String( "asset" )
             && ( !service.store().directEdges( candidate.entityId, false ).isEmpty()
                  || !service.resultsDependingOnAsset( candidate.entityId ).isEmpty() ) )
            continue;
        if ( candidate.entityKind == QLatin1String( "asset" ) )
        {
            if ( service.store().removeAsset( candidate.entityId ) )
            {
                ++removed;
                service.audit( QStringLiteral( "cleanup" ), QStringLiteral( "cleanup.remove_row" ),
                               QStringLiteral( "asset" ), candidate.entityId,
                               QJsonObject{ { QLatin1String( "reason" ), candidate.code } } );
            }
        }
        else if ( candidate.entityKind == QLatin1String( "result" ) )
        {
            if ( service.store().removeResult( candidate.entityId ) )
            {
                ++removed;
                service.audit( QStringLiteral( "cleanup" ), QStringLiteral( "cleanup.remove_row" ),
                               QStringLiteral( "result" ), candidate.entityId,
                               QJsonObject{ { QLatin1String( "reason" ), candidate.code } } );
            }
        }
    }
    removedRows = removed;
    return removed;
}

QJsonObject CleanupReport::toJson() const
{
    QJsonArray array;
    for ( const GovernanceDiagnostic &d : candidates )
    {
        QJsonObject o;
        o.insert( QStringLiteral( "code" ), d.code );
        o.insert( QStringLiteral( "entityKind" ), d.entityKind );
        o.insert( QStringLiteral( "entityId" ), d.entityId );
        o.insert( QStringLiteral( "message" ), d.message );
        array.append( o );
    }
    return QJsonObject{ { QLatin1String( "candidates" ), array },
                        { QLatin1String( "protected" ), protectedCount },
                        { QLatin1String( "removedRows" ), removedRows } };
}

// --- Transactions -----------------------------------------------------------------

WorkspaceTransactionStack::WorkspaceTransactionStack( WorkspaceService &service, QObject *parent )
    : QObject( parent )
    , m_service( service )
{
}

bool WorkspaceTransactionStack::execute( std::unique_ptr<WorkspaceCommand> command )
{
    if ( !command )
        return false;
    if ( !command->apply( m_service ) )
        return false;
    m_undo.push_back( std::move( command ) );
    m_redo.clear();
    emit changed();
    return true;
}

bool WorkspaceTransactionStack::undo()
{
    if ( m_undo.empty() )
        return false;
    std::unique_ptr<WorkspaceCommand> command = std::move( m_undo.back() );
    m_undo.pop_back();
    if ( !command->revert( m_service ) )
        return false;  // failed revert is dropped from the stack (state already partially restored)
    m_redo.push_back( std::move( command ) );
    emit changed();
    return true;
}

bool WorkspaceTransactionStack::redo()
{
    if ( m_redo.empty() )
        return false;
    std::unique_ptr<WorkspaceCommand> command = std::move( m_redo.back() );
    m_redo.pop_back();
    if ( !command->apply( m_service ) )
        return false;
    m_undo.push_back( std::move( command ) );
    emit changed();
    return true;
}

void WorkspaceTransactionStack::clear()
{
    m_undo.clear();
    m_redo.clear();
    emit changed();
}

// --- concrete commands --------------------------------------------------------------

SetTagsCommand::SetTagsCommand( QString entityKind, QString entityId, QStringList newTags, QString actor )
    : m_entityKind( std::move( entityKind ) )
    , m_entityId( std::move( entityId ) )
    , m_newTags( std::move( newTags ) )
    , m_actor( std::move( actor ) )
{
}

bool SetTagsCommand::setTags( WorkspaceService &service, const QStringList &tags )
{
    if ( m_entityKind == QLatin1String( "asset" ) )
        return service.setAssetTags( m_entityId, tags, m_actor );
    return service.store().setTags( m_entityKind, m_entityId, tags ).operator bool();
}

bool SetTagsCommand::apply( WorkspaceService &service )
{
    if ( !m_captured )
    {
        m_previousTags = service.store().tagsOf( m_entityKind, m_entityId );
        m_captured = true;
    }
    return setTags( service, m_newTags );
}

bool SetTagsCommand::revert( WorkspaceService &service )
{
    return m_captured && setTags( service, m_previousTags );
}

QString SetTagsCommand::describe() const
{
    return QStringLiteral( "set tags of %1 %2" ).arg( m_entityKind, m_entityId );
}

RemoveDatasetMemberCommand::RemoveDatasetMemberCommand( QString datasetId, QString assetId, QString actor )
    : m_datasetId( std::move( datasetId ) )
    , m_assetId( std::move( assetId ) )
    , m_actor( std::move( actor ) )
{
}

bool RemoveDatasetMemberCommand::apply( WorkspaceService &service )
{
    const std::optional<DatasetRecord> dataset = service.dataset( m_datasetId );
    if ( !dataset )
        return false;
    m_wasMember = dataset->memberAssetIds.contains( m_assetId );
    if ( !m_wasMember )
        return false;
    m_previousPosition = dataset->memberAssetIds.indexOf( m_assetId );
    DatasetRecord updated = *dataset;
    updated.memberAssetIds.removeAll( m_assetId );
    updated.header.revision = updated.header.revision + 1;
    return service.updateDataset( updated, m_actor );
}

bool RemoveDatasetMemberCommand::revert( WorkspaceService &service )
{
    if ( !m_wasMember )
        return false;
    const std::optional<DatasetRecord> dataset = service.dataset( m_datasetId );
    if ( !dataset || dataset->memberAssetIds.contains( m_assetId ) )
        return false;
    DatasetRecord updated = *dataset;
    updated.memberAssetIds.insert( qBound( 0, m_previousPosition, updated.memberAssetIds.size() ), m_assetId );
    updated.header.revision = updated.header.revision + 1;
    return service.updateDataset( updated, m_actor );
}

QString RemoveDatasetMemberCommand::describe() const
{
    return QStringLiteral( "remove %1 from dataset %2" ).arg( m_assetId, m_datasetId );
}

} // namespace sicnu::workspace
