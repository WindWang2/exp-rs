// src/workflow/ir2_registry_node_executor.cpp — IR2 NodeExecutor ↔ RSOperatorRegistry (D18)
#include "workflow/ir2_registry_node_executor.h"

#include "workflow/ir2_port_param_mapping.h"
#include "workflow/path_containment.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <json/json.h>

#include <algorithm>
#include <memory>

namespace sicnu::workflow {
namespace {

Json::Value qJsonObjectToJsonCpp( const QJsonObject &object )
{
    Json::Value parsed;
    Json::CharReaderBuilder builder;
    std::string errs;
    const QByteArray bytes = QJsonDocument( object ).toJson( QJsonDocument::Compact );
    const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
    if ( !reader->parse( bytes.constData(), bytes.constData() + bytes.size(), &parsed, &errs ) )
        return Json::Value( Json::objectValue );
    if ( !parsed.isObject() )
        return Json::Value( Json::objectValue );
    return parsed;
}

QString extractOutputPath( const Json::Value &result, const QString &fallbackPath )
{
    if ( result.isObject() )
    {
        if ( result.isMember( "output" ) && result["output"].isString()
             && !result["output"].asString().empty() )
            return QString::fromStdString( result["output"].asString() );
        if ( result.isMember( "outputPath" ) && result["outputPath"].isString()
             && !result["outputPath"].asString().empty() )
            return QString::fromStdString( result["outputPath"].asString() );
        if ( result.isMember( "result" ) && result["result"].isString()
             && !result["result"].asString().empty() )
            return QString::fromStdString( result["result"].asString() );
    }
    return fallbackPath;
}

// F-1032-P1-artifact: node ids are interpolated into artifact filenames. An
// id carrying separators, ':' (NTFS alternate data streams) or '..' would
// let a document place (or claim) artifacts outside the run directory.
bool isSafeNodeId( const QString &nodeId )
{
    if ( nodeId.isEmpty() )
        return false;
    if ( nodeId.contains( QLatin1Char( '/' ) ) || nodeId.contains( QLatin1Char( '\\' ) )
         || nodeId.contains( QLatin1Char( ':' ) ) )
        return false;
    if ( nodeId.contains( QLatin1String( ".." ) ) )
        return false;
    return true;
}

// Authorship stamp (F-1032-P1-artifact): existence + size + mtime captured
// immediately before the operator runs. A post-run artifact whose stamp is
// IDENTICAL to its pre-run stamp was not written by this execution — the
// operator pointed at a pre-existing (stale) file. ns/100ns mtime sources
// (ext4/NTFS) make a real rewrite indistinguishable only when it produces
// byte-identical output within one timestamp tick, in which case refusing is
// the safe direction.
struct FileStamp
{
    bool exists = false;
    qint64 size = 0;
    qint64 mtimeMs = 0;
};

FileStamp stampFile( const QString &path )
{
    const QFileInfo info( path );
    FileStamp stamp;
    stamp.exists = info.exists();
    if ( stamp.exists )
    {
        stamp.size = info.size();
        stamp.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    }
    return stamp;
}

bool isSameStamp( const FileStamp &before, const QString &path )
{
    const FileStamp after = stampFile( path );
    return after.exists && before.exists && after.size == before.size
           && after.mtimeMs == before.mtimeMs;
}

} // namespace

Ir2OperatorBinding classifyIr2OperatorBinding( const QString &operatorId )
{
    const QString trimmed = operatorId.trimmed();
    if ( trimmed.isEmpty() )
        return Ir2OperatorBinding::UnboundEmpty;
    if ( sicnu::operators::RSOperatorRegistry::instance().hasOperator( trimmed.toStdString() ) )
        return Ir2OperatorBinding::Bound;
    return Ir2OperatorBinding::UnboundUnknown;
}

bool isIr2OperatorBound( const QString &operatorId )
{
    return classifyIr2OperatorBinding( operatorId ) == Ir2OperatorBinding::Bound;
}

NodeExecutor makeRegistryNodeExecutor()
{
    return []( const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
               const QString &runDirectory ) -> NodeExecutionResult {
        // Unbound refusal runs BEFORE port→param mapping — mapping never
        // turns an unbound node into synthetic success.
        const Ir2OperatorBinding binding = classifyIr2OperatorBinding( node.operatorId );
        if ( binding != Ir2OperatorBinding::Bound )
            return makeIr2UnboundRefusal( node, binding );

        // Path-traversal refusal BEFORE any path is derived from the id.
        if ( !isSafeNodeId( node.nodeId ) )
        {
            NodeExecutionResult refusal;
            refusal.errorMessage =
                QStringLiteral( "ir2.unsafe_node_id: node id '%1' contains path separators or '..' (%2)" )
                    .arg( node.nodeId, node.operatorId );
            return refusal;
        }

        auto op = sicnu::operators::RSOperatorRegistry::instance().create(
            node.operatorId.trimmed().toStdString() );
        if ( !op )
            return makeIr2UnboundRefusal( node, Ir2OperatorBinding::UnboundUnknown );

        QDir().mkpath( runDirectory );
        const QDir runDir( runDirectory );
        const QString defaultOutput =
            runDir.filePath( QStringLiteral( "%1.out.tif" ).arg( node.nodeId ) );

        Json::Value params = qJsonObjectToJsonCpp( node.parameters );
        applyIr2InputPortMapping( node, inputArtifacts, params );
        if ( !params.isMember( "output" ) || !params["output"].isString()
             || params["output"].asString().empty() )
            params["output"] = defaultOutput.toStdString();
        if ( !params.isMember( "output_path" ) || !params["output_path"].isString()
             || params["output_path"].asString().empty() )
            params["output_path"] = params["output"].asString();

        // Custom-output contract: an operator may pick its own artifact name
        // via node parameters, but EVERY declared output path ("output" and
        // "output_path" alike) must live inside THIS run's directory. Refuse
        // before execution — the operator is never pointed at a path we
        // would refuse to publish.
        const auto declaredRefusal =
            [&]( const QString &declared ) -> std::unique_ptr<NodeExecutionResult> {
            if ( path_containment::lexicallyInsideDirectory( declared, runDir ) )
                return nullptr;
            auto refusal = std::make_unique<NodeExecutionResult>();
            refusal->errorMessage =
                QStringLiteral( "ir2.artifact_outside_run: declared output '%1' is outside run directory '%2' (node '%3', operator '%4')" )
                    .arg( declared, runDirectory, node.nodeId, node.operatorId );
            return refusal;
        };
        const QString declaredOutput =
            path_containment::absolutePathFor( QString::fromStdString( params["output"].asString() ), runDir );
        if ( auto refusal = declaredRefusal( declaredOutput ) )
            return std::move( *refusal );
        const QString declaredOutputPath =
            path_containment::absolutePathFor( QString::fromStdString( params["output_path"].asString() ), runDir );
        if ( auto refusal = declaredRefusal( declaredOutputPath ) )
            return std::move( *refusal );

        // Authorship baseline: what the declared output looked like BEFORE
        // the operator ran. Coarse-mtime filesystems (FAT/exFAT, 2 s
        // granularity) can make a genuinely rewritten file look unauthored —
        // refusing then is the safe direction.
        // Accepted residual: a result JSON naming a DIFFERENT file than the
        // declared output is trusted on containment + an in-execution-window
        // mtime alone; a hostile operator could name a sibling node's
        // in-flight artifact (same run directory). The declared-output stamp
        // ladder — the path every stock operator echoes — has no such hole.
        const FileStamp stampBefore = stampFile( declaredOutput );
        const qint64 startedAtMs = QDateTime::currentMSecsSinceEpoch();

        sicnu::operators::RSOperatorContext context;
        try
        {
            const Json::Value resultJson = op->execute( params, context );
            NodeExecutionResult result;
            result.artifactPath = extractOutputPath( resultJson, defaultOutput );
            if ( result.artifactPath.isEmpty() )
                result.artifactPath = defaultOutput;

            // #1002 fail-closed: operator success must be backed by an
            // artifact on disk. Publishing a path that does not exist would
            // let checkpoints and downstream nodes consume a phantom file.
            if ( !QFileInfo( result.artifactPath ).exists() )
            {
                NodeExecutionResult missing;
                missing.errorMessage =
                    QStringLiteral( "ir2.operator_failed: missing artifact '%1' (%2) node '%3'" )
                        .arg( result.artifactPath, node.operatorId, node.nodeId );
                return missing;
            }

            // F-1032-P1-artifact: existence is not authorship.
            //
            // 1. The published artifact must resolve inside the run
            //    directory (symlink escapes included).
            if ( !path_containment::resolvedInsideDirectory( result.artifactPath, runDir ) )
            {
                NodeExecutionResult outside;
                outside.errorMessage =
                    QStringLiteral( "ir2.artifact_outside_run: artifact '%1' is outside run directory '%2' (node '%3', operator '%4')" )
                        .arg( result.artifactPath, runDirectory, node.nodeId, node.operatorId );
                return outside;
            }

            // 2. The published artifact must have been authored by THIS
            //    execution. When it is the declared output we compare the
            //    pre-run stamp; a differently-named artifact must at least
            //    carry a modification time from within the execution window.
            bool authored = false;
            if ( path_containment::absolutePathFor( result.artifactPath, runDir ) == declaredOutput )
            {
                authored = !isSameStamp( stampBefore, result.artifactPath );
            }
            else
            {
                authored = QFileInfo( result.artifactPath ).lastModified().toMSecsSinceEpoch()
                           >= startedAtMs;
            }
            if ( !authored )
            {
                NodeExecutionResult stale;
                stale.errorMessage =
                    QStringLiteral( "ir2.artifact_stale: '%1' pre-existed this execution and was not rewritten (node '%3', operator '%2')" )
                        .arg( result.artifactPath, node.operatorId, node.nodeId );
                return stale;
            }

            result.success = true;
            return result;
        }
        catch ( const sicnu::operators::RSOperatorError &e )
        {
            NodeExecutionResult result;
            result.errorMessage =
                QStringLiteral( "ir2.operator_failed: %1 (%2) node '%3'" )
                    .arg( QString::fromStdString( e.message() ), node.operatorId, node.nodeId );
            return result;
        }
        catch ( const std::exception &e )
        {
            NodeExecutionResult result;
            result.errorMessage =
                QStringLiteral( "ir2.operator_failed: %1 (%2) node '%3'" )
                    .arg( QString::fromUtf8( e.what() ), node.operatorId, node.nodeId );
            return result;
        }
    };
}

} // namespace sicnu::workflow
