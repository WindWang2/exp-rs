// src/workflow/ir2_registry_node_executor.cpp — IR2 NodeExecutor ↔ RSOperatorRegistry (D18)
#include "workflow/ir2_registry_node_executor.h"

#include "workflow/ir2_port_param_mapping.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

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

/// The node id is interpolated into the default artifact file name: reject
/// separators and traversal components so it can never leave the run dir.
bool isSafeNodeId( const QString &nodeId )
{
    if ( nodeId.isEmpty() )
        return false;
    return !nodeId.contains( QLatin1Char( '/' ) ) && !nodeId.contains( QLatin1Char( '\\' ) )
           && !nodeId.contains( QLatin1String( ".." ) );
}

/// Canonical absolute path of the run directory (resolves symlinks, "." and
/// ".."); the confinement root for every artifact this node publishes.
QString canonicalRunDirectory( const QString &runDirectory )
{
    const QString canonical = QFileInfo( runDirectory ).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath( QDir( runDirectory ).absolutePath() ) : canonical;
}

/// Absolute, cleaned form of @p path; relative paths resolve inside @p root
/// (never against the process working directory).
QString resolveArtifactPath( const QString &root, const QString &path )
{
    const QFileInfo info( path );
    if ( info.isAbsolute() )
        return QDir::cleanPath( info.absoluteFilePath() );
    return QDir::cleanPath( QDir( root ).absoluteFilePath( path ) );
}

/// True when @p path names something strictly inside @p root — never the root
/// itself and never a sibling reached through ".." or another drive.
bool isInsideRunRoot( const QString &root, const QString &path )
{
    if ( root.isEmpty() || path.isEmpty() )
        return false;
    const QString rootNorm = QDir::cleanPath( root );
    const QString pathNorm = QDir::cleanPath( path );
    const QString prefix = rootNorm.endsWith( QLatin1Char( '/' ) )
                               ? rootNorm
                               : rootNorm + QLatin1Char( '/' );
    return pathNorm.startsWith( prefix );
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
               const QString &runDirectory,
               const std::atomic<bool> *cancelRequested ) -> NodeExecutionResult {
        // Unbound refusal runs BEFORE port→param mapping — mapping never
        // turns an unbound node into synthetic success.
        const Ir2OperatorBinding binding = classifyIr2OperatorBinding( node.operatorId );
        if ( binding != Ir2OperatorBinding::Bound )
            return makeIr2UnboundRefusal( node, binding );

        // #1032 fail-closed: the node id is interpolated into the default
        // artifact name, so a separator or traversal component would point it
        // outside the run directory.
        if ( !isSafeNodeId( node.nodeId ) )
        {
            NodeExecutionResult refused;
            refused.errorMessage =
                QStringLiteral( "ir2.operator_failed: unsafe nodeId '%1' (no path separators "
                                "or traversal components allowed)" )
                    .arg( node.nodeId );
            return refused;
        }

        auto op = sicnu::operators::RSOperatorRegistry::instance().create(
            node.operatorId.trimmed().toStdString() );
        if ( !op )
            return makeIr2UnboundRefusal( node, Ir2OperatorBinding::UnboundUnknown );

        QDir().mkpath( runDirectory );
        const QString runRoot = canonicalRunDirectory( runDirectory );
        const QString defaultOutput =
            QDir( runRoot ).filePath( QStringLiteral( "%1.out.tif" ).arg( node.nodeId ) );

        Json::Value params = qJsonObjectToJsonCpp( node.parameters );
        applyIr2InputPortMapping( node, inputArtifacts, params );
        if ( !params.isMember( "output" ) || !params["output"].isString()
             || params["output"].asString().empty() )
            params["output"] = defaultOutput.toStdString();
        if ( !params.isMember( "output_path" ) || !params["output_path"].isString()
             || params["output_path"].asString().empty() )
            params["output_path"] = params["output"].asString();

        // Confinement root for every declared output: the published artifact is
        // a handle on run-owned state that checkpoints persist, so neither a
        // node's own parameters nor its operator may point it outside the run
        // directory (#1032).
        QString expectedOutput = defaultOutput;
        QString offendingDeclaration;
        QVector<QString> staleTargets;
        auto confineDeclaredOutput = [&]( const char *key ) -> bool {
            if ( !params.isMember( key ) || !params[key].isString()
                 || params[key].asString().empty() )
                return true;
            const QString declared = QString::fromStdString( params[key].asString() );
            const QString resolved = resolveArtifactPath( runRoot, declared );
            if ( !isInsideRunRoot( runRoot, resolved ) )
            {
                offendingDeclaration = QStringLiteral( "%1 '%2'" )
                                          .arg( QLatin1String( key ), declared );
                return false;
            }
            expectedOutput = resolved;
            staleTargets.push_back( resolved );
            params[key] = resolved.toStdString();
            return true;
        };
        if ( !confineDeclaredOutput( "output" ) || !confineDeclaredOutput( "output_path" ) )
        {
            NodeExecutionResult refused;
            refused.errorMessage =
                QStringLiteral( "ir2.operator_failed: declared output %1 escapes the run "
                                "directory '%2' (node '%3')" )
                    .arg( offendingDeclaration, runRoot, node.nodeId );
            return refused;
        }
        if ( staleTargets.empty() )
            staleTargets.push_back( defaultOutput );

        // Existence is not authorship (#1032): a leftover artifact from an
        // earlier run must never pass for this run's output. Only run-scoped
        // paths are removed, and never a path a mapped input still references.
        auto referencedByInput = [&inputArtifacts, &params]( const QString &path ) {
            const QString normalized = QDir::cleanPath( path );
            auto same = [&normalized]( const std::string &candidate ) {
                return QDir::cleanPath( QString::fromStdString( candidate ) ) == normalized;
            };
            for ( const QString &artifact : inputArtifacts )
                if ( QDir::cleanPath( artifact ) == normalized )
                    return true;
            if ( params.isMember( "input" ) && params["input"].isString()
                 && same( params["input"].asString() ) )
                return true;
            const Json::Value &mapped = params["ir2_input_artifacts"];
            if ( mapped.isObject() )
                for ( const std::string &port : mapped.getMemberNames() )
                    if ( mapped[port].isString() && same( mapped[port].asString() ) )
                        return true;
            return false;
        };
        for ( const QString &stale : staleTargets )
            if ( !referencedByInput( stale ) )
                QFile::remove( stale );

        sicnu::operators::RSOperatorContext context;
        // #1152: wire the run's cooperative cancellation flag into the
        // operator context (the workflow_runtime.cpp pattern) so
        // requestCancel() aborts a long-running registry operator mid-run;
        // without it cancel and dock-close froze the GUI thread for the
        // node's full duration and then discarded the completed artifact.
        if ( cancelRequested )
            context.setCancelFlag( const_cast<std::atomic<bool> *>( cancelRequested ) );
        try
        {
            const Json::Value resultJson = op->execute( params, context );
            NodeExecutionResult result;
            QString artifactPath = extractOutputPath( resultJson, expectedOutput );
            if ( artifactPath.isEmpty() )
                artifactPath = expectedOutput;
            // The published artifact is confined to the run directory: any path
            // outside it is refused, never honored.
            artifactPath = resolveArtifactPath( runRoot, artifactPath );
            if ( !isInsideRunRoot( runRoot, artifactPath ) )
            {
                NodeExecutionResult refused;
                refused.errorMessage =
                    QStringLiteral( "ir2.operator_failed: artifact '%1' escapes the run "
                                    "directory '%2' (node '%3')" )
                        .arg( artifactPath, runRoot, node.nodeId );
                return refused;
            }
            // #1002 fail-closed: operator success must be backed by a regular
            // FILE inside the run directory. A directory, a symlink out of the
            // run tree or a stale leftover is not an artifact — publishing it
            // would let checkpoints and downstream nodes consume a phantom.
            const QFileInfo artifactInfo( artifactPath );
            const QString canonicalArtifact = artifactInfo.canonicalFilePath();
            if ( !artifactInfo.isFile()
                 || ( !canonicalArtifact.isEmpty()
                      && !isInsideRunRoot( runRoot, canonicalArtifact ) ) )
            {
                NodeExecutionResult missing;
                missing.errorMessage =
                    QStringLiteral( "ir2.operator_failed: missing artifact '%1' (%2) node '%3'" )
                        .arg( artifactPath, node.operatorId, node.nodeId );
                return missing;
            }
            result.artifactPath = artifactPath;
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
