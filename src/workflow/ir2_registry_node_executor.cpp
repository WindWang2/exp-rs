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

        QDir().mkpath( runDirectory );
        const QString defaultOutput =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.out.tif" ).arg( node.nodeId ) );

        Json::Value params = qJsonObjectToJsonCpp( node.parameters );
        applyIr2InputPortMapping( node, inputArtifacts, params );
        if ( !params.isMember( "output" ) || !params["output"].isString()
             || params["output"].asString().empty() )
            params["output"] = defaultOutput.toStdString();
        if ( !params.isMember( "output_path" ) || !params["output_path"].isString()
             || params["output_path"].asString().empty() )
            params["output_path"] = params["output"].asString();

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
            if ( !QFileInfo::exists( result.artifactPath ) )
            {
                NodeExecutionResult missing;
                missing.errorMessage =
                    QStringLiteral( "ir2.operator_failed: missing artifact '%1' (%2) node '%3'" )
                        .arg( result.artifactPath, node.operatorId, node.nodeId );
                return missing;
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
