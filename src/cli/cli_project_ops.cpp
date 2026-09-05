/***************************************************************************
 * cli_project_ops.cpp — see cli_project_ops.h for the contract.
 ***************************************************************************/
#include "cli_project_ops.h"

#include <QDomDocument>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <qgsproject.h>

#include "app/data_project_serializer.h"
#include "app/project_context.h"
#include "data/governance/governance_types.h"
#include "data/governance/relink_service.h"
#include "data/governance/repro_bundle.h"
#include "data/governance/workspace_service.h"
#include "data/governance/workspace_validator.h"
#include "exprs/exit_codes.h"

using namespace sicnu::cli;
namespace exprs_ns = exprs;

namespace
{

struct GovernedProject
{
    std::unique_ptr<sicnu::app::ProjectContext> context;
    QgsProject project;
    bool readOk = false;
};

/// Loads the project and its governance state through the same seam the GUI
/// uses (QgsProject + DataProjectSerializer + WorkspaceService).
bool loadGovernedProject( const QString &path, GovernedProject &out, QString *error )
{
    if ( !QFileInfo::exists( path ) )
    {
        if ( error )
            *error = QStringLiteral( "project file not found: %1" ).arg( path );
        return false;
    }
    auto created = sicnu::app::ProjectContext::createHeadless();
    if ( !created )
    {
        if ( error )
            *error = QStringLiteral( "cannot create headless project context" );
        return false;
    }
    out.context = created.take();
    out.context->openWorkspaceStore( path );

    sicnu::app::DataProjectSerializer serializer;
    QObject::connect( &out.project, &QgsProject::readProject, &out.project,
                      [ & ]( const QDomDocument &document ) {
                          ( void ) serializer.read( document, out.project, *out.context );
                      } );
    out.readOk = out.project.read( path );
    if ( !out.readOk )
    {
        if ( error )
            *error = QStringLiteral( "cannot read project: %1" ).arg( out.project.error() );
        return false;
    }
    // Idempotent: mirrors restored assets into the governance store (the
    // serializer already did this for v1/v3 files; safe to repeat).
    out.context->workspaceService().mirrorAllAssets();
    return true;
}

QString argValue( const QStringList &args, const QString &key, const QString &fallback = QString() )
{
    for ( const QString &arg : args )
    {
        if ( arg.startsWith( key + QLatin1Char( '=' ) ) )
            return arg.mid( key.size() + 1 );
    }
    return fallback;
}

Json::Value qJsonObjectToJson( const QJsonObject &object )
{
    const QByteArray raw = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::istringstream stream( raw.toStdString() );
    if ( Json::parseFromStream( builder, stream, &parsed, &errors ) )
        return parsed;
    return Json::Value( Json::objectValue );
}

} // namespace

int sicnu::cli::runProjectGovernanceCommand( const QString &sub, QStringList args, const CliIO &io )
{
    const QString usage = QStringLiteral(
        "usage: project <info|validate|health|search|migrate|relink|lineage|export-manifest|audit> <file.qgz|.qgs> [options]" );

    if ( sub == QLatin1String( "info" ) )
        return -1;  // handled by the caller (legacy path)

    // Every governance subcommand needs a file argument.
    if ( args.isEmpty() )
        return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, usage.toStdString() );
    const QString path = args.takeFirst();

    GovernedProject loaded;
    QString error;
    if ( !loadGovernedProject( path, loaded, &error ) )
        return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, error.toStdString() );
    sicnu::workspace::WorkspaceService &workspace = loaded.context->workspaceService();

    if ( sub == QLatin1String( "validate" ) || sub == QLatin1String( "health" ) )
    {
        sicnu::workspace::ValidationOptions options;
        options.verifyFingerprints = args.contains( QStringLiteral( "--fingerprints" ) );
        const sicnu::workspace::WorkspaceValidator validator( loaded.context->dataManager(), workspace );
        const sicnu::workspace::ValidationReport report = validator.validateProject( options );
        Json::Value data = qJsonObjectToJson( report.toJson() );
        return io.finish( report.ok(), "project." + sub.toStdString(), data,
                          report.ok() ? 0 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::ValidationFailure ) );
    }

    if ( sub == QLatin1String( "search" ) )
    {
        sicnu::workspace::WorkspaceQuery query;
        const QString set = argValue( args, "--set", QStringLiteral( "assets" ) );
        if ( set == QLatin1String( "results" ) ) query.set = sicnu::workspace::EntitySet::Results;
        else if ( set == QLatin1String( "runs" ) ) query.set = sicnu::workspace::EntitySet::Runs;
        else if ( set == QLatin1String( "datasets" ) ) query.set = sicnu::workspace::EntitySet::Datasets;
        if ( !args.isEmpty() && !args.first().startsWith( QLatin1String( "--" ) ) )
            query.text = args.takeFirst();
        query.kind = argValue( args, "--kind" );
        query.state = argValue( args, "--state" );
        query.sensor = argValue( args, "--sensor" );
        query.modality = argValue( args, "--modality" );
        query.crs = argValue( args, "--crs" );
        query.tag = argValue( args, "--tag" );
        query.runId = argValue( args, "--run-id" );
        query.offset = argValue( args, "--offset", QStringLiteral( "0" ) ).toLongLong();
        query.limit = argValue( args, "--limit", QStringLiteral( "50" ) ).toLongLong();

        const QString facet = argValue( args, "--facet" );
        const sicnu::workspace::WorkspacePage page = workspace.query( query, facet );
        Json::Value data( Json::objectValue );
        data["total"] = ( Json::Int64 ) page.total;
        Json::Value items( Json::arrayValue );
        for ( const QVariantMap &row : page.items )
        {
            Json::Value item( Json::objectValue );
            for ( auto it = row.constBegin(); it != row.constEnd(); ++it )
                item[it.key().toStdString()] = it.value().toString().toStdString();
            items.append( item );
        }
        data["items"] = items;
        if ( !facet.isEmpty() )
        {
            Json::Value facets( Json::arrayValue );
            for ( const sicnu::workspace::FacetCount &fc : page.facets )
            {
                Json::Value f( Json::objectValue );
                f["value"] = fc.value.toStdString();
                f["count"] = ( Json::Int64 ) fc.count;
                facets.append( f );
            }
            data["facets"] = facets;
        }
        return io.finish( true, "project.search", data, 0 );
    }

    if ( sub == QLatin1String( "migrate" ) )
    {
        // A successful load already performed the in-memory M1 migration; the
        // report quantifies it. --write persists format v3 (atomic save).
        const bool write = args.contains( QStringLiteral( "--write" ) );
        Json::Value data( Json::objectValue );
        data["path"] = path.toStdString();
        data["assetsIndexed"] = ( Json::Int64 ) workspace.store().assetCount();
        data["governanceStore"] = workspace.isStoreOpen();
        if ( write )
        {
            sicnu::app::DataProjectSerializer serializer;
            bool writeOk = false;
            QObject::connect( &loaded.project, &QgsProject::writeProject, &loaded.project,
                              [ & ]( QDomDocument &document ) {
                                  const sicnu::data::Result<void> result =
                                      serializer.write( document, *loaded.context );
                                  writeOk = static_cast<bool>( result );
                              } );
            const bool wrote = loaded.project.write( path );
            if ( !writeOk || !wrote )
            {
                const std::string detail = writeOk
                    ? ( "cannot write project: " + loaded.project.error().toStdString() )
                    : "serializer refused the v3 workspace block";
                return io.finish( false, "project.migrate", data,
                                  exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError ),
                                  {}, detail );
            }
            data["persisted"] = true;
        }
        return io.finish( true, "project.migrate", data, 0 );
    }

    if ( sub == QLatin1String( "relink" ) )
    {
        sicnu::workspace::RelinkService relink( loaded.context->dataManager(), workspace );
        const QString from = argValue( args, "--from" );
        const QString to = argValue( args, "--to" );
        const QString assetId = argValue( args, "--asset" );
        const QString newPath = argValue( args, "--path" );
        const bool verify = args.contains( QStringLiteral( "--verify-fingerprints" ) );

        if ( !from.isEmpty() && !to.isEmpty() )
        {
            const sicnu::workspace::RelinkOutcome outcome = relink.applyRootMove( from, to, verify );
            return io.finish( outcome.failed == 0, "project.relink", qJsonObjectToJson( outcome.toJson() ),
                              outcome.failed == 0 ? 0 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError ) );
        }
        if ( !assetId.isEmpty() && !newPath.isEmpty() )
        {
            QVector<sicnu::data::Diagnostic> diagnostics;
            const bool relinked = relink.relinkAsset( assetId, newPath, &diagnostics );
            Json::Value data( Json::objectValue );
            data["relinked"] = relinked;
            Json::Value diags( Json::arrayValue );
            for ( const sicnu::data::Diagnostic &d : diagnostics )
            {
                Json::Value dj( Json::objectValue );
                dj["code"] = d.code.toStdString();
                dj["message"] = d.message.toStdString();
                diags.append( dj );
            }
            data["diagnostics"] = diags;
            return io.finish( relinked, "project.relink", data,
                              relinked ? 0 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError ) );
        }
        return io.finish( false, "project.relink", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                          {}, "pass --from/--to (root move) or --asset/--path (single relink)" );
    }

    if ( sub == QLatin1String( "lineage" ) )
    {
        const QString assetId = argValue( args, "--asset" );
        if ( assetId.isEmpty() )
            return io.finish( false, "project.lineage", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "pass --asset=ID" );
        const QString direction = argValue( args, "--direction", QStringLiteral( "upstream" ) );
        const int depth = argValue( args, "--depth", QStringLiteral( "25" ) ).toInt();
        const QVector<QVariantMap> nodes = direction == QLatin1String( "downstream" )
                                               ? workspace.lineageDownstream( assetId, depth )
                                               : workspace.lineageUpstream( assetId, depth );
        Json::Value data( Json::objectValue );
        data["assetId"] = assetId.toStdString();
        data["direction"] = direction.toStdString();
        data["count"] = ( Json::Int64 ) nodes.size();
        Json::Value arr( Json::arrayValue );
        for ( const QVariantMap &node : nodes )
        {
            Json::Value n( Json::objectValue );
            n["assetId"] = node.value( QStringLiteral( "assetId" ) ).toString().toStdString();
            n["depth"] = ( Json::Int64 ) node.value( QStringLiteral( "depth" ) ).toLongLong();
            arr.append( n );
        }
        data["nodes"] = arr;
        return io.finish( true, "project.lineage", data, 0 );
    }

    if ( sub == QLatin1String( "export-manifest" ) )
    {
        sicnu::workspace::ReproBundleOptions options;
        options.outputDir = argValue( args, "--out" );
        const QString mode = argValue( args, "--mode", QStringLiteral( "metadata_only" ) );
        if ( mode == QLatin1String( "reference_only" ) )
            options.mode = sicnu::workspace::ReproBundleOptions::Mode::ReferenceOnly;
        else if ( mode == QLatin1String( "portable" ) )
            options.mode = sicnu::workspace::ReproBundleOptions::Mode::Portable;
        if ( options.outputDir.isEmpty() )
            return io.finish( false, "project.export-manifest", {},
                              exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                              {}, "pass --out=DIR" );
        const sicnu::workspace::ReproBundleExporter exporter( loaded.context->dataManager(), workspace );
        const sicnu::workspace::ReproBundleReport report = exporter.exportBundle( options );
        Json::Value data = qJsonObjectToJson( report.toJson() );
        return io.finish( report.ok, "project.export-manifest", data,
                          report.ok ? 0 : exprs_ns::exitCodeValue( exprs_ns::ExitCode::GenericError ) );
    }

    if ( sub == QLatin1String( "audit" ) )
    {
        const int limit = argValue( args, "--limit", QStringLiteral( "100" ) ).toInt();
        Json::Value data( Json::objectValue );
        Json::Value entries( Json::arrayValue );
        for ( const auto &entry : workspace.auditTail( limit ) )
        {
            Json::Value e( Json::objectValue );
            e["seq"] = ( Json::Int64 ) entry.seq;
            e["tsMs"] = ( Json::Int64 ) entry.tsMs;
            e["actor"] = entry.actor.toStdString();
            e["action"] = entry.action.toStdString();
            e["entityKind"] = entry.entityKind.toStdString();
            e["entityId"] = entry.entityId.toStdString();
            entries.append( e );
        }
        data["entries"] = entries;
        return io.finish( true, "project.audit", data, 0 );
    }

    return io.finish( false, "project", {}, exprs_ns::exitCodeValue( exprs_ns::ExitCode::InvalidInput ),
                      {}, usage.toStdString() );
}
