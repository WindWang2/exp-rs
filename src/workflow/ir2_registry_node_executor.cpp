// src/workflow/ir2_registry_node_executor.cpp — IR2 NodeExecutor ↔ RSOperatorRegistry (D18)
#include "workflow/ir2_registry_node_executor.h"

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_operator_registry.h"

#include <QDir>
#include <QFile>
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

QString pickPrimaryInput( const QHash<QString, QString> &inputArtifacts )
{
    if ( inputArtifacts.isEmpty() )
        return {};
    QStringList keys = inputArtifacts.keys();
    std::sort( keys.begin(), keys.end() );
    return inputArtifacts.value( keys.front() );
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

NodeExecutionResult refuseUnbound( const NodeFact &node, Ir2OperatorBinding kind )
{
    NodeExecutionResult result;
    if ( kind == Ir2OperatorBinding::UnboundEmpty )
    {
        result.errorMessage =
            QStringLiteral( "%1 empty operatorId on node '%2'" )
                .arg( QLatin1String( kIr2OperatorUnboundPrefix ), node.nodeId );
    }
    else
    {
        result.errorMessage =
            QStringLiteral( "%1 no registry binding for '%2' (node '%3')" )
                .arg( QLatin1String( kIr2OperatorUnboundPrefix ), node.operatorId, node.nodeId );
    }
    return result;
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

NodeExecutor makeSyntheticNodeExecutor()
{
    return []( const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
               const QString &runDirectory ) -> NodeExecutionResult {
        NodeExecutionResult result;
        QDir().mkpath( runDirectory );
        const QString artifact =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.artifact" ).arg( node.nodeId ) );
        QFile file( artifact );
        if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        {
            result.errorMessage = QStringLiteral( "cannot write artifact for '%1'" ).arg( node.nodeId );
            return result;
        }
        QByteArray payload;
        payload += QStringLiteral( "d17-node %1 op %2\n" ).arg( node.nodeId, node.operatorId ).toUtf8();
        QStringList parents = inputArtifacts.keys();
        std::sort( parents.begin(), parents.end() );
        for ( const QString &parent : parents )
            payload += QStringLiteral( "in %1\n" ).arg( inputArtifacts.value( parent ) ).toUtf8();
        file.write( payload );
        file.close();
        result.success = true;
        result.artifactPath = artifact;
        return result;
    };
}

NodeExecutor makeRegistryNodeExecutor()
{
    return []( const NodeFact &node, const QHash<QString, QString> &inputArtifacts,
               const QString &runDirectory ) -> NodeExecutionResult {
        const Ir2OperatorBinding binding = classifyIr2OperatorBinding( node.operatorId );
        if ( binding != Ir2OperatorBinding::Bound )
            return refuseUnbound( node, binding );

        auto op = sicnu::operators::RSOperatorRegistry::instance().create(
            node.operatorId.trimmed().toStdString() );
        if ( !op )
            return refuseUnbound( node, Ir2OperatorBinding::UnboundUnknown );

        QDir().mkpath( runDirectory );
        const QString defaultOutput =
            QDir( runDirectory ).filePath( QStringLiteral( "%1.out.tif" ).arg( node.nodeId ) );

        Json::Value params = qJsonObjectToJsonCpp( node.parameters );
        if ( !params.isMember( "input" ) || !params["input"].isString()
             || params["input"].asString().empty() )
        {
            const QString primary = pickPrimaryInput( inputArtifacts );
            if ( !primary.isEmpty() )
                params["input"] = primary.toStdString();
        }
        // Preserve parent artifact map for multi-input operators (deterministic keys).
        if ( !inputArtifacts.isEmpty() )
        {
            Json::Value inputs( Json::objectValue );
            QStringList parents = inputArtifacts.keys();
            std::sort( parents.begin(), parents.end() );
            for ( const QString &parent : parents )
                inputs[parent.toStdString()] = inputArtifacts.value( parent ).toStdString();
            params["ir2_input_artifacts"] = inputs;
        }
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
            result.success = true;
            result.artifactPath = extractOutputPath( resultJson, defaultOutput );
            // If the operator reported success but produced no file, still record the
            // declared output path so checkpoints stay path-shaped (fail-open on
            // existence — operator already returned).
            if ( result.artifactPath.isEmpty() )
                result.artifactPath = defaultOutput;
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
