// src/agent/harness/plan_tools.cpp
#include "plan_tools.h"

#include "agent_plan.h"
#include "capability_graph.h"
#include "capability_knowledge.h"
#include "contracts/spatial_contracts.h"
#include "context_ledger.h"
#include "entity_resolver.h"
#include "evidence.h"
#include "grounding_tools.h"
#include "harness_verification.h"
#include "scientific_preflight.h"
#include "spatial_tools/spatial_tool.h"
#include "operators/framework/model_catalog.h"
#include "workflow/workflow_run.h"
#include "workflow/workflow_run_coordinator.h"

#include <QFileInfo>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

/// Harness 7.0 (mission Area D): global repair-attempt ledger, bounded per
/// run. The 4.0 resume bound was "one per status call" — a poll loop could
/// retry forever. The ledger caps automatic repair/resume attempts for the
/// lifetime of the run id.
/// Threading/ownership note: tool execution is serialized by the surfaces
/// that drive it (MCP stdio loop, copilot UI thread), so the map is not
/// mutex-guarded; entries are one string+int per run id for the process
/// lifetime (bounded in practice by the coordinator's own run registry).
constexpr int kMaxRepairAttempts = 3;

int &repairAttempts( const std::string &runId )
{
  static std::map<std::string, int> kLedger;
  return kLedger[ runId ];
}

/// Structural duplicates: deterministic rename pass. Returns the actions
/// applied (empty when none). Only the duplicate occurrence is renamed —
/// first declaration wins, downstream references keep meaning the first.
Json::Value renameDuplicateStepIds( Json::Value &plan, const std::string &suffix )
{
  Json::Value actions( Json::arrayValue );
  if ( !plan.isObject() || !plan["steps"].isArray() )
    return actions;
  std::set<std::string> seen;
  Json::Value steps( Json::arrayValue );
  for ( const Json::Value &step : plan["steps"] )
  {
    Json::Value mutableStep = step;
    const std::string id = step.get( "id", "" ).asString();
    if ( !id.empty() && !seen.insert( id ).second )
    {
      const std::string renamed = id + suffix;
      mutableStep["id"] = renamed;
      actions.append( "renamed duplicate step '" + id + "' -> '" + renamed + "'" );
    }
    if ( !id.empty() )
      seen.insert( mutableStep["id"].asString() );
    steps.append( mutableStep );
  }
  if ( !actions.empty() )
    plan["steps"] = steps;
  return actions;
}

/// Declared outputs referencing unknown steps: deterministic drop pass.
Json::Value dropDanglingOutputs( Json::Value &plan )
{
  Json::Value actions( Json::arrayValue );
  if ( !plan.isObject() || !plan["steps"].isArray() || !plan["outputs"].isArray() )
    return actions;
  std::set<std::string> stepIds;
  for ( const Json::Value &step : plan["steps"] )
    stepIds.insert( step.get( "id", "" ).asString() );
  Json::Value outputs( Json::arrayValue );
  for ( const Json::Value &output : plan["outputs"] )
  {
    const std::string from = output.get( "from_step", "" ).asString();
    if ( !from.empty() && !stepIds.count( from ) )
    {
      actions.append( "dropped output '" + output.get( "name", "" ).asString() +
                      "' referencing unknown step '" + from + "'" );
      continue;
    }
    outputs.append( output );
  }
  if ( outputs.size() != plan["outputs"].size() )
    plan["outputs"] = outputs;
  return actions;
}

Json::Value refsProperty()
{
  Json::Value refs( Json::objectValue );
  refs["type"] = "array";
  refs["description"] = "Named inputs: [{\"name\": \"primary\", \"ref\": \"<asset ref>\"}].";
  Json::Value items( Json::objectValue );
  items["type"] = "object";
  Json::Value props( Json::objectValue );
  Json::Value name( Json::objectValue );
  name["type"] = "string";
  props["name"] = name;
  Json::Value ref( Json::objectValue );
  ref["type"] = "string";
  ref["description"] = "Dataset reference: asset-N id, UUID, path, or display name.";
  props["ref"] = ref;
  items["properties"] = props;
  Json::Value required( Json::arrayValue );
  required.append( "name" );
  required.append( "ref" );
  items["required"] = required;
  refs["items"] = items;
  return refs;
}

/// Reads run state + verifies declared/final outputs (Phase 9). Returns the
/// bounded structured result document shared by harness:run_status and
/// harness:execute_plan. `persistEvidence` (default true) controls the
/// evidence sidecar writes, the ledger rebind, and map confirmation —
/// observation surfaces (harness:explain) pass false so they stay strictly
/// read-only (adversarial review P1).
Json::Value runResultDocument( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                               const AgentPlan *plan, bool persistEvidence = true );

/// Harness 7.0 (mission Area F): derive verification expectations instead of
/// the near-vacuous 4.0 defaults. Layered, most specific wins:
///   1. structural defaults (existing semantics),
///   2. capability-knowledge verification contract for the plan intent
///      (finite/nodata/provenance/uncertainty checks tighten what is open),
///   3. the plan's own verification.expectations block (Pi/recipe authority).
VerificationExpectations deriveExpectations( const AgentPlan *plan,
                                             bool includeDeclared = true )
{
  VerificationExpectations expectations;
  // Workflow-run outputs are plain files today; provenance presence stays
  // warning-class unless knowledge/plan tightens it below.
  expectations.requireProvenance = false;

  if ( !plan )
    return expectations;

  bool provenanceDeclared = false;
  bool uncertaintyDeclared = false;
  const Json::Value &declared =
    plan->verification.get( "expectations", Json::Value() );
  if ( includeDeclared && declared.isObject() )
  {
    if ( declared.isMember( "kind" ) && declared["kind"].isString() )
      expectations.kind = declared["kind"].asString();
    if ( declared.isMember( "crs" ) && declared["crs"].isString() )
      expectations.crs = declared["crs"].asString();
    if ( declared.isMember( "width" ) && declared["width"].isInt() )
      expectations.width = declared["width"].asInt();
    if ( declared.isMember( "height" ) && declared["height"].isInt() )
      expectations.height = declared["height"].asInt();
    // Harness 8.0 (Area G): declared output band count.
    if ( declared.isMember( "expected_band_count" ) && declared["expected_band_count"].isInt() )
      expectations.expectedBandCount = declared["expected_band_count"].asInt();
    if ( declared.isMember( "max_nodata_fraction" ) && declared["max_nodata_fraction"].isNumeric() )
      expectations.maxNodataFraction = declared["max_nodata_fraction"].asDouble();
    if ( declared.isMember( "min_finite_fraction" ) && declared["min_finite_fraction"].isNumeric() )
      expectations.minFiniteFraction = declared["min_finite_fraction"].asDouble();
    if ( declared.isMember( "class_values" ) && declared["class_values"].isArray() )
      expectations.classValues = declared["class_values"];
    if ( declared.isMember( "expected_extent" ) && declared["expected_extent"].isObject() )
      expectations.expectedExtent = declared["expected_extent"];
    if ( declared.isMember( "require_provenance" ) && declared["require_provenance"].isBool() )
    {
      expectations.requireProvenance = declared["require_provenance"].asBool();
      provenanceDeclared = true;
    }
    if ( declared.isMember( "require_uncertainty" ) && declared["require_uncertainty"].isBool() )
    {
      expectations.requireUncertainty = declared["require_uncertainty"].asBool();
      uncertaintyDeclared = true;
    }
  }

  // Capability-knowledge contract for the intent: union of the checks the
  // serving capabilities declare, applied only to knobs the plan left open.
  // operatorsForIntent lazily loads the knowledge layer, so verification
  // strength never depends on whether some earlier tool happened to load it.
  if ( !plan->intent.empty() )
  {
    bool wantsFinite = false;
    bool wantsNodata = false;
    for ( const std::string &operatorId :
          CapabilityKnowledge::instance().operatorsForIntent( plan->intent ) )
    {
      const Json::Value checks = CapabilityKnowledge::instance()
                                   .entryForOperator( operatorId )
                                   .get( "verification", Json::Value() )
                                   .get( "checks", Json::Value( Json::arrayValue ) );
      for ( const Json::Value &check : checks )
      {
        if ( !check.isString() )
          continue;
        wantsFinite |= check.asString() == "finite_fraction";
        wantsNodata |= check.asString() == "nodata_fraction";
        if ( check.asString() == "provenance" )
        {
          if ( !provenanceDeclared )
            expectations.requireProvenance = true;
        }
        else if ( check.asString() == "uncertainty" )
        {
          if ( !uncertaintyDeclared )
            expectations.requireUncertainty = true;
        }
      }
    }
    if ( wantsFinite && expectations.minFiniteFraction <= 0.0 )
      expectations.minFiniteFraction = 0.5;
    if ( wantsNodata && expectations.maxNodataFraction >= 1.0 )
      expectations.maxNodataFraction = 0.9;
  }
  return expectations;
}

/// Harness 8.0 (Area E): the run identity stamped into plan bindings and
/// every evidence sidecar.
Json::Value runIdentityFor( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                            const AgentPlan *plan )
{
  Json::Value identity( Json::objectValue );
  identity["run_id"] = run->runId();
  if ( plan )
  {
    identity["plan_id"] = plan->planId;
    identity["plan_fingerprint"] = planFingerprint( *plan );
    if ( !plan->intent.empty() )
      identity["intent"] = plan->intent;
    if ( !plan->goal.empty() )
      identity["goal"] = plan->goal;
    identity["cleanup"] = plan->cleanup.empty() ? "keep_all" : plan->cleanup;
  }
  return identity;
}

} // namespace

HarnessError validatePlanIdentity( const AgentPlan &plan )
{
  if ( !plan.pins.isObject() || plan.pins.empty() )
    return {};

  // Model pin: "<id>" or "<id>@<version>" must resolve in the ModelCatalog.
  if ( plan.pins.isMember( "model" ) && plan.pins["model"].isString() )
  {
    const std::string modelRef = plan.pins["model"].asString();
    std::string modelError;
    if ( !sicnu::operators::ModelCatalog::instance().resolve( modelRef, &modelError ) )
    {
      Json::Value details( Json::objectValue );
      details["pin"] = modelRef;
      details["reason"] = modelError;
      return HarnessError::make(
        error_codes::kModelNotReady,
        "Pinned model '" + modelRef + "' does not resolve in the model catalog", details,
        true, suggestedAction( "select_model", Json::Value() ) );
    }
  }

  if ( !plan.pins.isMember( "datasets" ) || !plan.pins["datasets"].isObject() )
    return {};

  for ( const std::string &slot : plan.pins["datasets"].getMemberNames() )
  {
    const Json::Value &pin = plan.pins["datasets"][ slot ];
    // The pin itself must resolve to the same entity the slot resolves to.
    std::string pinReference;
    if ( pin.isMember( "asset_entity_id" ) && pin["asset_entity_id"].isString() )
      pinReference = pin["asset_entity_id"].asString();
    else if ( pin.isMember( "asset_id" ) && pin["asset_id"].isString() )
      pinReference = pin["asset_id"].asString();
    else if ( pin.isMember( "path" ) && pin["path"].isString() )
      pinReference = pin["path"].asString();
    if ( pinReference.empty() )
      continue; // shape issues are reported by validateAgentPlan

    std::string slotReference;
    for ( const Json::Value &input : plan.inputs )
    {
      if ( input.isObject() && input.get( "name", "" ).asString() == slot )
      {
        slotReference = input.get( "ref", "" ).asString();
        break;
      }
    }
    if ( slotReference.empty() )
      continue;

    HarnessError slotError;
    const auto slotResolved = resolveDatasetRef(
      QString::fromStdString( slotReference ), &slotError );
    if ( !slotResolved )
      return slotError; // DATASET_NOT_FOUND etc. — preflight-grade failure

    HarnessError pinError;
    const auto pinResolved = resolveDatasetRef(
      QString::fromStdString( pinReference ), &pinError );
    if ( !pinResolved )
    {
      Json::Value details( Json::objectValue );
      details["slot"] = slot;
      details["pin"] = pinReference;
      return HarnessError::make(
        error_codes::kIdentityMismatch,
        "Pinned identity for slot '" + slot + "' no longer resolves", details, true,
        suggestedAction( "reinspect_dataset", Json::Value() ) );
    }

    // Identity match: equal non-empty entity ids, or equal canonical paths,
    // or equal concrete paths. Unregistered files carry no entity ids — two
    // empty ids must never compare equal.
    const bool sameEntity =
      ( !slotResolved->assetEntityId.isEmpty() &&
        slotResolved->assetEntityId == pinResolved->assetEntityId ) ||
      ( !slotResolved->canonicalPath.isEmpty() &&
        slotResolved->canonicalPath == pinResolved->canonicalPath ) ||
      slotResolved->path == pinResolved->path;
    bool matches = sameEntity;
    // Same entity but the pinned revision has moved on — the plan was built
    // against other bytes.
    if ( matches && pin.isMember( "revision" ) && pin["revision"].isNumeric() &&
         slotResolved->revision > 0 )
      matches = slotResolved->revision == pin["revision"].asInt64();
    if ( !matches )
    {
      Json::Value details( Json::objectValue );
      details["slot"] = slot;
      details["pinned"] = pinReference;
      details["resolved"] = slotResolved->toJson();
      return HarnessError::make(
        error_codes::kIdentityMismatch,
        "Input slot '" + slot + "' does not match the pinned dataset identity", details, true,
        suggestedAction( "reinspect_dataset", Json::Value() ) );
    }
  }
  return {};
}

namespace {

/// Phase 10: final map confirmation. Verifies the composed map output when the
/// plan declares one: target layout presence, MapSpec preflight/repair loop,
/// and export gating. Bounded and deterministic — reuses the cartography
/// tools through the registry, no second layout engine.
/// Harness 8.0 (Area J): explainability. One bounded document answering:
/// what data were used, why the method is applicable, what executed, what
/// the verification found, where the evidence lives, and what remains
/// unknown. Assembled ONLY from authoritative stores (live run, capability
/// knowledge, context ledger, evidence sidecars) — no chat memory, no
/// reconstruction from prose.
class ExplainTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:explain"; }
    std::string displayName() const override { return "Run Explanation"; }
    std::string description() const override
    {
      return "Explains a plan run from authoritative records: {run_id, plan?} "
             "→ data used (inputs with resolved identity), method applicability "
             "(serving capabilities + why), what executed (per-step status and "
             "cache hits), verification evidence and sidecar paths, uncertainty "
             "and provenance, and what remains unknown (missing facts, stale "
             "asset context, unresolved decisions). Read-only and bounded.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "explain", "provenance", "uncertainty", "evidence" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value runId( Json::objectValue );
      runId["type"] = "string";
      runId["description"] = "Workflow run id (from harness:execute_plan).";
      props["run_id"] = runId;
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "Optional original plan (enriches data/applicability sections).";
      props["plan"] = plan;
      return objectSchema( std::move( props ), Json::Value() );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["explanation"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string runId = input.get( "run_id", "" ).asString();
      if ( runId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'run_id'",
                                           error_codes::kInvalidParameter, "validation" );

      std::optional<AgentPlan> plan;
      if ( input.isMember( "plan" ) && input["plan"].isObject() )
      {
        AgentPlan parsed;
        HarnessError error;
        if ( readAgentPlan( input["plan"], parsed, error ) )
          plan = parsed;
      }

      const auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
      const long pipelineId = coordinator.pipelineIdForRun( runId );
      auto run = pipelineId >= 0 ? coordinator.runForPipeline( pipelineId ) : nullptr;
      if ( !run )
        return SpatialToolResult::failure( "Unknown run id: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );

      // Plan binding from the ledger when the caller did not re-supply the
      // plan (later turns of a long session).
      Json::Value binding;
      for ( const Json::Value &entry : ContextLedger::instance().planBindings() )
        if ( entry.get( "run_id", "" ).asString() == runId )
          binding = entry;

      Json::Value explanation( Json::objectValue );
      explanation["run_id"] = runId;

      // What data were used.
      Json::Value data( Json::arrayValue );
      if ( plan )
      {
        for ( const Json::Value &entry : plan->inputs )
        {
          Json::Value slot( Json::objectValue );
          slot["name"] = entry.get( "name", "" ).asString();
          slot["reference"] = entry.get( "ref", "" ).asString();
          HarnessError error;
          if ( const auto resolved = resolveDatasetRef(
                 QString::fromStdString( slot["reference"].asString() ), &error ) )
          {
            slot["resolved_path"] = resolved->path.toStdString();
            slot["asset_entity_id"] = resolved->assetEntityId.toStdString();
            slot["revision"] = static_cast<Json::Int64>( resolved->revision );
            if ( const Json::Value cached = cachedUnderstandingFor(
                   resolved->path, resolved->revision ); cached.isObject() )
            {
              slot["modality"] = cached.get( "modality", "" );
              slot["sensor"] = cached.get( "sensor", "" );
              slot["acquisition_time"] = cached.get( "acquisition_time", "" );
              slot["radiometric_state"] = cached.get( "radiometric_state", "" );
            }
          }
          else
          {
            slot["resolution_error"] = error.code;
          }
          data.append( slot );
        }
      }
      explanation["data_used"] = data;

      // Why the method is applicable (capability knowledge for the intent).
      const std::string intent =
        plan ? plan->intent
             : binding.get( "intent", "" ).asString();
      if ( !intent.empty() )
      {
        Json::Value applicability( Json::objectValue );
        applicability["intent"] = intent;
        Json::Value serving( Json::arrayValue );
        for ( const std::string &operatorId :
              CapabilityKnowledge::instance().operatorsForIntent( intent ) )
          serving.append( operatorId );
        applicability["serving_capabilities"] = serving;
        explanation["method_applicability"] = applicability;
      }

      // What actually executed. Observation-only: no sidecar writes, no
      // ledger rebind, no map-repair loops (adversarial review P1).
      Json::Value doc = runResultDocument( run, plan ? &*plan : nullptr,
                                           /*persistEvidence=*/false );
      Json::Value executed( Json::objectValue );
      executed["state"] = doc["state"];
      executed["status"] = doc["status"];
      executed["steps"] = doc["steps"];
      if ( doc.isMember( "auto_resumed" ) )
        executed["auto_resumed"] = doc["auto_resumed"];
      explanation["what_executed"] = executed;

      // Verification evidence + sidecars (from the same run document).
      explanation["verification"] = doc.get( "verification", Json::Value() );
      explanation["evidence"] = doc.get( "evidence", Json::Value( Json::arrayValue ) );

      // Assumptions/warnings stay visible: the failed-but-warning checks.
      Json::Value assumptions( Json::arrayValue );
      for ( const Json::Value &artifact :
            doc.get( "verification", Json::Value() ).get( "artifacts",
                                                          Json::Value( Json::arrayValue ) ) )
        for ( const Json::Value &check : artifact.get( "checks",
                                                       Json::Value( Json::arrayValue ) ) )
          if ( !check.get( "passed", true ).asBool() &&
               check.get( "severity", "" ).asString() == "warning" )
            assumptions.append( check );
      explanation["assumptions"] = assumptions;

      // What remains unknown.
      Json::Value unknowns( Json::objectValue );
      Json::Value unresolvedDecisions( Json::arrayValue );
      for ( const Json::Value &decision : ContextLedger::instance().decisions() )
        if ( decision.get( "status", "" ).asString() != "resolved" )
          unresolvedDecisions.append( decision );
      unknowns["unresolved_decisions"] = unresolvedDecisions;
      Json::Value staleContexts( Json::arrayValue );
      for ( const Json::Value &context : ContextLedger::instance().assetContexts() )
        if ( context.get( "stale", false ).asBool() )
          staleContexts.append( context["path"] );
      // Missing facts for the intent, judged against the primary input's
      // cached understanding (never re-inspects — observation only).
      if ( !intent.empty() && plan && plan->inputs.isArray() && plan->inputs.size() > 0 )
      {
        Json::Value understanding;
        for ( const Json::Value &entry : plan->inputs )
        {
          HarnessError error;
          const auto resolved = resolveDatasetRef(
            QString::fromStdString( entry.get( "ref", "" ).asString() ), &error );
          if ( resolved )
          {
            understanding = cachedUnderstandingFor( resolved->path, resolved->revision );
            break;
          }
        }
        unknowns["missing_facts"] =
          missingFactsForIntent( intent, understanding )[ "missing_facts" ];
      }
      unknowns["stale_asset_contexts"] = staleContexts;
      explanation["unknowns"] = unknowns;

      // Plan identity/provenance anchors.
      if ( plan )
      {
        explanation["plan_id"] = plan->planId;
        explanation["plan_fingerprint"] = planFingerprint( *plan );
        explanation["cleanup"] = plan->cleanup.empty() ? "keep_all" : plan->cleanup;
      }
      else if ( binding.isObject() )
      {
        explanation["plan_id"] = binding.get( "plan_id", "" );
        explanation["plan_fingerprint"] = binding.get( "plan_fingerprint", "" );
      }

      Json::Value out( Json::objectValue );
      out["explanation"] = explanation;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

Json::Value confirmMapOutput( const AgentPlan &plan, const Json::Value &stepsDoc );

class PreflightTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:preflight"; }
    std::string displayName() const override { return "Scientific Preflight"; }
    std::string description() const override
    {
      return "Deterministic scientific preflight (Phase 5): validates that the "
             "resolved inputs are scientifically fit for the intent BEFORE any "
             "execution — NDVI (NIR/Red roles, radiometry, NoData), change "
             "(grid/CRS/resolution/radiometry pair checks), sar_change "
             "(modality, polarization, calibration domain, grid), classify "
             "(training samples), phenology (ordering, band roles). Blocked "
             "means the plan is refused — fix the inputs, never re-try prose.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "preflight", "science", "validation" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value intent( Json::objectValue );
      intent["type"] = "string";
      intent["description"] = "ndvi | change | sar_change | classify | phenology (or empty).";
      props["intent"] = intent;
      props["inputs"] = refsProperty();
      Json::Value required( Json::arrayValue );
      required.append( "intent" );
      required.append( "inputs" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["preflight"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string intent = input.get( "intent", "" ).asString();
      if ( !isKnownIntent( intent ) )
        return SpatialToolResult::failure( "Unknown intent: " + intent,
                                           error_codes::kInvalidParameter, "validation" );
      if ( !input.isMember( "inputs" ) || !input["inputs"].isArray() )
        return SpatialToolResult::failure( "missing array parameter 'inputs'",
                                           error_codes::kInvalidParameter, "validation" );
      const PreflightOutcome outcome = preflightIntent( intent, input["inputs"] );
      Json::Value out( Json::objectValue );
      out["preflight"] = outcome.toJson( intent );
      Json::Value summaries( Json::arrayValue );
      // preflightIntent re-resolves internally; re-run resolution here only to
      // echo typed errors per slot without re-inspecting.
      for ( const auto &entry : input["inputs"] )
      {
        Json::Value summary( Json::objectValue );
        summary["name"] = entry.get( "name", "" ).asString();
        summaries.append( summary );
      }
      out["inputs"] = summaries;
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class PlanTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:plan"; }
    std::string displayName() const override { return "Plan Validate & Compile"; }
    std::string description() const override
    {
      return "Validates an AgentPlan v2 document and compiles it to the "
             "authoritative WorkflowDefinition JSON (single execution engine). "
             "Returns typed validation issues, the compiled workflow JSON, and "
             "resource estimates (per-step and aggregate RAM). Does not run "
             "anything — use harness:execute_plan for that.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "plan", "workflow", "compile", "estimates" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "AgentPlan v2 document (kind: execution_plan).";
      props["plan"] = plan;
      Json::Value required( Json::arrayValue );
      required.append( "plan" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["plan"] = Json::Value( Json::objectValue );
      props["compilable"] = Json::Value( Json::objectValue );
      props["workflow_json"] = Json::Value( Json::objectValue );
      props["estimates"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "plan" ) || !input["plan"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'plan'",
                                           error_codes::kInvalidParameter, "validation" );
      AgentPlan plan;
      HarnessError error;
      if ( !readAgentPlan( input["plan"], plan, error ) )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      Json::Value out( Json::objectValue );
      out["plan"] = planSummary( plan );
      const std::vector<AgentPlanIssue> issues = validateAgentPlan( plan );
      if ( !issues.empty() )
      {
        Json::Value arr( Json::arrayValue );
        for ( const AgentPlanIssue &issue : issues )
        {
          Json::Value entry( Json::objectValue );
          entry["code"] = issue.error.code;
          entry["summary"] = issue.error.summary;
          if ( !issue.stepId.empty() )
            entry["step_id"] = issue.stepId;
          entry["repairable"] = issue.repairable;
          arr.append( entry );
        }
        out["issues"] = arr;
        out["compilable"] = false;
        return SpatialToolResult::ok( std::move( out ) );
      }
      out["compilable"] = true;
      HarnessError compileError;
      const std::string workflowJson = compilePlanToWorkflowJson( plan, compileError );
      if ( workflowJson.empty() )
        return SpatialToolResult::failure( compileError.summary, compileError.code, "validation" );
      out["workflow_json"] = workflowJson;
      out["estimates"] = estimatePlanResources( plan );
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class RepairPlanTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:repair_plan"; }
    std::string displayName() const override { return "Bounded Plan Repair"; }
    std::string description() const override
    {
      return "Deterministic, bounded plan repair (<= 3 passes): applies only "
             "structurally safe fixes — duplicate step ids are renamed (first "
             "declaration wins) and declared outputs referencing unknown steps "
             "are dropped — then re-validates. Everything else (unknown "
             "operator, dangling wiring, verification typos) is returned as "
             "typed advisory issues with suggested actions for the agent, "
             "never guessed. Unchanged steps keep engine execution-cache reuse "
             "(fingerprint-based); repaired plans compile to a fresh run.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "plan", "repair", "validation", "bounded" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "AgentPlan document (v2 or legacy v1) to repair.";
      props["plan"] = plan;
      Json::Value required( Json::arrayValue );
      required.append( "plan" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["repaired_plan"] = Json::Value( Json::objectValue );
      props["passes"] = Json::Value( Json::objectValue );
      props["repair_log"] = Json::Value( Json::arrayValue );
      props["issues"] = Json::Value( Json::arrayValue );
      props["compilable"] = Json::Value( Json::objectValue );
      props["workflow_json"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "plan" ) || !input["plan"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'plan'",
                                           error_codes::kInvalidParameter, "validation" );

      Json::Value working = input["plan"];
      constexpr int kMaxPasses = 3;
      Json::Value repairLog( Json::arrayValue );
      int passes = 0;

      std::vector<AgentPlanIssue> issues;
      for ( ; passes < kMaxPasses; ++passes )
      {
        AgentPlan plan;
        HarnessError error;
        if ( !readAgentPlan( working, plan, error ) )
          return SpatialToolResult::failure( error.summary, error.code, "validation" );
        issues = validateAgentPlan( plan );
        if ( issues.empty() )
          break;

        // One deterministic pass: only structurally safe surgery. Science
        // (operator choice, wiring intent, verification strength) is never
        // auto-edited — those stay advisory for Pi.
        Json::Value actions( Json::arrayValue );
        for ( const Json::Value &applied :
              renameDuplicateStepIds( working, "-r" + std::to_string( passes + 1 ) ) )
          actions.append( applied );
        for ( const Json::Value &applied : dropDanglingOutputs( working ) )
          actions.append( applied );
        if ( actions.empty() )
          break; // nothing deterministically repairable — advisory only
        Json::Value passLog( Json::objectValue );
        passLog["pass"] = passes + 1;
        passLog["actions"] = actions;
        repairLog.append( passLog );
      }

      // Final validation of the (possibly repaired) document.
      Json::Value out( Json::objectValue );
      AgentPlan finalPlan;
      HarnessError error;
      bool ok = readAgentPlan( working, finalPlan, error );
      if ( ok )
      {
        issues = validateAgentPlan( finalPlan );
        out["repaired_plan"] = working;
        out["passes"] = passes;
        out["repair_log"] = repairLog;
        if ( !issues.empty() )
        {
          Json::Value arr( Json::arrayValue );
          for ( const AgentPlanIssue &issue : issues )
          {
            Json::Value entry( Json::objectValue );
            entry["code"] = issue.error.code;
            entry["summary"] = issue.error.summary;
            if ( !issue.stepId.empty() )
              entry["step_id"] = issue.stepId;
            entry["repairable"] = issue.repairable;
            entry["suggested_actions"] = issue.error.suggestedActions;
            arr.append( entry );
          }
          out["issues"] = arr;
          out["compilable"] = false;
          return SpatialToolResult::ok( std::move( out ) );
        }
        out["compilable"] = true;
        HarnessError compileError;
        const std::string workflowJson = compilePlanToWorkflowJson( finalPlan, compileError );
        if ( workflowJson.empty() )
          return SpatialToolResult::failure( compileError.summary, compileError.code,
                                             "validation" );
        out["workflow_json"] = workflowJson;
        out["estimates"] = estimatePlanResources( finalPlan );
        return SpatialToolResult::ok( std::move( out ) );
      }
      return SpatialToolResult::failure( error.summary, error.code, "validation" );
    }
};

class ExecutePlanTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:execute_plan"; }
    std::string displayName() const override { return "Execute Plan"; }
    std::string description() const override
    {
      return "Executes an AgentPlan through the authoritative workflow engine "
             "(WorkflowRunCoordinator -> TaskCenter). Runs deterministic "
             "scientific preflight first (when intent is set) — a blocked "
             "preflight refuses execution. Returns run_id, pipeline_id, and "
             "the immediate run state; observe with harness:run_status. "
             "Transient failures are retried via bounded resume (never "
             "unlimited); a FAIL verification can never surface as success.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "execute", "plan", "workflow", "run" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      props["plan"] = plan;
      Json::Value skipPreflight( Json::objectValue );
      skipPreflight["type"] = "boolean";
      skipPreflight["description"] = "Skip intent preflight (only for custom plans).";
      props["skip_preflight"] = skipPreflight;
      Json::Value required( Json::arrayValue );
      required.append( "plan" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["executed"] = Json::Value( Json::objectValue );
      props["run_id"] = Json::Value( Json::objectValue );
      props["pipeline_id"] = Json::Value( Json::objectValue );
      props["preflight"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isMember( "plan" ) || !input["plan"].isObject() )
        return SpatialToolResult::failure( "missing object parameter 'plan'",
                                           error_codes::kInvalidParameter, "validation" );
      AgentPlan plan;
      HarnessError error;
      if ( !readAgentPlan( input["plan"], plan, error ) )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      // 0. Identity pins (Area E): a plan bound to specific dataset/model
      // identities refuses silently swapped or re-registered inputs.
      if ( plan.pins.isObject() && !plan.pins.empty() )
      {
        const HarnessError identityError = validatePlanIdentity( plan );
        if ( !identityError.code.empty() )
          return SpatialToolResult::failure( identityError.summary, identityError.code,
                                             "validation" );
      }

      // 1. Deterministic preflight gate (Phase 5): blocked -> refuse.
      const bool skip = input.get( "skip_preflight", false ).asBool();
      if ( !skip && !plan.intent.empty() && plan.inputs.isArray() && !plan.inputs.empty() )
      {
        const PreflightOutcome outcome = preflightIntent( plan.intent, plan.inputs );
        if ( outcome.verdict == "blocked" )
        {
          Json::Value out( Json::objectValue );
          out["preflight"] = outcome.toJson( plan.intent );
          out["executed"] = false;
          return SpatialToolResult::ok( std::move( out ) );
        }
      }

      // 2. Compile to the authoritative WorkflowDefinition.
      const std::string workflowJson = compilePlanToWorkflowJson( plan, error );
      if ( workflowJson.empty() )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      // 3. Submit through the single engine seam.
      const long pipelineId =
        sicnu::workflow::WorkflowRunCoordinator::instance().startTrackedPipelineJson(
          workflowJson, false );
      if ( pipelineId < 0 )
        return SpatialToolResult::failure( "Workflow engine rejected the compiled plan",
                                           error_codes::kExecutionFailed, "runtime" );

      Json::Value out( Json::objectValue );
      out["executed"] = true;
      out["pipeline_id"] = static_cast<Json::Int64>( pipelineId );
      out["plan_fingerprint"] = planFingerprint( plan );
      out["cleanup"] = plan.cleanup.empty() ? "keep_all" : plan.cleanup;
      if ( auto run = sicnu::workflow::WorkflowRunCoordinator::instance().runForPipeline(
             pipelineId ) )
      {
        out["run_id"] = run->runId();
        out["status"] = run->state() == sicnu::workflow::WorkflowRunState::Running
                          ? "running"
                          : sicnu::workflow::workflowRunStateToString( run->state() );
        // Harness 7.0 (Area E): bind plan -> run for typed cross-turn context.
        ContextLedger::instance().recordPlanBinding( run->runId(), plan.planId, plan.goal,
                                                     plan.intent, "running",
                                                     planFingerprint( plan ) );
      }
      out["next"] = "harness:run_status {run_id} — poll until terminal";
      return SpatialToolResult::ok( std::move( out ) );
    }
};

class RunStatusTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:run_status"; }
    std::string displayName() const override { return "Run Status & Verification"; }
    std::string description() const override
    {
      return "Observes the REAL workflow run state (no fictional progress): "
             "state, progress, per-step status with cache-hit and output "
             "paths, and — once terminal — automatic output verification with "
             "PASS / PASS_WITH_WARNINGS / FAIL verdicts per artifact and "
             "overall. Status 'failed' is authoritative: never report success "
             "for it. Supports bounded transient retry via resume when the "
             "failure was transient and attempts remain.";
    }
    std::vector<std::string> tags() const override
    { return { "harness", "run", "status", "verification", "resume" }; }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value runId( Json::objectValue );
      runId["type"] = "string";
      runId["description"] = "Workflow run id (from harness:execute_plan).";
      props["run_id"] = runId;
      Json::Value plan( Json::objectValue );
      plan["type"] = "object";
      plan["description"] = "Optional original plan (enables map confirmation + retry policy).";
      props["plan"] = plan;
      Json::Value resume( Json::objectValue );
      resume["type"] = "boolean";
      resume["description"] = "Resume once if the run failed transiently (default true, "
                              "bounded to one automatic resume).";
      props["auto_resume_transient"] = resume;
      Json::Value required( Json::arrayValue );
      required.append( "run_id" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["run_id"] = Json::Value( Json::objectValue );
      props["state"] = Json::Value( Json::objectValue );
      props["status"] = Json::Value( Json::objectValue );
      props["steps"] = Json::Value( Json::arrayValue );
      props["verification"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      const std::string runId = input.get( "run_id", "" ).asString();
      if ( runId.empty() )
        return SpatialToolResult::failure( "missing string parameter 'run_id'",
                                           error_codes::kInvalidParameter, "validation" );
      auto &coordinator = sicnu::workflow::WorkflowRunCoordinator::instance();
      const long pipelineId = coordinator.pipelineIdForRun( runId );
      if ( pipelineId < 0 )
        return SpatialToolResult::failure( "Unknown run id: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );
      auto run = coordinator.runForPipeline( pipelineId );
      if ( !run )
        return SpatialToolResult::failure( "Run is no longer tracked: " + runId,
                                           error_codes::kWorkflowNotFound, "validation" );

      std::optional<AgentPlan> plan;
      if ( input.isMember( "plan" ) && input["plan"].isObject() )
      {
        AgentPlan parsed;
        HarnessError error;
        if ( readAgentPlan( input["plan"], parsed, error ) )
          plan = parsed;
      }

      Json::Value doc = runResultDocument( run, plan ? &*plan : nullptr );

      // Bounded transient retry (Phase 13 + 7.0 Area D): a failed run whose
      // recorded error is transient-class may be resumed — but attempts are
      // counted in a global per-run ledger (kMaxRepairAttempts), so a poll
      // loop cannot retry forever. Resume re-runs only steps without valid
      // outputs — completed work is never redone.
      doc["repair_attempts"] = repairAttempts( runId );
      doc["repair_attempt_limit"] = kMaxRepairAttempts;
      if ( run->state() == sicnu::workflow::WorkflowRunState::Failed &&
           input.get( "auto_resume_transient", true ).asBool() )
      {
        const std::string message = run->errorMessage();
        const HarnessError normalized = normalizeLegacyError( "EXECUTION_FAILED", message );
        const bool transient = message.find( "timed out" ) != std::string::npos ||
                               message.find( "Transient" ) != std::string::npos ||
                               message.find( "transient" ) != std::string::npos ||
                               normalized.code == error_codes::kTransientFailure;
        int &attempts = repairAttempts( runId );
        if ( transient && attempts < kMaxRepairAttempts && !doc.isMember( "auto_resumed" ) )
        {
          ++attempts;
          QString resumeError;
          const long resumed = coordinator.resumeRun( runId, &resumeError );
          Json::Value retry( Json::objectValue );
          retry["attempted"] = resumed > 0;
          retry["policy"] = "ledger_bounded_resume";
          retry["attempt"] = attempts;
          retry["attempt_limit"] = kMaxRepairAttempts;
          if ( resumed > 0 )
          {
            retry["resumed_pipeline_id"] = static_cast<Json::Int64>( resumed );
            doc["auto_resume"] = retry;
            if ( auto resumedRun = coordinator.runForPipeline( resumed ) )
            {
              doc = runResultDocument( resumedRun, plan ? &*plan : nullptr );
              doc["auto_resumed"] = true;
              doc["repair_attempts"] = attempts;
              doc["repair_attempt_limit"] = kMaxRepairAttempts;
            }
          }
          else
          {
            retry["error"] = resumeError.toStdString();
            doc["auto_resume"] = retry;
          }
        }
      }
      return SpatialToolResult::ok( std::move( doc ) );
    }
};

Json::Value runResultDocument( const std::shared_ptr<sicnu::workflow::WorkflowRun> &run,
                               const AgentPlan *plan, bool persistEvidence )
{
  const bool runResultDocumentPersistEvidence = persistEvidence;
  Json::Value doc( Json::objectValue );
  doc["run_id"] = run->runId();
  doc["state"] = sicnu::workflow::workflowRunStateToString( run->state() );
  doc["progress"] = run->progress();
  const bool terminal = sicnu::workflow::isTerminalRunState( run->state() );

  Json::Value steps( Json::arrayValue );
  std::vector<std::string> outputPaths;
  for ( const auto &step : run->stepPlans() )
  {
    Json::Value entry( Json::objectValue );
    entry["step_id"] = step.stepId;
    entry["status"] = step.status;
    entry["cache_hit"] = step.cacheHit;
    if ( !step.outputLayerPath.empty() )
    {
      entry["output"] = step.outputLayerPath;
      if ( step.status == "Completed" )
        outputPaths.push_back( step.outputLayerPath );
    }
    if ( !step.errorMessage.empty() )
      entry["error"] = step.errorMessage;
    if ( step.taskId > 0 )
      entry["execution_id"] = "task-" + std::to_string( step.taskId );
    steps.append( entry );
  }
  doc["steps"] = steps;

  Json::Value verificationDoc( Json::objectValue );
  if ( terminal && run->state() == sicnu::workflow::WorkflowRunState::Completed )
  {
    const VerificationExpectations derived = deriveExpectations( plan );
    // Harness 8.0 (adversarial review): plan-declared expectations (width/
    // height/CRS/band count/extent) apply only to the plan's DECLARED
    // outputs — enforcing them on intermediate products would force false
    // FAILs in multimodal/classified chains. Structural + knowledge-derived
    // checks apply to every completed output.
    VerificationExpectations baseDerived = deriveExpectations( plan, false );

    // Declared output paths: plan.outputs {from_step} → completed step path.
    std::set<std::string> declaredPaths;
    if ( plan && plan->outputs.isArray() )
    {
      std::map<std::string, std::string> stepOutput;
      for ( const auto &step : run->stepPlans() )
        if ( !step.outputLayerPath.empty() )
          stepOutput[ step.stepId ] = step.outputLayerPath;
      for ( const Json::Value &output : plan->outputs )
      {
        const auto it = stepOutput.find( output.get( "from_step", "" ).asString() );
        if ( it != stepOutput.end() )
          declaredPaths.insert( it->second );
      }
    }

    // Harness 8.0 (Area F): first evaluation writes the evidence sidecars
    // and persists the verification record; subsequent observations of the
    // same run reuse the persisted record — polls are stable, read-only and
    // can never re-flip a verdict (adversarial review P1/P2).
    const Json::Value runIdentity = runIdentityFor( run, plan );
    const bool persistEvidence = runResultDocumentPersistEvidence;

    std::vector<ArtifactVerification> verifications;
    Json::Value artifacts( Json::arrayValue );
    Json::Value evidenceArtifacts( Json::arrayValue );
    for ( const std::string &path : outputPaths )
    {
      const bool declaredOutput = declaredPaths.count( path ) > 0;
      const VerificationExpectations &expectationsForArtifact =
        declaredOutput ? derived : baseDerived;

      // Write-once: an existing verification sidecar for THIS run is
      // authoritative — reuse its verdict and checks verbatim.
      if ( const auto reused = evidence::readVerificationEvidence( path, run->runId() ) )
      {
        verifications.push_back( *reused );
        artifacts.append( reused->toJson() );
        Json::Value evidenceEntry( Json::objectValue );
        evidenceEntry["path"] = path;
        evidenceEntry["reused"] = true;
        evidenceEntry["verification_sidecar"] = path + ".verification.json";
        evidenceEntry["provenance_sidecar"] = path + ".provenance.json";
        if ( QFileInfo::exists( QString::fromStdString( path + ".uncertainty.json" ) ) )
          evidenceEntry["uncertainty_sidecar"] = path + ".uncertainty.json";
        else
          evidenceEntry["uncertainty_declared"] = false;
        evidenceArtifacts.append( evidenceEntry );
        continue;
      }

      // First evaluation for this run+artifact.
      ArtifactVerification artifact;

      // 1. Uncertainty: operator-declared facts only; a method that produces
      //    no uncertainty yields no sidecar and no fabrication. Written
      //    BEFORE verification so the presence check reflects this run.
      evidence::UncertaintyHarvest harvest;
      evidence::SidecarResult uncertaintyWritten;
      evidence::SidecarResult provenanceWritten;
      evidence::SidecarResult verificationWritten;
      if ( persistEvidence )
      {
        harvest = evidence::harvestUncertainty( run->stepPlans(), path );
        uncertaintyWritten = evidence::writeUncertaintySidecar( path, harvest );
      }

      // 2. Verify. The run-identity provenance sidecar is deliberately NOT
      //    written yet (adversarial review): `provenance_present` must
      //    reflect derivation provenance (engine sidecar / catalog), never
      //    the harness's own bookkeeping.
      artifact = verifyArtifact( path, expectationsForArtifact );

      if ( persistEvidence )
      {
        // 3. Now write run-identity provenance (engine sidecar always wins).
        provenanceWritten = evidence::writeProvenanceSidecarIfAbsent( path, runIdentity );
        if ( !provenanceWritten.written && !provenanceWritten.error.empty() )
        {
          VerificationCheck failed;
          failed.check = "provenance_written";
          failed.passed = false;
          failed.severity = "warning";
          failed.code = error_codes::kOutputInvalid;
          failed.details["error"] = provenanceWritten.error;
          appendCheck( artifact, std::move( failed ) );
        }

        // 4. Declared uncertainty that could not be persisted is an evidence
        //    failure (error-class) — declared science evidence never
        //    silently disappears. A missing file for a method that declares
        //    none stays the existing warning-class advisory check.
        if ( harvest.declared && !uncertaintyWritten.written )
        {
          VerificationCheck failed;
          failed.check = "uncertainty_written";
          failed.passed = false;
          failed.severity = "error";
          failed.code = error_codes::kOutputInvalid;
          failed.details["error"] = uncertaintyWritten.error;
          appendCheck( artifact, std::move( failed ) );
        }

        // 5. Persist the verification evidence itself.
        verificationWritten = evidence::writeVerificationEvidence(
          path, artifact, expectationsForArtifact, runIdentity, harvest );
        if ( !verificationWritten.written && !verificationWritten.error.empty() )
        {
          VerificationCheck failed;
          failed.check = "verification_evidence_written";
          failed.passed = false;
          failed.severity = "warning";
          failed.code = error_codes::kOutputInvalid;
          failed.details["error"] = verificationWritten.error;
          appendCheck( artifact, std::move( failed ) );
        }
      }

      Json::Value evidenceEntry( Json::objectValue );
      evidenceEntry["path"] = path;
      evidenceEntry["verification_sidecar"] =
        verificationWritten.written ? Json::Value( verificationWritten.path ) : Json::Value();
      evidenceEntry["provenance_sidecar"] =
        ( provenanceWritten.written ||
          QFileInfo::exists( QString::fromStdString( path + ".provenance.json" ) ) )
          ? Json::Value( path + ".provenance.json" )
          : Json::Value();
      if ( harvest.declared )
        evidenceEntry["uncertainty_sidecar"] =
          uncertaintyWritten.written ? Json::Value( uncertaintyWritten.path ) : Json::Value();
      else
        evidenceEntry["uncertainty_declared"] = false;
      evidenceArtifacts.append( evidenceEntry );

      verifications.push_back( artifact );
      artifacts.append( artifact.toJson() );
    }
    const Verdict overall = aggregateVerdict( verifications );
    verificationDoc["artifacts"] = artifacts;
    verificationDoc["verdict"] = verdictToStringWire( overall );
    verificationDoc["expectations"] = [ &derived ] {
      Json::Value e( Json::objectValue );
      if ( !derived.crs.empty() )
        e["crs"] = derived.crs;
      if ( derived.width )
        e["width"] = derived.width;
      if ( derived.height )
        e["height"] = derived.height;
      if ( derived.expectedBandCount > 0 )
        e["expected_band_count"] = derived.expectedBandCount;
      if ( derived.minFiniteFraction > 0.0 )
        e["min_finite_fraction"] = derived.minFiniteFraction;
      if ( derived.maxNodataFraction < 1.0 )
        e["max_nodata_fraction"] = derived.maxNodataFraction;
      if ( derived.classValues.isArray() && !derived.classValues.empty() )
        e["class_values"] = derived.classValues;
      if ( derived.expectedExtent.isObject() )
        e["expected_extent"] = derived.expectedExtent;
      e["require_provenance"] = derived.requireProvenance;
      if ( derived.requireUncertainty )
        e["require_uncertainty"] = true;
      return e;
    }();
    doc["verification"] = verificationDoc;
    doc["evidence"] = evidenceArtifacts;
    // A FAIL verification forces status failed — no false success path.
    doc["status"] = overall == Verdict::Fail ? "failed" : "completed";
    // Harness 7.0 (Area E): the binding's verification status follows the
    // run. Harness 8.0: observation surfaces (harness:explain) must not
    // mutate the ledger, so the rebind happens on the persisting path only.
    if ( runResultDocumentPersistEvidence )
    {
      ContextLedger::instance().recordPlanBinding(
        run->runId(), plan ? plan->planId : "", plan ? plan->goal : "",
        plan ? plan->intent : "", verdictToStringWire( overall ),
        plan ? planFingerprint( *plan ) : "" );
      if ( overall != Verdict::Fail && plan && plan->mapOutput.isObject() )
        doc["map_confirmation"] = confirmMapOutput( *plan, doc["steps"] );
    }
  }
  else if ( terminal && run->state() == sicnu::workflow::WorkflowRunState::Failed )
  {
    verificationDoc["verdict"] = "FAIL";
    verificationDoc["reason"] = "run failed before outputs could be verified";
    doc["verification"] = verificationDoc;
    doc["status"] = "failed";
    doc["error"] = run->errorMessage();
    ContextLedger::instance().recordPlanBinding(
      run->runId(), plan ? plan->planId : "", plan ? plan->goal : "",
      plan ? plan->intent : "", "FAIL" );
  }
  else
  {
    doc["status"] = "running";
  }
  return doc;
}

Json::Value confirmMapOutput( const AgentPlan &plan, const Json::Value &stepsDoc )
{
  Json::Value confirmation( Json::objectValue );
  const Json::Value mapOutput = plan.mapOutput;
  const std::string layout = mapOutput.get( "layout_name", "" ).asString();
  confirmation["layout"] = layout;

  // 1. The plan step that produced the map output must have completed.
  std::string fromStep = mapOutput.get( "from_step", "" ).asString();
  if ( fromStep.empty() && plan.outputs.isArray() && plan.outputs.size() > 0 )
    fromStep = plan.outputs[0].get( "from_step", "" ).asString();
  bool stepCompleted = false;
  for ( const auto &step : stepsDoc )
  {
    if ( step.get( "step_id", "" ).asString() == fromStep &&
         step.get( "status", "" ).asString() == "Completed" )
      stepCompleted = true;
  }
  if ( !stepCompleted && !fromStep.empty() )
  {
    confirmation["verdict"] = "FAIL";
    confirmation["reason"] = "map-producing step did not complete: " + fromStep;
    return confirmation;
  }

  // 2. MapSpec path: compose -> preflight -> bounded repair loop through the
  // existing cartography tools (registry), then export gating.
  const std::string verdictFail = verdictToStringWire( Verdict::Fail );
  if ( mapOutput.isMember( "mapspec" ) )
  {
    auto compose = SpatialToolRegistry::instance().find( "cartography:compose" );
    auto preflight = SpatialToolRegistry::instance().find( "cartography:preflight" );
    auto repair = SpatialToolRegistry::instance().find( "cartography:repair" );
    if ( !compose || !preflight )
    {
      confirmation["verdict"] = "FAIL";
      confirmation["reason"] = "cartography tools unavailable";
      return confirmation;
    }
    Json::Value composeInput;
    composeInput["mapspec"] = mapOutput["mapspec"];
    if ( !layout.empty() )
      composeInput["layout_name"] = layout;
    const SpatialToolResult composed = ( *compose )->execute( composeInput );
    if ( !composed.success )
    {
      confirmation["verdict"] = verdictFail;
      confirmation["reason"] = composed.error;
      return confirmation;
    }
    if ( composed.output.isMember( "quality" ) )
      confirmation["compose_quality"] = composed.output["quality"];
    // Platform 8.0: the confirmation identifies WHAT was composed — the
    // rendering-free structural digest and the declared template/component
    // provenance come straight from cartography:compose (no second
    // digest/provenance implementation here).
    if ( composed.output.isMember( "structural_digest" ) )
      confirmation["structural_digest"] = composed.output["structural_digest"];
    if ( composed.output.isMember( "provenance" ) )
      confirmation["provenance"] = composed.output["provenance"];
    if ( composed.output.isMember( "declared_output" ) )
      confirmation["declared_output"] = composed.output["declared_output"];

    Json::Value preflightInput;
    preflightInput["mapspec"] = mapOutput["mapspec"];
    SpatialToolResult checked = ( *preflight )->execute( preflightInput );
    int repairs = 0;
    while ( checked.success && checked.output.isObject() &&
            checked.output.get( "passed", true ).asBool() == false && repairs < 3 && repair )
    {
      Json::Value repairInput;
      repairInput["mapspec"] = mapOutput["mapspec"];
      repairInput["issues"] = checked.output.get( "issues", Json::Value( Json::arrayValue ) );
      const SpatialToolResult repaired = ( *repair )->execute( repairInput );
      if ( !repaired.success )
        break;
      checked = ( *preflight )->execute( preflightInput );
      ++repairs;
    }
    confirmation["repair_passes"] = repairs;
    const bool passed = checked.success && checked.output.isObject() &&
                        checked.output.get( "passed", false ).asBool();
    confirmation["verdict"] = passed ? verdictToStringWire( Verdict::Pass ) : verdictFail;
    if ( checked.output.isObject() && checked.output.isMember( "quality_score" ) )
      confirmation["quality_score"] = checked.output["quality_score"];
    return confirmation;
  }

  // 3. Layout-only path: the referenced layout must exist and export must
  // have been requested explicitly — presence checks only, no guessing.
  if ( !layout.empty() )
  {
    auto listLayouts = SpatialToolRegistry::instance().find( "layout:list" );
    bool layoutExists = false;
    if ( listLayouts )
    {
      const SpatialToolResult listed = ( *listLayouts )->execute( Json::Value() );
      if ( listed.success && listed.output.isObject() )
      {
        for ( const auto &entry : listed.output.get( "layouts", Json::Value( Json::arrayValue ) ) )
        {
          if ( entry.get( "name", "" ).asString() == layout )
            layoutExists = true;
        }
      }
    }
    if ( !layoutExists )
    {
      confirmation["verdict"] = verdictFail;
      confirmation["reason"] = "declared map layout does not exist: " + layout;
      return confirmation;
    }
    confirmation["verdict"] = verdictToStringWire( Verdict::Pass );
    confirmation["note"] = "layout exists; run cartography:preflight for full map QA";
    return confirmation;
  }

  confirmation["verdict"] = verdictToStringWire( Verdict::PassWithWarnings );
  confirmation["reason"] = "map_output declared without layout/mapspec — nothing to confirm";
  return confirmation;
}

} // namespace

void registerPlanTools()
{
  auto &registry = SpatialToolRegistry::instance();
  registry.registerTool( std::make_shared<PreflightTool>() );
  registry.registerTool( std::make_shared<PlanTool>() );
  registry.registerTool( std::make_shared<RepairPlanTool>() );
  registry.registerTool( std::make_shared<ExecutePlanTool>() );
  registry.registerTool( std::make_shared<RunStatusTool>() );
  // Harness 8.0 (Area J): evidence-driven explanation surface.
  registry.registerTool( std::make_shared<ExplainTool>() );
}

} // namespace sicnu::agent::harness
