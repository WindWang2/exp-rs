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

bool ir2NodeIdIsSafe( const QString &nodeId )
{
    // Default artifact names interpolate nodeId; separators / ".." would
    // write outside the run directory.
    if ( nodeId.contains( QLatin1Char( '/' ) ) || nodeId.contains( QLatin1Char( '\\' ) ) )
        return false;
    if ( nodeId.contains( QLatin1String( ".." ) ) )
        return false;
    return true;
}

QString ir2NormalizedPath( const QString &path )
{
    const QFileInfo info( path );
    QString resolved = info.canonicalFilePath();
    if ( resolved.isEmpty() )
        resolved = info.absoluteFilePath();
    return QDir::cleanPath( QDir::fromNativeSeparators( resolved ) );
}

bool ir2PathIsInsideRunDirectory( const QString &path, const QString &runDirectory )
{
    QString root = ir2NormalizedPath( runDirectory );
    const QString file = ir2NormalizedPath( path );
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if ( file.compare( root, cs ) == 0 )
        return false;
    if ( !root.endsWith( QLatin1Char( '/' ) ) )
        root += QLatin1Char( '/' );
    return file.startsWith( root, cs );
}

void ir2ConfineOutputParam( Json::Value &params, const char *key, const QString &fallback,
                            const QString &runDirectory )
{
    if ( !params.isMember( key ) || !params[key].isString() )
        return;
    const std::string raw = params[key].asString();
    if ( raw.empty() )
        return;
    if ( !ir2PathIsInsideRunDirectory( QString::fromStdString( raw ), runDirectory ) )
        params[key] = fallback.toStdString();
}

NodeExecutionResult ir2OperatorFailed( const NodeFact &node, const QString &detail )
{
    NodeExecutionResult failed;
    failed.errorMessage =
        QStringLiteral( "ir2.operator_failed: %1 (%2) node '%3'" )
            .arg( detail, node.operatorId, node.nodeId );
    return failed;
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

        auto op = sicnu::operators::RSOperatorRegistry::instance().create(
            node.operatorId.trimmed().toStdString() );
        if ( !op )
            return makeIr2UnboundRefusal( node, Ir2OperatorBinding::UnboundUnknown );

        if ( !ir2NodeIdIsSafe( node.nodeId ) )
            return ir2OperatorFailed( node, QStringLiteral( "unsafe nodeId '%1'" ).arg( node.nodeId ) );

        QDir().mkpath( runDirectory );
        const QString defaultOutput =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.out.tif" ).arg( node.nodeId ) );

        Json::Value params = qJsonObjectToJsonCpp( node.parameters );
        applyIr2InputPortMapping( node, inputArtifacts, params );
        if ( !params.isMember( "output" ) || !params["output"].isString()
             || params["output"].asString().empty() )
            params["output"] = defaultOutput.toStdString();
        ir2ConfineOutputParam( params, "output", defaultOutput, runDirectory );
        if ( !params.isMember( "output_path" ) || !params["output_path"].isString()
             || params["output_path"].asString().empty() )
            params["output_path"] = params["output"].asString();
        ir2ConfineOutputParam( params, "output_path", defaultOutput, runDirectory );

        sicnu::operators::RSOperatorContext context;
        try
        {
            const Json::Value resultJson = op->execute( params, context );
            NodeExecutionResult result;
            result.artifactPath = extractOutputPath( resultJson, defaultOutput );
            if ( result.artifactPath.isEmpty() )
                result.artifactPath = defaultOutput;
            // #1002 / F-1032-P1-artifact: existence is not authorship. The
            // path must be a regular file inside the canonical run directory;
            // operator JSON may not publish a leftover, a directory, or a
            // path outside that root.
            const QFileInfo artifactInfo( result.artifactPath );
            if ( !artifactInfo.isFile() )
            {
                if ( !artifactInfo.exists() )
                {
                    return ir2OperatorFailed(
                        node, QStringLiteral( "missing artifact '%1'" ).arg( result.artifactPath ) );
                }
                return ir2OperatorFailed(
                    node, QStringLiteral( "artifact is not a file '%1'" ).arg( result.artifactPath ) );
            }
            if ( !ir2PathIsInsideRunDirectory( result.artifactPath, runDirectory ) )
            {
                return ir2OperatorFailed(
                    node,
                    QStringLiteral( "artifact outside run directory '%1'" ).arg( result.artifactPath ) );
            }
            const QString canonical = artifactInfo.canonicalFilePath();
            result.artifactPath = canonical.isEmpty() ? artifactInfo.absoluteFilePath() : canonical;
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
