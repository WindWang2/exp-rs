#include "explain/adapters/workflow_projection.h"

#include "workflow/workflow_ir_v2.h"
#include "workflow/workflow_types.h"
#include "explain/state_vocabulary.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <memory>

namespace sicnu::explain::adapters
{
namespace
{

std::string toStd( const QString &text )
{
  return text.toStdString();
}

Json::Value qJsonObjectToJsoncpp( const QJsonObject &object )
{
  // The parameters value object crosses the Qt/jsoncpp boundary through its
  // serialized form: value fidelity is what matters here, not byte form.
  const QJsonDocument document( object );
  const QByteArray bytes = document.toJson( QJsonDocument::Compact );
  Json::Value parsed;
  const std::string text = bytes.toStdString();
  const Json::CharReaderBuilder builder;
  const std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string error;
  if ( !reader->parse( text.data(), text.data() + text.size(), &parsed, &error ) )
    return Json::Value( Json::nullValue );
  return parsed;
}

void record( std::vector<ProjectionProblem> &problems, const std::string &code,
             const std::string &field, const std::string &message )
{
  problems.push_back( { code, field, message } );
}

PortFactView projectPort( const sicnu::workflow::PortFact &port,
                          std::vector<ProjectionProblem> &problems )
{
  PortFactView view;
  view.portName = toStd( port.portName );
  view.dataType = toStd( port.dataType );
  view.crs = toStd( port.crs );
  // The radiometric-state vocabulary is shared with the explain state
  // vocabulary; anything outside it is a projection problem and degrades to
  // "not declared" instead of leaking an unknown token into explanations.
  view.stateToken = toStd( port.radiometricState );
  if ( !view.stateToken.empty() && !isKnownStateToken( view.stateToken ) )
  {
    record( problems, "state_token_unknown", "port." + view.portName + ".radiometricState",
            "state token '" + view.stateToken + "' is outside the shared vocabulary; "
            "projected as not declared" );
    view.stateToken.clear();
  }
  if ( port.resolutionX != 0.0 )
    view.resolutionX = port.resolutionX;
  if ( port.resolutionY != 0.0 )
    view.resolutionY = port.resolutionY;
  if ( port.bandCount != 0 )
    view.bandCount = port.bandCount;
  return view;
}

} // namespace

std::optional<ExplanationRequest> projectWorkflowDocument(
  const sicnu::workflow::WorkflowDocument &document, const std::string &nodeId,
  std::vector<ProjectionProblem> &problems )
{
  const QString qNodeId = QString::fromStdString( nodeId );
  const auto nodeIt = std::find_if( document.nodes.begin(), document.nodes.end(),
                                    [ &qNodeId ]( const sicnu::workflow::NodeFact &node )
                                    { return node.nodeId == qNodeId; } );
  if ( nodeIt == document.nodes.end() )
  {
    record( problems, "node_unknown", "nodeId", "node '" + nodeId + "' is not in the document" );
    return std::nullopt;
  }

  ExplanationRequest request;
  request.workflowKind = WorkflowKindD17Designer;
  request.workflowId = toStd( document.workflowId );
  request.workflowTitle = toStd( document.name );
  request.stepId = nodeId;
  request.stepTitle = toStd( nodeIt->displayName );
  // Every IR 2.0 node is an operator invocation (the parser refuses
  // operator-less nodes): the projected step is an operator step.
  request.stepKind = StepKindOperator;
  request.operatorId = toStd( nodeIt->operatorId );
  request.parameters = qJsonObjectToJsoncpp( nodeIt->parameters );
  for ( const sicnu::workflow::PortFact &port : nodeIt->inputPorts )
    request.inputPorts.push_back( projectPort( port, problems ) );
  for ( const sicnu::workflow::PortFact &port : nodeIt->outputPorts )
    request.outputPorts.push_back( projectPort( port, problems ) );

  if ( request.operatorId.empty() )
  {
    record( problems, "operator_missing", "node.operatorId",
            "node '" + nodeId + "' carries no operatorId; the builder will treat it as "
            "a non-operator step" );
  }
  return request;
}

std::optional<ExplanationRequest> projectEngine2Step( const sicnu::workflow::WorkflowDefinition &definition,
                                                      const std::string &stepId,
                                                      const std::string &executionStatus,
                                                      std::vector<ProjectionProblem> &problems )
{
  const auto stepIt = std::find_if( definition.steps.begin(), definition.steps.end(),
                                    [ &stepId ]( const sicnu::workflow::StepDef &step )
                                    { return step.id == stepId; } );
  if ( stepIt == definition.steps.end() )
  {
    record( problems, "node_unknown", "stepId", "step '" + stepId + "' is not in the definition" );
    return std::nullopt;
  }

  ExplanationRequest request;
  request.workflowKind = WorkflowKindGuidedPipeline;
  request.workflowId = definition.id;
  request.workflowTitle = definition.title;
  request.stepId = stepIt->id;
  request.stepTitle = stepIt->title;
  switch ( stepIt->kind )
  {
    case sicnu::workflow::StepKind::Operator:
      request.stepKind = StepKindOperator;
      request.operatorId = stepIt->operatorId;
      break;
    case sicnu::workflow::StepKind::Interactive:
      request.stepKind = StepKindInteractive;
      break;
    case sicnu::workflow::StepKind::Review:
      request.stepKind = StepKindReview;
      break;
    case sicnu::workflow::StepKind::Composite:
      request.stepKind = StepKindComposite;
      break;
  }
  request.parameters = stepIt->params;
  // Engine 2.0 steps declare no typed port facts; ports stay empty (unknown).
  request.executionStatus = executionStatus;
  return request;
}

} // namespace sicnu::explain::adapters
