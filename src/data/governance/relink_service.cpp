// relink_service.cpp — see relink_service.h for the contract.
#include "relink_service.h"

#include "workspace_service.h"

#include "data_asset.h"
#include "data_manager.h"
#include "source_descriptor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace sicnu::workspace
{

using sicnu::data::AssetId;
using sicnu::data::AssetSnapshot;
using sicnu::data::RelocateRequest;
using sicnu::data::SourceDescriptor;

namespace
{

bool isRemoteLocator( const QString &path )
{
    return path.startsWith( QLatin1String( "/vsi" ) ) || path.startsWith( QLatin1String( "http://" ) )
           || path.startsWith( QLatin1String( "https://" ) ) || path.startsWith( QLatin1String( "s3://" ) )
           || path.startsWith( QLatin1String( "gs://" ) ) || path.startsWith( QLatin1String( "az://" ) );
}

bool isLocalFileLocator( const AssetSnapshot &snapshot )
{
    return snapshot.storageKind() != sicnu::data::StorageKind::Remote
           && !isRemoteLocator( snapshot.source().canonicalSource );
}

QString assetKindName( sicnu::data::AssetKind kind )
{
    switch ( kind )
    {
        case sicnu::data::AssetKind::Raster: return QStringLiteral( "raster" );
        case sicnu::data::AssetKind::Vector: return QStringLiteral( "vector" );
        case sicnu::data::AssetKind::RemoteMap: return QStringLiteral( "remote_map" );
        case sicnu::data::AssetKind::VirtualRaster: return QStringLiteral( "virtual_raster" );
    }
    return QStringLiteral( "raster" );
}

Diagnostic diag( const QString &code, const QString &message,
                 DiagnosticSeverity severity = DiagnosticSeverity::Error )
{
    return Diagnostic{ code, message, severity };
}

} // namespace

RelinkService::RelinkService( sicnu::data::DataManager &dataManager, WorkspaceService &service )
    : m_dataManager( dataManager )
    , m_service( service )
{
}

QVector<MissingAsset> RelinkService::scanMissing() const
{
    QVector<MissingAsset> out;
    for ( const AssetSnapshot &snapshot : m_dataManager.assets() )
    {
        if ( !isLocalFileLocator( snapshot ) )
            continue;
        if ( QFileInfo::exists( snapshot.source().canonicalSource ) )
            continue;
        MissingAsset missing;
        missing.assetId = snapshot.id().toString();
        missing.path = snapshot.source().canonicalSource;
        missing.displayName = snapshot.displayName();
        missing.kind = assetKindName( snapshot.kind() );
        out.append( missing );
    }
    return out;
}

QStringList RelinkService::fingerprintCandidates( const QString &candidatePath, qint64 limit ) const
{
    const QString digest = WorkspaceService::contentFingerprint( candidatePath );
    if ( digest.isEmpty() )
        return {};
    QStringList ids;
    for ( const GovernedAsset &asset : m_service.store().assetsByFingerprint( digest, limit ) )
        ids.append( asset.assetId );
    return ids;
}

bool RelinkService::relinkAsset( const QString &assetId, const QString &newPath, QVector<Diagnostic> *diagnostics )
{
    const std::optional<AssetSnapshot> snapshot = m_dataManager.asset( AssetId::fromString( assetId ).value_or( AssetId() ) );
    if ( !snapshot )
    {
        if ( diagnostics )
            diagnostics->append( diag( QStringLiteral( "relink.unknown_asset" ),
                                       QStringLiteral( "asset %1 is not registered" ).arg( assetId ) ) );
        return false;
    }
    RelocateRequest request;
    request.id = snapshot->id();
    request.replacement.providerKey = snapshot->source().providerKey;
    request.replacement.canonicalSource = QDir::cleanPath( newPath );
    const sicnu::data::Result<sicnu::data::RelocateResult> result = m_dataManager.relocate( request );
    if ( !result )
    {
        if ( diagnostics )
            diagnostics->append( diag( QStringLiteral( "relink.rejected" ),
                                       QStringLiteral( "relocate rejected for %1: %2" )
                                           .arg( assetId, result.diagnostics().isEmpty()
                                                     ? QStringLiteral( "incompatible structure" )
                                                     : result.diagnostics().first().message ) ) );
        return false;
    }
    m_service.audit( QStringLiteral( "relink" ), QStringLiteral( "asset.relink" ), QStringLiteral( "asset" ),
                     assetId, QJsonObject{ { QLatin1String( "from" ), snapshot->source().canonicalSource },
                                           { QLatin1String( "to" ), request.replacement.canonicalSource } } );
    m_service.mirrorAsset( snapshot->id() );
    return true;
}

RelinkOutcome RelinkService::applyRootMove( const QString &fromRoot, const QString &toRoot,
                                            bool verifyFingerprint )
{
    RelinkOutcome outcome;
    const QString from = QDir::cleanPath( fromRoot );
    const QString to = QDir::cleanPath( toRoot );

    for ( const MissingAsset &missing : scanMissing() )
    {
        if ( !missing.path.startsWith( from + QLatin1Char( '/' ) ) && missing.path != from )
        {
            ++outcome.skipped;
            continue;
        }
        const QString suffix = missing.path.mid( from.length() );
        const QString candidate = QDir::cleanPath( to + suffix );
        if ( !QFileInfo::exists( candidate ) )
        {
            ++outcome.skipped;
            outcome.diagnostics.append( diag(
                QStringLiteral( "relink.target_missing" ),
                QStringLiteral( "%1 has no counterpart under %2" ).arg( missing.path, to ),
                DiagnosticSeverity::Warning ) );
            continue;
        }
        applyRelink( missing, candidate, &outcome, verifyFingerprint );
    }

    // Record the mapping for future re-opens (portable project support).
    if ( outcome.relinked > 0 )
        ( void ) m_service.store().upsertPathMapping(
            PathMapping{ QStringLiteral( "externalRoot" ), from, to } );
    m_service.audit( QStringLiteral( "relink" ), QStringLiteral( "project.root_move" ),
                     QStringLiteral( "workspace" ), QString(),
                     QJsonObject{ { QLatin1String( "from" ), from },
                                  { QLatin1String( "to" ), to },
                                  { QLatin1String( "relinked" ), outcome.relinked } } );
    return outcome;
}

RelinkOutcome RelinkService::relinkBulk( const QMap<QString, QString> &remap )
{
    RelinkOutcome outcome;
    for ( auto it = remap.constBegin(); it != remap.constEnd(); ++it )
    {
        MissingAsset missing;
        missing.assetId = it.key();
        applyRelink( missing, it.value(), &outcome, false );
    }
    return outcome;
}

bool RelinkService::applyRelink( const MissingAsset &missing, const QString &newPath,
                                 RelinkOutcome *outcome, bool verifyFingerprint )
{
    QVector<Diagnostic> diagnostics;
    if ( verifyFingerprint && !missing.assetId.isEmpty() )
    {
        const std::optional<GovernedAsset> row = m_service.store().assetById( missing.assetId );
        if ( row && !row->contentFingerprint.isEmpty() )
        {
            const QString digest = WorkspaceService::contentFingerprint( newPath );
            if ( !digest.isEmpty() && digest != row->contentFingerprint )
            {
                ++outcome->failed;
                outcome->diagnostics.append( diag(
                    QStringLiteral( "relink.fingerprint_mismatch" ),
                    QStringLiteral( "%1 does not match the stored fingerprint of %2" )
                        .arg( newPath, missing.assetId ) ) );
                return false;
            }
        }
    }
    if ( relinkAsset( missing.assetId, newPath, &diagnostics ) )
    {
        ++outcome->relinked;
        return true;
    }
    ++outcome->failed;
    outcome->diagnostics.append( diagnostics );
    return false;
}

QJsonObject RelinkOutcome::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "relinked" ), relinked );
    o.insert( QStringLiteral( "failed" ), failed );
    o.insert( QStringLiteral( "skipped" ), skipped );
    QJsonArray array;
    for ( const Diagnostic &d : diagnostics )
    {
        QJsonObject dobj;
        dobj.insert( QStringLiteral( "code" ), d.code );
        dobj.insert( QStringLiteral( "message" ), d.message );
        array.append( dobj );
    }
    o.insert( QStringLiteral( "diagnostics" ), array );
    return o;
}

} // namespace sicnu::workspace
