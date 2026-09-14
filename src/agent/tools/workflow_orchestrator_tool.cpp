// src/agent/tools/workflow_orchestrator_tool.cpp — production rules + heal loop (D17)
#include "workflow_orchestrator_tool.h"

#include "workflow/contract_checker.h"
#include "workflow/workflow_repair_engine.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace sicnu::agent::tools {

using sicnu::workflow::EdgeFact;
using sicnu::workflow::NodeFact;
using sicnu::workflow::PortFact;
using sicnu::workflow::WorkflowDefinition;

namespace {

PortFact inPort( const QString &crs, const QString &state, double res, int bands, bool required = true )
{
    return PortFact{ QStringLiteral( "input" ), QStringLiteral( "Raster" ), crs, state, res, res, bands, required };
}

PortFact outPort( const QString &crs, const QString &state, double res, int bands )
{
    return PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), crs, state, res, res, bands, false };
}

PortFact maskOut( const QString &crs, double res )
{
    return PortFact{ QStringLiteral( "output" ), QStringLiteral( "Mask" ), crs, QStringLiteral( "Mask" ), res, res, 1, false };
}

struct NodeSpec
{
    QString id;
    QString op;
    QString name;
    QJsonObject params;
    PortFact in;
    PortFact out;
    bool hasIn = true;
};

WorkflowDefinition chain( const QString &workflowId, const QString &goal, const QString &sensor,
                          const QVector<NodeSpec> &specs, const QJsonObject &extraMeta = {} )
{
    WorkflowDefinition def;
    def.workflowId = workflowId;
    def.name = goal;
    def.description = QStringLiteral( "Compiled by WorkflowOrchestratorTool (sensor %1)" ).arg( sensor );
    def.metadata.insert( QStringLiteral( "compiledFrom" ), QStringLiteral( "nl_goal" ) );
    def.metadata.insert( QStringLiteral( "sensor" ), sensor );
    for ( auto it = extraMeta.begin(); it != extraMeta.end(); ++it )
        def.metadata.insert( it.key(), it.value() );

    int index = 0;
    QString previousId;
    for ( const NodeSpec &spec : specs )
    {
        NodeFact node;
        node.nodeId = spec.id;
        node.operatorId = spec.op;
        node.displayName = spec.name;
        node.parameters = spec.params;
        node.canvasPosition = QPointF( index * 260.0, 0.0 );
        node.inputPorts = spec.hasIn ? QVector<PortFact>{ spec.in } : QVector<PortFact>{};
        node.outputPorts = QVector<PortFact>{ spec.out };
        def.nodes.append( node );
        if ( index > 0 && !previousId.isEmpty() )
            def.edges.append( EdgeFact{ QStringLiteral( "orch_e%1" ).arg( index ), previousId,
                                        QStringLiteral( "output" ), spec.id, QStringLiteral( "input" ) } );
        previousId = spec.id;
        ++index;
    }
    return def;
}

// Sensor baselines: band counts / resolutions per supported family.
void sensorProfile( const QString &sensor, int *bands, double *res, QString *crs )
{
    if ( sensor.startsWith( QLatin1String( "Landsat" ) ) )
    {
        *bands = 7;
        *res = 30.0;
        *crs = QStringLiteral( "EPSG:32649" );
    }
    else if ( sensor.startsWith( QLatin1String( "Sentinel" ) ) )
    {
        *bands = 13;
        *res = 10.0;
        *crs = QStringLiteral( "EPSG:32649" );
    }
    else // GF-1 and the default family
    {
        *bands = 4;
        *res = 10.0;
        *crs = QStringLiteral( "EPSG:32649" );
    }
}

bool goalMentions( const QString &goal, std::initializer_list<const char *> keywords )
{
    for ( const char *keyword : keywords )
        if ( goal.contains( QLatin1String( keyword ), Qt::CaseInsensitive ) )
            return true;
    return false;
}

} // namespace

QJsonObject WorkflowOrchestratorTool::getToolJsonSchema()
{
    // Draft-07 tool schema — the agent catalog entry.
    return QJsonObject{
        { "$schema", QStringLiteral( "http://json-schema.org/draft-07/schema#" ) },
        { "title", QStringLiteral( "workflow_orchestrator" ) },
        { "description", QStringLiteral( "Compile a natural-language remote-sensing goal into a "
                                         "workflow pipeline; heal broken workflows from execution logs." ) },
        { "type", QStringLiteral( "object" ) },
        { "properties",
          QJsonObject{
              { "goal", QJsonObject{ { "type", QStringLiteral( "string" ) },
                                     { "minLength", 1 },
                                     { "description", QStringLiteral( "Natural language processing goal" ) } } },
              { "sensor", QJsonObject{ { "type", QStringLiteral( "string" ) },
                                       { "enum", QJsonArray{ QStringLiteral( "GF-1" ), QStringLiteral( "Landsat8" ),
                                                             QStringLiteral( "Sentinel2" ) } } } },
              { "region", QJsonObject{ { "type", QStringLiteral( "string" ) },
                                       { "pattern", QStringLiteral( "^bbox:[-0-9.,]+$" ) } } },
              { "inputs", QJsonObject{ { "type", QStringLiteral( "array" ) },
                                       { "items", QJsonObject{ { "type", QStringLiteral( "string" ) } } } } } } },
        { "required", QJsonArray{ QStringLiteral( "goal" ) } },
        { "additionalProperties", false } };
}

AutonomousCompileResult WorkflowOrchestratorTool::compileGoalToWorkflow( const AutonomousCompileRequest &request )
{
    AutonomousCompileResult result;
    if ( request.userNaturalLanguageGoal.trimmed().isEmpty() )
    {
        result.textualExplanation = QStringLiteral( "empty goal" );
        return result;
    }

    int bands = 4;
    double res = 10.0;
    QString crs = QStringLiteral( "EPSG:32649" );
    sensorProfile( request.sensorType, &bands, &res, &crs );

    const QString &goal = request.userNaturalLanguageGoal;
    result.isSuccess = true;

    if ( goalMentions( goal, { "water", "水体", "water extraction", "extract water" } )
         && goalMentions( goal, { "ndwi", "index", "water index" } ) )
    {
        // Water extraction: import -> calibrate -> atmosphere -> NDWI -> threshold.
        result.workflow = chain(
            QStringLiteral( "wf_orch_water" ), goal, request.sensorType,
            { { QStringLiteral( "node_import" ), QStringLiteral( "rs:import_raster" ), QStringLiteral( "Import" ),
                QJsonObject{ { "sensor", request.sensorType } }, {}, outPort( crs, QStringLiteral( "DN" ), res, bands ), false },
              { QStringLiteral( "node_calib" ), QStringLiteral( "rs:radiometric_calibration" ), QStringLiteral( "Calibrate" ),
                QJsonObject{ { "method", QStringLiteral( "linear" ) } }, inPort( crs, QStringLiteral( "DN" ), res, bands ),
                outPort( crs, QStringLiteral( "Radiance" ), res, bands ) },
              { QStringLiteral( "node_atm" ), QStringLiteral( "rs:atmospheric_correction" ), QStringLiteral( "Atmosphere" ),
                QJsonObject{ { "method", QStringLiteral( "dos1" ) } }, inPort( crs, QStringLiteral( "Radiance" ), res, bands ),
                outPort( crs, QStringLiteral( "BOA" ), res, bands ) },
              { QStringLiteral( "node_ndwi" ), QStringLiteral( "rs:spectral_index" ), QStringLiteral( "NDWI" ),
                QJsonObject{ { "index", QStringLiteral( "NDWI" ) } }, inPort( crs, QStringLiteral( "BOA" ), res, bands ),
                outPort( crs, QStringLiteral( "Index" ), res, 1 ) },
              { QStringLiteral( "node_threshold" ), QStringLiteral( "rs:threshold" ), QStringLiteral( "Water threshold" ),
                QJsonObject{ { "threshold", 0.0 } }, inPort( crs, QStringLiteral( "Index" ), res, 1 ),
                maskOut( crs, res ) } } );
        result.textualExplanation = QStringLiteral( "water extraction chain: import -> calibration -> "
                                                    "atmosphere -> NDWI -> threshold" );
        return result;
    }

    if ( goalMentions( goal, { "ndvi", "植被指数", "spectral index", "index" } ) )
    {
        // Calibrate + index: import -> calibration -> atmosphere -> NDVI.
        result.workflow = chain(
            QStringLiteral( "wf_orch_ndvi" ), goal, request.sensorType,
            { { QStringLiteral( "node_import" ), QStringLiteral( "rs:import_raster" ), QStringLiteral( "Import" ),
                QJsonObject{ { "sensor", request.sensorType } }, {}, outPort( crs, QStringLiteral( "DN" ), res, bands ), false },
              { QStringLiteral( "node_calib" ), QStringLiteral( "rs:radiometric_calibration" ), QStringLiteral( "Calibrate" ),
                QJsonObject{ { "method", QStringLiteral( "linear" ) } }, inPort( crs, QStringLiteral( "DN" ), res, bands ),
                outPort( crs, QStringLiteral( "Radiance" ), res, bands ) },
              { QStringLiteral( "node_atm" ), QStringLiteral( "rs:atmospheric_correction" ), QStringLiteral( "Atmosphere" ),
                QJsonObject{ { "method", QStringLiteral( "dos1" ) } }, inPort( crs, QStringLiteral( "Radiance" ), res, bands ),
                outPort( crs, QStringLiteral( "BOA" ), res, bands ) },
              { QStringLiteral( "node_ndvi" ), QStringLiteral( "rs:spectral_index" ), QStringLiteral( "NDVI" ),
                QJsonObject{ { "index", QStringLiteral( "NDVI" ) } }, inPort( crs, QStringLiteral( "BOA" ), res, bands ),
                outPort( crs, QStringLiteral( "Index" ), res, 1 ) } } );
        result.textualExplanation = QStringLiteral( "radiometric chain: import -> calibration -> atmosphere -> NDVI" );
        return result;
    }

    if ( goalMentions( goal, { "change detection", "变化检测", "bi-temporal", "bitemporal" } ) )
    {
        result.workflow = chain(
            QStringLiteral( "wf_orch_change" ), goal, request.sensorType,
            { { QStringLiteral( "node_import" ), QStringLiteral( "rs:import_raster" ), QStringLiteral( "Import pair" ),
                QJsonObject{ { "sensor", request.sensorType }, { "pair", true } }, {},
                outPort( crs, QStringLiteral( "BOA" ), res, bands ), false },
              { QStringLiteral( "node_cva" ), QStringLiteral( "rs:change_vector" ), QStringLiteral( "CVA" ),
                QJsonObject{}, inPort( crs, QStringLiteral( "BOA" ), res, bands ),
                outPort( crs, QStringLiteral( "Index" ), res, 1 ) },
              { QStringLiteral( "node_threshold" ), QStringLiteral( "rs:threshold" ), QStringLiteral( "Change mask" ),
                QJsonObject{ { "threshold", 0.15 } }, inPort( crs, QStringLiteral( "Index" ), res, 1 ),
                maskOut( crs, res ) } } );
        result.textualExplanation = QStringLiteral( "change detection chain: import -> CVA -> threshold" );
        return result;
    }

    if ( goalMentions( goal, { "fusion", "融合", "pansharpen", "pan-sharpen" } ) )
    {
        result.workflow = chain(
            QStringLiteral( "wf_orch_fusion" ), goal, request.sensorType,
            { { QStringLiteral( "node_import" ), QStringLiteral( "rs:import_raster" ), QStringLiteral( "Import pan+ms" ),
                QJsonObject{ { "sensor", request.sensorType }, { "pair", QStringLiteral( "pan_ms" ) } }, {},
                outPort( crs, QStringLiteral( "DN" ), res, bands ), false },
              { QStringLiteral( "node_calib" ), QStringLiteral( "rs:radiometric_calibration" ), QStringLiteral( "Calibrate" ),
                QJsonObject{ { "method", QStringLiteral( "linear" ) } }, inPort( crs, QStringLiteral( "DN" ), res, bands ),
                outPort( crs, QStringLiteral( "Radiance" ), res, bands ) },
              { QStringLiteral( "node_fusion" ), QStringLiteral( "rs:gs_fusion" ), QStringLiteral( "Gram-Schmidt" ),
                QJsonObject{}, inPort( crs, QStringLiteral( "Radiance" ), res, bands ),
                outPort( crs, QStringLiteral( "Radiance" ), res / 2.0, bands ) } } );
        result.textualExplanation = QStringLiteral( "fusion chain: import -> calibration -> GS fusion" );
        return result;
    }

    if ( goalMentions( goal, { "classif", "分类", "land cover", "supervised" } ) )
    {
        result.workflow = chain(
            QStringLiteral( "wf_orch_classify" ), goal, request.sensorType,
            { { QStringLiteral( "node_import" ), QStringLiteral( "rs:import_raster" ), QStringLiteral( "Import" ),
                QJsonObject{ { "sensor", request.sensorType } }, {}, outPort( crs, QStringLiteral( "BOA" ), res, bands ), false },
              { QStringLiteral( "node_features" ), QStringLiteral( "rs:spatial_filter" ), QStringLiteral( "Feature smoothing" ),
                QJsonObject{ { "kernel", QStringLiteral( "gaussian" ) } }, inPort( crs, QStringLiteral( "BOA" ), res, bands ),
                outPort( crs, QStringLiteral( "BOA" ), res, bands ) },
              { QStringLiteral( "node_rf" ), QStringLiteral( "rs:random_forest_classify" ), QStringLiteral( "Random forest" ),
                QJsonObject{ { "trees", 100 } }, inPort( crs, QStringLiteral( "BOA" ), res, bands ),
                PortFact{ QStringLiteral( "output" ), QStringLiteral( "Raster" ), crs, QStringLiteral( "Categorical" ), res, res, 1, false } } } );
        result.textualExplanation = QStringLiteral( "classification chain: import -> smoothing -> random forest" );
        return result;
    }

    result.isSuccess = false;
    result.textualExplanation = QStringLiteral( "no production rule matches goal" );
    return result;
}

AutonomousCompileResult WorkflowOrchestratorTool::healWorkflow( const WorkflowDefinition &brokenWorkflow,
                                                                const QString &executionErrorLog )
{
    AutonomousCompileResult result;
    result.workflow = brokenWorkflow;

    // Pattern: PROJ/GDAL CRS mismatch text. Extracts the TARGET CRS the
    // engine demanded and injects the repair engine's reproject rule.
    static const QRegularExpression crsPattern(
        QStringLiteral( "(?:Different spatial reference system|CRS mismatch)[^\\n]*?"
                        "(EPSG:\\d{4,5})\\s*(?:and|vs|,|->)\\s*(EPSG:\\d{4,5})" ),
        QRegularExpression::CaseInsensitiveOption );
    auto match = crsPattern.match( executionErrorLog );
    if ( match.hasMatch() )
    {
        const QString logSource = match.captured( 1 );
        const QString logTarget = match.captured( 2 );

        // Align the log with the document: find the violating edges via the
        // contract checker; heal them toward the LOG's target CRS by
        // rewriting the consumer requirement when it matches the log source.
        WorkflowDefinition working = brokenWorkflow;
        auto violations = sicnu::workflow::inspectContracts( working );
        if ( violations.isEmpty() )
        {
            // The document's port facts did not encode the break the engine
            // hit at runtime: adopt the log's facts on the mismatched edge.
            bool adopted = false;
            for ( sicnu::workflow::NodeFact &node : working.nodes )
                for ( PortFact &port : node.inputPorts )
                    if ( port.crs == logSource )
                    {
                        port.crs = logTarget;
                        adopted = true;
                    }
            if ( adopted )
                result.injectedRepairRules.append( QStringLiteral( "rule_crs_log_adopt" ) );
        }

        const auto plan = sicnu::workflow::WorkflowRepairEngine::inferRepairs( working );
        if ( plan.requiresRepair )
        {
            for ( const auto &action : plan.suggestedActions )
                if ( !result.injectedRepairRules.contains( action.ruleId ) )
                    result.injectedRepairRules.append( action.ruleId );
            result.workflow = sicnu::workflow::WorkflowRepairEngine::applyRepairPlan( working, plan );
        }
        else
        {
            result.workflow = working;
        }

        result.isSuccess = sicnu::workflow::inspectContracts( result.workflow ).isEmpty();
        result.textualExplanation = QStringLiteral( "healed CRS mismatch %1 -> %2 (%3 rule(s) injected)" )
                                        .arg( logSource, logTarget )
                                        .arg( result.injectedRepairRules.size() );
        return result;
    }

    result.textualExplanation = QStringLiteral( "no known error pattern in log" );
    return result;
}

} // namespace sicnu::agent::tools
