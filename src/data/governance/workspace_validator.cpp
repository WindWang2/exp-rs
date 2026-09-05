// workspace_validator.cpp — see workspace_validator.h for the contract.
#include "workspace_validator.h"

#include "workspace_service.h"

#include "collection_types.h"
#include "data_asset.h"
#include "data_manager.h"
#include "source_descriptor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace sicnu::workspace
{

using sicnu::data::AssetId;
using sicnu::data::AssetSnapshot;
using sicnu::data::CollectionId;
using sicnu::data::CollectionSnapshot;

namespace
{

bool isRemoteLocator( const QString &path )
{
    return path.startsWith( QLatin1String( "/vsi" ) ) || path.startsWith( QLatin1String( "http://" ) )
           || path.startsWith( QLatin1String( "https://" ) ) || path.startsWith( QLatin1String( "s3://" ) )
           || path.startsWith( QLatin1String( "gs://" ) ) || path.startsWith( QLatin1String( "az://" ) );
}

GovernanceDiagnostic makeDiag( DiagnosticKind kind, DiagnosticSeverity severity, const QString &code,
                               const QString &entityKind, const QString &entityId, const QString &message,
                               const QString &repair = QString() )
{
    GovernanceDiagnostic d;
    d.kind = kind;
    d.severity = severity;
    d.code = code;
    d.entityKind = entityKind;
    d.entityId = entityId;
    d.message = message;
    d.repairSuggestion = repair;
    return d;
}

} // namespace

WorkspaceValidator::WorkspaceValidator( sicnu::data::DataManager &dataManager, WorkspaceService &service )
    : m_dataManager( dataManager )
    , m_service( service )
{
}

ValidationReport WorkspaceValidator::validateProject( const ValidationOptions &options ) const
{
    ValidationReport report;
    const QVector<AssetSnapshot> snapshots = m_dataManager.assets();

    // 1. Asset-level checks.
    for ( const AssetSnapshot &snapshot : snapshots )
    {
        if ( report.diagnostics.size() >= options.maxDiagnostics )
            break;
        checkAsset( snapshot.id().toString(), options, report );
    }

    // 2. Duplicate source identity (different ids, same provider|canonical key).
    QHash<QString, int> keyCounts;
    for ( const AssetSnapshot &snapshot : snapshots )
        keyCounts[snapshot.source().providerKey + QLatin1Char( '|' ) + snapshot.source().canonicalSource]++;
    for ( const AssetSnapshot &snapshot : snapshots )
    {
        const QString key = snapshot.source().providerKey + QLatin1Char( '|' ) + snapshot.source().canonicalSource;
        if ( keyCounts.value( key ) > 1 )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::DuplicateAsset, DiagnosticSeverity::Info, QStringLiteral( "asset.duplicate_source" ),
                QStringLiteral( "asset" ), snapshot.id().toString(),
                QStringLiteral( "%1 assets share the same source identity (%2)" )
                    .arg( keyCounts.value( key ) )
                    .arg( snapshot.source().canonicalSource ),
                QStringLiteral( "Consolidate via the asset browser; sources are deduplicated on registration." ) ) );
            report.summary.add( report.diagnostics.last() );
            if ( report.diagnostics.size() >= options.maxDiagnostics )
                break;
        }
    }

    // 3. Collections: member presence.
    for ( const CollectionId &collectionId : m_dataManager.collections() )
    {
        const std::optional<CollectionSnapshot> collectionSnapshot = m_dataManager.collection( collectionId );
        if ( !collectionSnapshot )
            continue;
        for ( const AssetId &child : collectionSnapshot->childAssetIds )
        {
            if ( !m_dataManager.asset( child ) )
            {
                report.diagnostics.append( makeDiag(
                    DiagnosticKind::CollectionMemberMissing, DiagnosticSeverity::Error,
                    QStringLiteral( "collection.member_missing" ), QStringLiteral( "collection" ),
                    collectionId.toString(),
                    QStringLiteral( "collection '%1' references missing asset %2" )
                        .arg( collectionSnapshot->displayName, child.toString() ),
                    QStringLiteral( "Remove the stale reference or relink the asset." ) ) );
                report.summary.add( report.diagnostics.last() );
            }
        }
    }

    // 4. Datasets: member presence.
    for ( const DatasetRecord &dataset : m_service.datasets() )
    {
        for ( const QString &member : dataset.memberAssetIds )
        {
            if ( !m_service.store().assetById( member ) )
            {
                report.diagnostics.append( makeDiag(
                    DiagnosticKind::CollectionMemberMissing, DiagnosticSeverity::Error,
                    QStringLiteral( "dataset.member_missing" ), QStringLiteral( "dataset" ),
                    dataset.id.toString(),
                    QStringLiteral( "dataset '%1' references missing asset %2" )
                        .arg( dataset.header.name, member ),
                    QStringLiteral( "Remove the stale member or relink the asset." ) ) );
                report.summary.add( report.diagnostics.last() );
            }
        }
    }

    // 5. Results: artifact existence, orphans, producer anchors.
    for ( const ResultRecord &result : m_service.results() )
    {
        for ( const ResultArtifact &artifact : result.artifacts )
        {
            if ( !artifact.path.isEmpty() && !QFileInfo::exists( artifact.path ) )
            {
                report.diagnostics.append( makeDiag(
                    DiagnosticKind::MissingFile, DiagnosticSeverity::Error,
                    QStringLiteral( "result.artifact_missing" ), QStringLiteral( "result" ),
                    result.id.toString(),
                    QStringLiteral( "result '%1' artifact %2 is missing" ).arg( result.header.name, artifact.path ),
                    QStringLiteral( "Re-run the producing workflow or mark the result superseded." ) ) );
                report.summary.add( report.diagnostics.last() );
            }
        }
        if ( !result.producer.value( QLatin1String( "runId" ) ).toString().isEmpty()
             && !m_service.run( result.producer.value( QLatin1String( "runId" ) ).toString() ) )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::WorkflowReferenceMissing, DiagnosticSeverity::Warning,
                QStringLiteral( "result.run_missing" ), QStringLiteral( "result" ), result.id.toString(),
                QStringLiteral( "result '%1' references unknown run %2" )
                    .arg( result.header.name, result.producer.value( QLatin1String( "runId" ) ).toString() ),
                QStringLiteral( "Recompute lineage or clear the producer anchor." ) ) );
            report.summary.add( report.diagnostics.last() );
        }
    }
    for ( const ResultRecord &orphan : m_service.orphanResults() )
    {
        report.diagnostics.append( makeDiag(
            DiagnosticKind::OrphanResult, DiagnosticSeverity::Warning, QStringLiteral( "result.orphan" ),
            QStringLiteral( "result" ), orphan.id.toString(),
            QStringLiteral( "result '%1' has no reachable producing run" ).arg( orphan.header.name ),
            QStringLiteral( "Re-run the producer or archive the result." ) ) );
        report.summary.add( report.diagnostics.last() );
    }

    // 6. Governance store integrity.
    const GovernanceDiagnostic integrity = m_service.store().integrityCheck();
    if ( integrity.severity != DiagnosticSeverity::Info )
    {
        report.diagnostics.append( integrity );
        report.summary.add( integrity );
    }

    return report;
}

ValidationReport WorkspaceValidator::validateAsset( const QString &assetId, const ValidationOptions &options ) const
{
    ValidationReport report;
    checkAsset( assetId, options, report );
    return report;
}

void WorkspaceValidator::checkAsset( const QString &assetId, const ValidationOptions &options,
                                     ValidationReport &report ) const
{
    const std::optional<AssetSnapshot> snapshot =
        m_dataManager.asset( sicnu::data::AssetId::fromString( assetId ).value_or( sicnu::data::AssetId() ) );
    if ( !snapshot )
        return;
    const QString path = snapshot->source().canonicalSource;
    const std::optional<GovernedAsset> row = m_service.store().assetById( assetId );

    if ( isRemoteLocator( path ) )
    {
        // Malformed URI check only (scheme presence); network probes stay opt-in
        // so validation never blocks on connectivity.
        if ( path.startsWith( QLatin1String( "http" ) ) && !QUrl( path ).isValid() )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::BrokenUri, DiagnosticSeverity::Error, QStringLiteral( "asset.broken_uri" ),
                QStringLiteral( "asset" ), assetId,
                QStringLiteral( "remote locator %1 does not parse as a URI" ).arg( path ),
                QStringLiteral( "Fix the URL or relink the asset." ) ) );
            report.summary.add( report.diagnostics.last() );
        }
        Q_UNUSED( options );
        return;
    }

    if ( !QFileInfo::exists( path ) )
    {
        report.diagnostics.append( makeDiag(
            DiagnosticKind::MissingFile, DiagnosticSeverity::Error, QStringLiteral( "asset.missing_file" ),
            QStringLiteral( "asset" ), assetId,
            QStringLiteral( "asset '%1' source %2 does not exist" ).arg( snapshot->displayName(), path ),
            QStringLiteral( "Relink to the moved file (asset:relink) or remove the reference." ) ) );
        report.summary.add( report.diagnostics.last() );
        return;
    }

    const QFileInfo info( path );
    const qint64 size = info.size();
    const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
    if ( row && row->sizeBytes >= 0 && ( row->sizeBytes != size || ( row->mtimeMs && row->mtimeMs != mtime ) ) )
    {
        report.diagnostics.append( makeDiag(
            DiagnosticKind::ChangedMetadata, DiagnosticSeverity::Warning, QStringLiteral( "asset.changed_metadata" ),
            QStringLiteral( "asset" ), assetId,
            QStringLiteral( "asset '%1' size/mtime drifted since last verification" ).arg( snapshot->displayName() ),
            QStringLiteral( "Re-verify content (asset:validate --fingerprints) to refresh the revision." ) ) );
        report.summary.add( report.diagnostics.last() );
    }
    if ( options.verifyFingerprints && row && !row->contentFingerprint.isEmpty() )
    {
        const QString digest = WorkspaceService::contentFingerprint( path );
        if ( !digest.isEmpty() && digest != row->contentFingerprint )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::ChangedContent, DiagnosticSeverity::Error, QStringLiteral( "asset.changed_content" ),
                QStringLiteral( "asset" ), assetId,
                QStringLiteral( "content digest of '%1' differs from the registered fingerprint" )
                    .arg( snapshot->displayName() ),
                QStringLiteral( "Re-register or restore the original data; dependent results are stale." ) ) );
            report.summary.add( report.diagnostics.last() );
        }
    }

    if ( const auto *raster = std::get_if<sicnu::data::RasterStructure>( &snapshot->structure() ) )
    {
        if ( raster->crsWkt.isEmpty() )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::CrsMissing, DiagnosticSeverity::Warning, QStringLiteral( "asset.crs_missing" ),
                QStringLiteral( "asset" ), assetId,
                QStringLiteral( "raster '%1' carries no CRS" ).arg( snapshot->displayName() ),
                QStringLiteral( "Assign a CRS before analysis outputs are trusted." ) ) );
            report.summary.add( report.diagnostics.last() );
        }
        if ( row && row->bandCount >= 0 && row->bandCount != raster->bandCount )
        {
            report.diagnostics.append( makeDiag(
                DiagnosticKind::BandCountChanged, DiagnosticSeverity::Error, QStringLiteral( "asset.band_count_changed" ),
                QStringLiteral( "asset" ), assetId,
                QStringLiteral( "band count changed from %1 to %2" ).arg( row->bandCount ).arg( raster->bandCount ),
                QStringLiteral( "Re-derive dependent products; band-indexed workflows may be invalid." ) ) );
            report.summary.add( report.diagnostics.last() );
        }
    }
}

QJsonObject ValidationReport::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "errors" ), summary.errors );
    o.insert( QStringLiteral( "warnings" ), summary.warnings );
    o.insert( QStringLiteral( "infos" ), summary.infos );
    QJsonArray array;
    for ( const GovernanceDiagnostic &d : diagnostics )
    {
        QJsonObject dobj;
        dobj.insert( QStringLiteral( "kind" ), diagnosticKindToString( d.kind ) );
        dobj.insert( QStringLiteral( "severity" ),
                     d.severity == DiagnosticSeverity::Error
                         ? QStringLiteral( "error" )
                         : d.severity == DiagnosticSeverity::Warning ? QStringLiteral( "warning" )
                                                                     : QStringLiteral( "info" ) );
        dobj.insert( QStringLiteral( "code" ), d.code );
        dobj.insert( QStringLiteral( "entityKind" ), d.entityKind );
        dobj.insert( QStringLiteral( "entityId" ), d.entityId );
        dobj.insert( QStringLiteral( "message" ), d.message );
        if ( !d.repairSuggestion.isEmpty() )
            dobj.insert( QStringLiteral( "repair" ), d.repairSuggestion );
        array.append( dobj );
    }
    o.insert( QStringLiteral( "diagnostics" ), array );
    return o;
}

} // namespace sicnu::workspace
