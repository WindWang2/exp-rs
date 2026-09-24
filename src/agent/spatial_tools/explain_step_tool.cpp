// src/agent/spatial_tools/explain_step_tool.cpp
//
// RS14-15 Explainable Workflow (Slice G). This TU carries the tool class and
// the guidance-corpus cache only — it deliberately holds no reference to
// SpatialToolRegistry, so the narrow explain test lane can compile it
// directly against Sicnu::explain_adapters without the full agent link set.
// Registration happens in spatial_tool.cpp next to the other built-ins.
#include "explain_step_tool.h"

#include "explain/adapters/registry_operator_knowledge.h"
#include "explain/adapters/workflow_projection.h"
#include "explain/explanation_builder.h"
#include "explain/explanation_validator.h"
#include "explain/guidance_store.h"
#include "explain/step_explanation.h"
#include "explain/step_explanation_view_model.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/rs/rs_operators_init.h"
#include "workflow/workflow_ir_v2.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

#include <json/json.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent::spatial_tools {

namespace {

using sicnu::explain::BuildOutcome;
using sicnu::explain::ExplanationRequest;
using sicnu::explain::StepExplanationViewModel;
using sicnu::explain::ValidationIssue;

constexpr const char *kToolName = "explain:step";
// Serialized-bytes budget for the tool response. The explanation document is
// authoritative and always emitted complete; only the derived markdown
// rendering is dropped when the response would exceed the budget.
constexpr size_t kMaxOutputBytes = 16384;

std::mutex &guidanceMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::string &guidanceDirOverride()
{
  static std::string overrideDir;
  return overrideDir;
}

// The cache is a shared_ptr so a concurrent setExplainGuidanceDirectory()
// reset destroys the old store only after the last in-flight execute() that
// borrowed it has finished (the execute path copies the pointer under the
// mutex and then uses it without the lock held).
std::shared_ptr<const sicnu::explain::GuidanceStore> &cachedGuidanceStore()
{
  static std::shared_ptr<const sicnu::explain::GuidanceStore> store;
  return store;
}

std::string &guidanceLoadReport()
{
  static std::string report;
  return report;
}

// Same resolution policy as RecipeCatalog::defaultDirectory: environment,
// cwd-relative data dir, app-dir-relative data dir, then the configured
// source dir (test/CI lanes).
std::string defaultGuidanceDirectory()
{
  const QString envDir = QProcessEnvironment::systemEnvironment().value(
    QStringLiteral( "SICNU_EXPLAIN_GUIDANCE_DIR" ) );
  if ( !envDir.isEmpty() && QDir( envDir ).exists() )
    return envDir.toStdString();

  const QDir cwdGuidance( QDir::current().filePath( QStringLiteral( "data/explain/guidance" ) ) );
  if ( cwdGuidance.exists() )
    return cwdGuidance.absolutePath().toStdString();

  if ( QCoreApplication::instance() )
  {
    const QDir appGuidance( QCoreApplication::applicationDirPath()
                            + QStringLiteral( "/../data/explain/guidance" ) );
    if ( appGuidance.exists() )
      return appGuidance.absolutePath().toStdString();
  }

#ifdef SICNU_SOURCE_DIR
  {
    const QDir sourceGuidance( QDir( QString::fromUtf8( SICNU_SOURCE_DIR ) )
                                 .filePath( QStringLiteral( "data/explain/guidance" ) ) );
    if ( sourceGuidance.exists() )
      return sourceGuidance.absolutePath().toStdString();
  }
#endif

  return cwdGuidance.absolutePath().toStdString();
}

// The store is cached after the first load; an override (test seam) resets
// it. Load problems are reported verbatim in the load report — a silently
// empty corpus would look like "no authored guidance" and is never masked.
std::shared_ptr<const sicnu::explain::GuidanceStore> guidanceStore()
{
  std::lock_guard<std::mutex> lock( guidanceMutex() );
  std::shared_ptr<const sicnu::explain::GuidanceStore> &store = cachedGuidanceStore();
  if ( !store )
  {
    std::vector<sicnu::explain::GuidanceLoadProblem> problems;
    const std::string directory = guidanceDirOverride().empty() ? defaultGuidanceDirectory()
                                                                : guidanceDirOverride();
    store = sicnu::explain::GuidanceStore::loadFromDirectory( directory, problems );
    guidanceLoadReport() = directory;
    for ( const auto &problem : problems )
      guidanceLoadReport() += " [" + problem.code + ":" + problem.file + "]";
  }
  return store;
}

QJsonObject toQJsonObject( const Json::Value &value, bool &ok )
{
  ok = false;
  if ( !value.isObject() )
    return QJsonObject();
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  const std::string text = Json::writeString( builder, value );
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson( QByteArray::fromStdString( text ), &error );
  if ( error.error != QJsonParseError::NoError || !document.isObject() )
    return QJsonObject();
  ok = true;
  return document.object();
}

size_t serializedBytes( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value ).size();
}

class ExplainStepTool final : public SpatialTool
{
public:
  std::string name() const override { return kToolName; }
  std::string displayName() const override { return "Explain step (why this step)"; }
  std::string description() const override
  {
    return "Explainable workflow (RS14-15): explains ONE workflow step. "
           "mode=operator explains a single operator call against the live operator "
           "registry plus the authored teaching-guidance corpus; "
           "mode=workflow_document explains one node of a Workflow IR 2.0 document. "
           "Returns the canonical exp.step_explanation.v1 document (provenance-tagged: "
           "system_fact | authored_guidance | inferred), a deterministic markdown "
           "rendering, and the validator's hallucination report. Unknown operators and "
           "unresolvable nodes fail closed.";
  }
  std::vector<std::string> tags() const override
  {
    return { "explain", "teaching", "why-this-step", "provenance", "read-only" };
  }

  Json::Value inputSchema() const override
  {
    auto stringField = []( const char *description ) {
      Json::Value field;
      field["type"] = "string";
      field["description"] = description;
      return field;
    };

    Json::Value mode = stringField( "Which representation the step comes from: "
                                    "operator | workflow_document" );
    mode["enum"] = Json::Value( Json::arrayValue );
    mode["enum"].append( "operator" );
    mode["enum"].append( "workflow_document" );

    Json::Value document;
    document["type"] = "object";
    document["description"] = "workflow_document mode: the Workflow IR 2.0 document";

    Json::Value parameters;
    parameters["type"] = "object";
    parameters["description"] = "operator mode: configured parameter values";

    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    schema["properties"] = Json::Value( Json::objectValue );
    schema["properties"]["mode"] = mode;
    schema["properties"]["operatorId"] =
      stringField( "operator mode: registered operator id, e.g. rs:ndvi" );
    schema["properties"]["nodeId"] = stringField( "workflow_document mode: nodeId inside the document" );
    schema["properties"]["document"] = document;
    schema["properties"]["parameters"] = parameters;
    schema["properties"]["workflowId"] =
      stringField( "operator mode: workflow/plan id (default: adhoc)" );
    schema["properties"]["stepId"] = stringField( "operator mode: step id (default: operatorId)" );
    Json::Value required( Json::arrayValue );
    required.append( "mode" );
    schema["required"] = required;
    return schema;
  }

  Json::Value outputSchema() const override
  {
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    schema["description"] = "exp.step_explanation.v1 document + deterministic markdown + "
                            "validator report under an explicit byte budget";
    return schema;
  }

  SpatialToolResult execute( const Json::Value &input ) override
  {
    if ( !input.isObject() || !input.isMember( "mode" ) || !input["mode"].isString() )
      return SpatialToolResult::failure( "missing string parameter 'mode'", "INVALID_PARAMETER",
                                         "validation" );
    const std::string mode = input["mode"].asString();

    sicnu::operators::rs::initBuiltinRsOperators();
    const explain::adapters::RegistryOperatorKnowledge knowledge(
      sicnu::operators::RSOperatorRegistry::instance() );
    const std::shared_ptr<const sicnu::explain::GuidanceStore> store = guidanceStore();

    ExplanationRequest request;
    if ( mode == "operator" )
    {
      if ( !input.isMember( "operatorId" ) || !input["operatorId"].isString()
           || input["operatorId"].asString().empty() )
        return SpatialToolResult::failure( "operator mode requires 'operatorId'",
                                           "INVALID_PARAMETER", "validation" );
      if ( input.isMember( "parameters" ) && serializedBytes( input["parameters"] ) > kMaxOutputBytes )
        return SpatialToolResult::failure( "parameters exceed the serialized-bytes budget",
                                           "INVALID_PARAMETER", "validation" );
      const std::string operatorId = input["operatorId"].asString();
      // Fail closed BEFORE building when the operator is absent: the caller
      // gets a typed refusal, never a skeleton that looks like knowledge.
      if ( !knowledge.findOperator( operatorId ).has_value() )
        return SpatialToolResult::failure(
          "operator '" + operatorId + "' is not registered in the live registry",
          "UNKNOWN_OPERATOR", "validation" );

      std::string workflowId = "adhoc";
      if ( input.isMember( "workflowId" ) && input["workflowId"].isString()
           && !input["workflowId"].asString().empty() )
        workflowId = input["workflowId"].asString();
      std::string stepId = operatorId;
      if ( input.isMember( "stepId" ) && input["stepId"].isString()
           && !input["stepId"].asString().empty() )
        stepId = input["stepId"].asString();

      request = sicnu::explain::makeOperatorRequest(
        sicnu::explain::WorkflowKindAgentPlan, workflowId, stepId, operatorId, operatorId,
        input.isMember( "parameters" ) && input["parameters"].isObject()
          ? input["parameters"]
          : Json::Value( Json::objectValue ) );
    }
    else if ( mode == "workflow_document" )
    {
      if ( !input.isMember( "document" ) || !input["document"].isObject()
           || !input.isMember( "nodeId" ) || !input["nodeId"].isString()
           || input["nodeId"].asString().empty() )
        return SpatialToolResult::failure(
          "workflow_document mode requires 'document' (object) and 'nodeId'",
          "INVALID_PARAMETER", "validation" );

      bool parsed = false;
      const QJsonObject documentObject = toQJsonObject( input["document"], parsed );
      if ( !parsed )
        return SpatialToolResult::failure( "the document could not be converted for the "
                                           "IR 2.0 reader",
                                           "DOCUMENT_UNPARSEABLE", "validation" );
      const auto document = sicnu::workflow::WorkflowIR::fromJson( documentObject );
      if ( !document.isSuccess() )
        return SpatialToolResult::failure( document.error().toStdString(), "DOCUMENT_REFUSED",
                                           "validation" );

      std::vector<explain::adapters::ProjectionProblem> problems;
      const std::optional<ExplanationRequest> projected = explain::adapters::projectWorkflowDocument(
        document.value(), input["nodeId"].asString(), problems );
      if ( !projected.has_value() )
        return SpatialToolResult::failure(
          problems.empty() ? "node not found" : problems.back().message, "NODE_UNKNOWN",
          "validation" );
      request = *projected;
    }
    else
    {
      return SpatialToolResult::failure( "unknown mode '" + mode + "'", "INVALID_PARAMETER",
                                         "validation" );
    }

    const BuildOutcome outcome =
      sicnu::explain::StepExplanationBuilder( knowledge, *store, nullptr ).build( request );
    if ( outcome.failed() )
      return SpatialToolResult::failure( outcome.failureMessage, outcome.failureCode, "runtime" );

    const sicnu::explain::ValidationReport report =
      sicnu::explain::ExplanationValidator( knowledge, store.get() ).validate( outcome.explanation );

    return respond( outcome, report.issues );
  }

private:
  static SpatialToolResult respond(
    const BuildOutcome &outcome,
    const std::vector<ValidationIssue> &validationIssues )
  {
    const StepExplanationViewModel model =
      StepExplanationViewModel::fromExplanation( outcome.explanation );

    const std::string canonical = sicnu::explain::stepExplanationToCanonicalJson( outcome.explanation );
    const Json::CharReaderBuilder readerBuilder;
    const std::unique_ptr<Json::CharReader> reader( readerBuilder.newCharReader() );
    Json::Value explanationJson;
    std::string parseError;
    if ( !reader->parse( canonical.data(), canonical.data() + canonical.size(), &explanationJson,
                         &parseError ) )
      return SpatialToolResult::failure( "canonical explanation failed to re-parse: " + parseError,
                                         "INTERNAL", "runtime" );

    Json::Value response( Json::objectValue );
    response["explanation"] = explanationJson;

    Json::Value problems( Json::arrayValue );
    for ( const auto &problem : outcome.problems )
    {
      Json::Value entry( Json::objectValue );
      entry["code"] = problem.code;
      entry["field"] = problem.field;
      entry["message"] = problem.message;
      problems.append( entry );
    }
    response["problems"] = problems;

    Json::Value validation( Json::arrayValue );
    for ( const auto &issue : validationIssues )
    {
      Json::Value entry( Json::objectValue );
      entry["code"] = issue.code;
      entry["field"] = issue.field;
      entry["message"] = issue.message;
      validation.append( entry );
    }
    response["validation"] = validation;
    response["valid"] = validation.empty();
    response["guidanceSource"] = explainGuidanceLoadReport();

    // Budget: measured against the FULL response (explanation, problems,
    // validation and diagnostics included). The explanation document is
    // never cut; when the full response would exceed the budget, the derived
    // markdown rendering is dropped and the truncation is reported.
    bool truncated = false;
    response["markdown"] = model.toMarkdown();
    if ( serializedBytes( response ) > kMaxOutputBytes )
    {
      response.removeMember( "markdown" );
      truncated = true;
    }

    Json::Value budget( Json::objectValue );
    // The budget block itself is excluded from its own measurement (it is
    // small and fixed-shape); the reported number covers everything else.
    budget["outputBytes"] = static_cast<Json::UInt64>( serializedBytes( response ) );
    budget["maxOutputBytes"] = static_cast<Json::UInt64>( kMaxOutputBytes );
    budget["truncated"] = truncated;
    response["budget"] = budget;

    return SpatialToolResult::ok( response );
  }
};

} // namespace

SpatialToolPtr createExplainStepTool()
{
  return std::make_shared<ExplainStepTool>();
}

std::string explainGuidanceLoadReport()
{
  std::lock_guard<std::mutex> lock( guidanceMutex() );
  return guidanceLoadReport();
}

void setExplainGuidanceDirectory( const std::string &directory )
{
  std::lock_guard<std::mutex> lock( guidanceMutex() );
  guidanceDirOverride() = directory;
  cachedGuidanceStore().reset();
  guidanceLoadReport().clear();
}

} // namespace sicnu::agent::spatial_tools
