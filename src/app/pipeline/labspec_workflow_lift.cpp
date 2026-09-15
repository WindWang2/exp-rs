// src/app/pipeline/labspec_workflow_lift.cpp — LabSpec -> WorkflowDocument lift (D17)
#include "labspec_workflow_lift.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace sicnu::app::pipeline {

using sicnu::workflow::EdgeFact;
using sicnu::workflow::NodeFact;
using sicnu::workflow::PortFact;
using sicnu::workflow::WorkflowDocument;

namespace {
/// jsoncpp -> QJson for LabSpec parameter objects (both trees are ordered;
/// object keys end up sorted in QJsonObject).
QJsonValue jsonToQJson( const Json::Value &value )
{
    if ( value.isObject() )
    {
        QJsonObject object;
        for ( auto it = value.begin(); it != value.end(); ++it )
            object.insert( QString::fromStdString( it.key().asString() ), jsonToQJson( *it ) );
        return object;
    }
    if ( value.isArray() )
    {
        QJsonArray array;
        for ( const Json::Value &entry : value )
            array.append( jsonToQJson( entry ) );
        return array;
    }
    if ( value.isBool() )
        return value.asBool();
    if ( value.isNumeric() )
        return value.asDouble();
    if ( value.isString() )
        return QString::fromStdString( value.asString() );
    return {};
}
} // namespace

namespace {
constexpr const char *kLabStepsKey = "labSteps";
constexpr const char *kStepTitle = "title";
constexpr const char *kStepGuidance = "guidance";
} // namespace

WorkflowDocument liftLabSpecToWorkflow( const lab::LabSpec &spec )
{
    WorkflowDocument def;
    def.workflowId = spec.id;
    def.name = spec.titleZh.isEmpty() ? spec.title : spec.titleZh;
    def.description = spec.objective;
    def.metadata.insert( QStringLiteral( "labSpecVersion" ), 1 );
    def.metadata.insert( QStringLiteral( "labTitleEn" ), spec.title );
    def.metadata.insert( QStringLiteral( "gradingPipeline" ), spec.gradingPipeline );

    QJsonObject labSteps;
    int labIndex = 0;
    QString lastLabNodeId;
    QStringList pendingGuidance;
    for ( const lab::LabStep &step : spec.steps )
    {
        const QString stepTitle = step.titleZh.isEmpty() ? step.title : step.titleZh;
        QString guidance = stepTitle + QStringLiteral( "\n" ) + step.descriptionZh;
        if ( !step.teachingNote.isEmpty() )
            guidance += QStringLiteral( "\n" ) + step.teachingNote;
        if ( !step.completionHint.isEmpty() )
            guidance += QStringLiteral( "\n✓ " ) + step.completionHint;

        if ( step.hasOperator() )
        {
            sicnu::workflow::NodeFact node;
            node.nodeId = QStringLiteral( "lab_step_%1" ).arg( labIndex + 1 );
            node.operatorId = step.operatorId;
            node.displayName = stepTitle;
            node.canvasPosition = QPointF( ( labIndex % 4 ) * 280.0, ( labIndex / 4 ) * 140.0 );
            node.parameters = jsonToQJson( step.params ).toObject();

            sicnu::workflow::PortFact in;
            in.portName = QStringLiteral( "input" );
            in.dataType = QStringLiteral( "Raster" );
            in.isRequired = labIndex > 0;
            node.inputPorts = { in };
            sicnu::workflow::PortFact out;
            out.portName = QStringLiteral( "output" );
            out.dataType = QStringLiteral( "Raster" );
            out.isRequired = false;
            node.outputPorts = { out };

            if ( labIndex > 0 && !lastLabNodeId.isEmpty() )
                def.edges.append( sicnu::workflow::EdgeFact{ QStringLiteral( "lab_e%1" ).arg( labIndex ),
                                                             lastLabNodeId, QStringLiteral( "output" ),
                                                             node.nodeId, QStringLiteral( "input" ) } );

            pendingGuidance.prepend( guidance );
            QJsonObject entry;
            entry.insert( QLatin1String( "is_lab_step" ), true );
            entry.insert( QLatin1String( kStepTitle ), stepTitle );
            QString merged;
            for ( const QString &piece : pendingGuidance )
                merged += merged.isEmpty() ? piece : QStringLiteral( "\n" ) + piece;
            entry.insert( QLatin1String( kStepGuidance ), merged );
            labSteps.insert( node.nodeId, entry );
            pendingGuidance.clear();

            lastLabNodeId = node.nodeId;
            def.nodes.append( node );
            ++labIndex;
        }
        else if ( !lastLabNodeId.isEmpty() )
        {
            QJsonObject entry = labSteps.value( lastLabNodeId ).toObject();
            const QString existing = entry.value( QLatin1String( kStepGuidance ) ).toString();
            entry.insert( QLatin1String( kStepGuidance ),
                          existing.isEmpty() ? guidance : existing + QStringLiteral( "\n" ) + guidance );
            labSteps.insert( lastLabNodeId, entry );
        }
        else
        {
            pendingGuidance.append( guidance );
        }
    }
    def.metadata.insert( QLatin1String( kLabStepsKey ), labSteps );
    return def;
}

} // namespace sicnu::app::pipeline
