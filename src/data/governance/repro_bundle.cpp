// repro_bundle.cpp — see repro_bundle.h for the contract.
#include "repro_bundle.h"

#include "workspace_service.h"

#include "data_asset.h"
#include "data_manager.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gdal.h>
#include <gdal_version.h>

namespace sicnu::workspace
{

using sicnu::data::AssetSnapshot;

namespace
{

QJsonObject writeJson( const QString &path, const QJsonObject &document, QStringList *warnings )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        if ( warnings )
            warnings->append( QStringLiteral( "cannot write %1" ).arg( path ) );
        return {};
    }
    file.write( QJsonDocument( document ).toJson( QJsonDocument::Indented ) );
    return document;
}

QJsonObject environmentSummary()
{
    QJsonObject env;
    env.insert( QStringLiteral( "gdal" ), QString::fromUtf8( GDALVersionInfo( "RELEASE_NAME" ) ) );
    env.insert( QStringLiteral( "qt" ), QString::fromUtf8( qVersion() ) );
#if defined( Q_PROCESSOR_X86_64 )
    env.insert( QStringLiteral( "arch" ), QStringLiteral( "x86_64" ) );
#endif
#ifdef Q_OS_LINUX
    env.insert( QStringLiteral( "os" ), QStringLiteral( "linux" ) );
#elif defined( Q_OS_MACOS )
    env.insert( QStringLiteral( "os" ), QStringLiteral( "macos" ) );
#elif defined( Q_OS_WIN )
    env.insert( QStringLiteral( "os" ), QStringLiteral( "windows" ) );
#endif
    return env;
}

QString modeToString( ReproBundleOptions::Mode mode )
{
    switch ( mode )
    {
        case ReproBundleOptions::Mode::ReferenceOnly: return QStringLiteral( "reference_only" );
        case ReproBundleOptions::Mode::MetadataOnly: return QStringLiteral( "metadata_only" );
        case ReproBundleOptions::Mode::Portable: return QStringLiteral( "portable" );
    }
    return QStringLiteral( "reference_only" );
}

} // namespace

ReproBundleExporter::ReproBundleExporter( sicnu::data::DataManager &dataManager, WorkspaceService &service )
    : m_dataManager( dataManager )
    , m_service( service )
{
}

ReproBundleReport ReproBundleExporter::exportBundle( const ReproBundleOptions &options ) const
{
    ReproBundleReport report;
    if ( options.outputDir.isEmpty() )
    {
        report.warnings.append( QStringLiteral( "output directory is empty" ) );
        return report;
    }
    if ( !QDir().mkpath( options.outputDir ) )
    {
        report.warnings.append( QStringLiteral( "cannot create %1" ).arg( options.outputDir ) );
        return report;
    }

    // ---- inputs ---------------------------------------------------------------
    QJsonArray inputArray;
    qint64 copiedBytes = 0;
    for ( const AssetSnapshot &snapshot : m_dataManager.assets() )
    {
        if ( snapshot.persistence() == sicnu::data::PersistencePolicy::TaskTemporary )
            continue;
        QJsonObject input;
        input.insert( QStringLiteral( "assetId" ), snapshot.id().toString() );
        input.insert( QStringLiteral( "kind" ), snapshot.displayName().isEmpty()
                                                     ? snapshot.source().canonicalSource
                                                     : snapshot.displayName() );
        input.insert( QStringLiteral( "locator" ), snapshot.source().canonicalSource );
        input.insert( QStringLiteral( "revision" ), ( qint64 ) snapshot.revision().value() );
        const std::optional<GovernedAsset> row = m_service.store().assetById( snapshot.id().toString() );
        if ( row )
        {
            if ( options.mode != ReproBundleOptions::Mode::ReferenceOnly )
                input.insert( QStringLiteral( "contentFingerprint" ), row->contentFingerprint );
            if ( row->sizeBytes >= 0 )
                input.insert( QStringLiteral( "sizeBytes" ), row->sizeBytes );
            if ( !row->sensor.isEmpty() )
                input.insert( QStringLiteral( "sensor" ), row->sensor );
            if ( !row->modality.isEmpty() )
                input.insert( QStringLiteral( "modality" ), row->modality );
        }
        if ( options.mode == ReproBundleOptions::Mode::Portable
             && snapshot.storageKind() == sicnu::data::StorageKind::File )
        {
            const QString digest = WorkspaceService::contentFingerprint( snapshot.source().canonicalSource );
            const QString suffix = QFileInfo( snapshot.source().canonicalSource ).suffix();
            const QString relative = digest.isEmpty()
                                         ? QStringLiteral( "data/unhashed_%1" ).arg( inputArray.size() )
                                         : QStringLiteral( "data/%1.%2" ).arg( digest, suffix.isEmpty() ? QStringLiteral( "bin" ) : suffix );
            const QString destination = QDir( options.outputDir ).filePath( relative );
            const bool exists = QFileInfo::exists( destination );  // same digest = same content
            if ( exists || ( QDir().mkpath( QFileInfo( destination ).absolutePath() )
                             && QFile::copy( snapshot.source().canonicalSource, destination ) ) )
            {
                if ( !exists )
                {
                    const qint64 bytes = QFileInfo( destination ).size();
                    copiedBytes += bytes;
                    ++report.copiedCount;
                    if ( copiedBytes > options.portableMaxBytes )
                    {
                        report.warnings.append(
                            QStringLiteral( "portable copy budget exceeded; remaining inputs are references" ) );
                        // Stop copying; the rest degrade to reference-only entries.
                        const_cast<ReproBundleOptions &>( options ).mode = ReproBundleOptions::Mode::MetadataOnly;
                    }
                }
                input.insert( QStringLiteral( "bundlePath" ), relative );
            }
            else
                report.warnings.append( QStringLiteral( "copy failed for %1" ).arg( snapshot.source().canonicalSource ) );
        }
        inputArray.append( input );
        ++report.inputCount;
    }
    writeJson( QDir( options.outputDir ).filePath( QStringLiteral( "inputs.json" ) ),
               QJsonObject{ { QLatin1String( "inputs" ), inputArray } }, &report.warnings );

    // ---- workflows ------------------------------------------------------------
    QJsonArray workflowArray;
    for ( const RunRecord &run : m_service.runs() )
    {
        QJsonObject runJson;
        runJson.insert( QStringLiteral( "runId" ), run.id );
        runJson.insert( QStringLiteral( "workflowId" ), run.workflowId );
        runJson.insert( QStringLiteral( "state" ), run.state );
        runJson.insert( QStringLiteral( "startedMs" ), run.startedMs );
        runJson.insert( QStringLiteral( "finishedMs" ), run.finishedMs );
        runJson.insert( QStringLiteral( "summary" ), run.summary );
        workflowArray.append( runJson );
        ++report.workflowCount;
    }
    writeJson( QDir( options.outputDir ).filePath( QStringLiteral( "workflows.json" ) ),
               QJsonObject{ { QLatin1String( "runs" ), workflowArray } }, &report.warnings );

    // ---- results --------------------------------------------------------------
    QJsonArray resultArray;
    for ( const ResultRecord &result : m_service.results() )
    {
        QJsonObject resultJson;
        resultJson.insert( QStringLiteral( "id" ), result.id.toString() );
        resultJson.insert( QStringLiteral( "semanticType" ), resultSemanticTypeToString( result.semanticType ) );
        resultJson.insert( QStringLiteral( "status" ), resultStatusToString( result.status ) );
        resultJson.insert( QStringLiteral( "producer" ), result.producer );
        resultJson.insert( QStringLiteral( "metrics" ), result.metrics );
        QJsonArray inputs;
        for ( const ResultInput &input : result.inputs )
        {
            QJsonObject inputJson;
            inputJson.insert( QStringLiteral( "assetId" ), input.assetId );
            inputJson.insert( QStringLiteral( "revision" ), ( qint64 ) input.revision );
            inputs.append( inputJson );
        }
        resultJson.insert( QStringLiteral( "inputs" ), inputs );
        QJsonArray artifacts;
        for ( const ResultArtifact &artifact : result.artifacts )
        {
            QJsonObject artifactJson;
            artifactJson.insert( QStringLiteral( "path" ), artifact.path );
            artifactJson.insert( QStringLiteral( "digest" ), artifact.contentDigest );
            artifacts.append( artifactJson );
        }
        resultJson.insert( QStringLiteral( "artifacts" ), artifacts );
        resultArray.append( resultJson );
        ++report.resultCount;
    }
    writeJson( QDir( options.outputDir ).filePath( QStringLiteral( "results.json" ) ),
               QJsonObject{ { QLatin1String( "results" ), resultArray } }, &report.warnings );

    // ---- provenance slice ------------------------------------------------------
    QJsonArray edgeArray;
    for ( const AssetSnapshot &snapshot : m_dataManager.assets() )
    {
        for ( const GovernanceStore::LineageEdge &edge :
              m_service.store().directEdges( snapshot.id().toString(), true ) )
        {
            QJsonObject edgeJson;
            edgeJson.insert( QStringLiteral( "output" ), edge.outputAssetId );
            edgeJson.insert( QStringLiteral( "input" ), edge.inputAssetId );
            edgeJson.insert( QStringLiteral( "inputRevision" ), ( qint64 ) edge.inputRevision );
            edgeJson.insert( QStringLiteral( "operatorId" ), edge.operatorId );
            edgeJson.insert( QStringLiteral( "runId" ), edge.runId );
            edgeJson.insert( QStringLiteral( "fingerprint" ), edge.fingerprint );
            edgeArray.append( edgeJson );
        }
    }
    writeJson( QDir( options.outputDir ).filePath( QStringLiteral( "provenance.json" ) ),
               QJsonObject{ { QLatin1String( "edges" ), edgeArray } }, &report.warnings );

    // ---- manifest ---------------------------------------------------------------
    QJsonObject manifest;
    manifest.insert( QStringLiteral( "formatVersion" ), 1 );
    manifest.insert( QStringLiteral( "kind" ), QStringLiteral( "exp_rs_repro_bundle" ) );
    manifest.insert( QStringLiteral( "mode" ), modeToString( options.mode ) );
    manifest.insert( QStringLiteral( "createdAtUtc" ),
                     QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ) );
    manifest.insert( QStringLiteral( "projectId" ), m_service.store().meta( QStringLiteral( "project_id" ) ) );
    manifest.insert( QStringLiteral( "softwareVersion" ), QStringLiteral( "SICNU GEO RS workspace-3.0" ) );
    if ( options.includeEnvironment )
        manifest.insert( QStringLiteral( "environment" ), environmentSummary() );
    manifest.insert( QStringLiteral( "inputs" ), report.inputCount );
    manifest.insert( QStringLiteral( "workflows" ), report.workflowCount );
    manifest.insert( QStringLiteral( "results" ), report.resultCount );
    writeJson( QDir( options.outputDir ).filePath( QStringLiteral( "manifest.json" ) ), manifest, &report.warnings );

    report.manifestPath = QDir( options.outputDir ).filePath( QStringLiteral( "manifest.json" ) );
    report.copiedBytes = copiedBytes;
    report.ok = report.warnings.isEmpty();
    m_service.audit( QStringLiteral( "export" ), QStringLiteral( "project.repro_bundle" ),
                     QStringLiteral( "workspace" ), QString(),
                     QJsonObject{ { QLatin1String( "mode" ), modeToString( options.mode ) },
                                  { QLatin1String( "inputs" ), report.inputCount } } );
    return report;
}

QJsonObject ReproBundleReport::toJson() const
{
    QJsonArray warningArray;
    for ( const QString &warning : warnings )
        warningArray.append( warning );
    return QJsonObject{ { QLatin1String( "ok" ), ok },
                        { QLatin1String( "manifestPath" ), manifestPath },
                        { QLatin1String( "inputs" ), inputCount },
                        { QLatin1String( "workflows" ), workflowCount },
                        { QLatin1String( "results" ), resultCount },
                        { QLatin1String( "copiedCount" ), copiedCount },
                        { QLatin1String( "copiedBytes" ), copiedBytes },
                        { QLatin1String( "warnings" ), warningArray } };
}

} // namespace sicnu::workspace
