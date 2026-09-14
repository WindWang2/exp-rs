// src/agent/harness/workflow_planner.cpp
#include "workflow_planner.h"

#include "capability_graph.h"
#include "capability_knowledge.h"
#include "context_ledger.h"
#include "entity_resolver.h"
#include "recipe_catalog.h"
#include "../spatial_tools/spatial_tool.h"

#include <algorithm>
#include <map>
#include <set>

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

SpatialToolPtr findTool( const std::string &name )
{
  return SpatialToolRegistry::instance().find( name ).value_or( nullptr );
}

} // namespace

Json::Value PlannerStageReport::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["stage"] = stage;
  doc["status"] = status;
  if ( !summary.empty() )
    doc["summary"] = summary;
  if ( details.isObject() && !details.empty() )
    doc["details"] = details;
  return doc;
}

Json::Value CompiledWorkflow::stagesJson() const
{
  Json::Value list( Json::arrayValue );
  for ( const PlannerStageReport &stage : stages )
    list.append( stage.toJson() );
  return list;
}

namespace {

PlannerStageReport stageReport( const std::string &stage, const std::string &status,
                                const std::string &summary, Json::Value details = Json::Value() )
{
  PlannerStageReport report;
  report.stage = stage;
  report.status = status;
  report.summary = summary;
  report.details = details.isObject() ? details : Json::Value( Json::objectValue );
  return report;
}

/// The understanding document's concrete path (grounding output).
std::string pathOfFacts( const Json::Value &facts )
{
  if ( !facts.isObject() )
    return {};
  return facts.get( "path", "" ).asString();
}

/// Converts plan step wiring into IR node edges. Plan edges:
/// [{step, port, to_port}].
bool planStepToIrNode( const Json::Value &step, IrNode &node, HarnessError &error )
{
  node.id = step.get( "id", "" ).asString();
  node.operatorId = step.get( "operator_id", step.get( "operatorId", "" ) ).asString();
  if ( node.id.empty() || node.operatorId.empty() )
  {
    error = HarnessError::make( error_codes::kInvalidPlan,
                                "Plan step is missing id or operator_id" );
    return false;
  }
  node.params = step.isMember( "params" ) && step["params"].isObject()
                  ? step["params"]
                  : Json::Value( Json::objectValue );
  if ( step.isMember( "inputs" ) && step["inputs"].isArray() )
  {
    for ( const Json::Value &edge : step["inputs"] )
    {
      IrNodeInput parsed;
      parsed.node = edge.get( "step", "" ).asString();
      parsed.output = edge.get( "port", "output" ).asString();
      parsed.as = edge.get( "to_port", "input" ).asString();
      if ( parsed.node.empty() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    "Plan step wiring is missing the upstream step id" );
        return false;
      }
      node.inputs.push_back( parsed );
    }
  }
  node.verification = step.get( "verification", "" ).asString();
  if ( step.isMember( "resource_estimate_mb" ) && step["resource_estimate_mb"].isNumeric() )
    node.resourceEstimateMb = step["resource_estimate_mb"].asInt64();
  node.semanticOutput = step.get( "role", "" ).asString(); // plan roles project to semantics
  node.source = "agent";
  return true;
}

} // namespace

bool planToWorkflowIr( const Json::Value &planDoc, WorkflowIr &ir, HarnessError &error )
{
  AgentPlan plan;
  if ( !readAgentPlan( planDoc, plan, error ) )
    return false;

  ir = WorkflowIr{};
  ir.raw = planDoc;
  ir.goal = plan.goal;
  ir.intent = plan.intent;
  ir.irId = planDoc.get( "plan_id", "" ).asString();

  for ( const Json::Value &input : plan.inputs )
  {
    if ( !input.isObject() )
      continue;
    IrInputSlot slot;
    slot.name = input.get( "name", "" ).asString();
    slot.reference = input.get( "ref", "" ).asString();
    if ( slot.name.empty() || slot.reference.empty() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  "Plan inputs need name and ref to compile to an IR" );
      return false;
    }
    ir.inputs.push_back( std::move( slot ) );
  }

  for ( const Json::Value &step : plan.steps )
  {
    IrNode node;
    if ( !planStepToIrNode( step, node, error ) )
      return false;
    ir.nodes.push_back( std::move( node ) );
  }

  for ( const Json::Value &output : plan.outputs )
  {
    if ( !output.isObject() )
      continue;
    IrOutputDecl decl;
    decl.name = output.get( "name", "" ).asString();
    decl.node = output.get( "from_step", "" ).asString();
    decl.port = output.get( "port", "output" ).asString();
    decl.kind = output.get( "kind", "" ).asString();
    if ( decl.name.empty() || decl.node.empty() )
      continue;
    ir.outputs.push_back( std::move( decl ) );
  }

  normalizeWorkflowIr( ir );
  if ( ir.irId.empty() )
    ir.irId = deriveIrId( ir );
  return true;
}

Json::Value agentPlanToDocument( const AgentPlan &plan )
{
  Json::Value doc( Json::objectValue );
  doc["kind"] = "execution_plan";
  doc["schema_version"] = kAgentPlanSchemaVersion;
  doc["plan_id"] = plan.planId.empty() ? "plan-agent" : plan.planId;
  doc["goal"] = plan.goal;
  doc["intent"] = plan.intent;
  doc["inputs"] = plan.inputs;
  doc["steps"] = plan.steps;
  doc["outputs"] = plan.outputs;
  doc["verification"] = plan.verification;
  if ( plan.mapOutput.isObject() || plan.mapOutput.isNull() )
    doc["map_output"] = plan.mapOutput;
  if ( plan.pins.isObject() && !plan.pins.empty() )
    doc["pins"] = plan.pins;
  if ( !plan.cleanup.empty() )
    doc["cleanup"] = plan.cleanup;
  // Compiler provenance survives round-trips: execute_plan reads it from the
  // raw document to bind it into the run context.
  if ( plan.raw.isObject() && plan.raw.isMember( "workflow_ir" ) )
    doc["workflow_ir"] = plan.raw["workflow_ir"];
  return doc;
}

bool lowerIrToAgentPlan( const WorkflowIr &ir, const Json::Value &resolvedSlotPaths,
                         AgentPlan &plan, HarnessError &error )
{
  const std::vector<AgentPlanIssue> structural = validateIrStructure( ir );
  if ( !structural.empty() )
  {
    Json::Value details( Json::objectValue );
    Json::Value codes( Json::arrayValue );
    for ( const AgentPlanIssue &issue : structural )
      codes.append( issue.error.code );
    details["issues"] = codes;
    error = HarnessError::make( error_codes::kInvalidPlan,
                                "IR failed structural validation before lowering", details );
    return false;
  }

  const std::string outputDir = ir.expectations.get( "output_dir", "" ).asString();

  plan = AgentPlan{};
  plan.planId = ir.irId;
  plan.goal = ir.goal;
  plan.intent = ir.intent;

  for ( const IrInputSlot &slot : ir.inputs )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = slot.name;
    entry["ref"] = slot.reference;
    plan.inputs.append( entry );
  }

  bool anyVerification = false;
  for ( const IrNode &node : ir.nodes )
  {
    Json::Value step( Json::objectValue );
    step["id"] = node.id;
    step["operator_id"] = node.operatorId;
    step["params"] = node.params;
    if ( node.params.isObject() && !node.params.isMember( "output" ) && !outputDir.empty() )
      step["params"]["output"] = derivedOutputPath( ir, node, outputDir );
    Json::Value edges( Json::arrayValue );
    for ( const IrNodeInput &edge : node.inputs )
    {
      Json::Value wire( Json::objectValue );
      if ( !edge.node.empty() )
      {
        wire["step"] = edge.node;
        wire["port"] = edge.output;
        wire["to_port"] = edge.as;
        edges.append( wire );
      }
      else
      {
        // Slot wiring needs the grounded path — that is the execution-time
        // binding; an ungrounded slot cannot be lowered.
        Json::Value resolved;
        if ( resolvedSlotPaths.isObject() && resolvedSlotPaths.isMember( edge.input ) )
          resolved = resolvedSlotPaths[edge.input];
        if ( !resolved.isString() || resolved.asString().empty() )
        {
          Json::Value details( Json::objectValue );
          details["slot"] = edge.input;
          details["node"] = node.id;
          error = HarnessError::make(
            error_codes::kInvalidPlan,
            "Cannot lower: input slot '" + edge.input + "' has no grounded path", details );
          return false;
        }
        step["params"][ edge.as ] = resolved.asString();
      }
    }
    if ( !edges.empty() )
      step["inputs"] = edges;
    if ( !node.verification.empty() )
    {
      step["verification"] = node.verification;
      anyVerification = anyVerification || node.verification != "skip";
    }
    plan.steps.append( step );
  }

  for ( const IrOutputDecl &output : ir.outputs )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = output.name;
    entry["from_step"] = output.node;
    entry["port"] = output.port;
    if ( !output.kind.empty() )
      entry["kind"] = output.kind;
    plan.outputs.append( entry );
  }

  Json::Value verification( Json::objectValue );
  verification["enabled"] = anyVerification;
  plan.verification = verification;

  return true;
}

CompiledWorkflow compileWorkflow( const CompileWorkflowRequest &request, HarnessError &error )
{
  CompiledWorkflow result;
  if ( !request.irDoc.isObject() && request.recipeId.empty() )
  {
    error = HarnessError::makeWithAction(
      error_codes::kInvalidParameter,
      "compileWorkflow needs an 'ir' document or a 'recipe_id'",
      "harness:compile_workflow", Json::Value() );
    return result;
  }

  // Stage: parse (IR document or recipe instantiation -> IR).
  WorkflowIr ir;
  if ( request.irDoc.isObject() )
  {
    if ( !readWorkflowIr( request.irDoc, ir, error ) )
      return result;
    result.stages.push_back( stageReport( "parse", "ok", "Read agent-authored WorkflowIR",
                                          workflowIrToJson( ir ) ) );
  }
  else
  {
    HarnessError recipeError;
    const Json::Value planDoc =
      RecipeCatalog::instance().instantiateRecipe( request.recipeId, request.recipeBindings,
                                                   recipeError );
    if ( planDoc.isNull() )
    {
      result.stages.push_back(
        stageReport( "parse", "fail", recipeError.summary, recipeError.details ) );
      error = recipeError;
      return result;
    }
    if ( !planToWorkflowIr( planDoc, ir, recipeError ) )
    {
      result.stages.push_back(
        stageReport( "parse", "fail", recipeError.summary, recipeError.details ) );
      error = recipeError;
      return result;
    }
    Json::Value details( Json::objectValue );
    details["recipe_id"] = request.recipeId;
    result.stages.push_back( stageReport( "parse", "ok",
                                          "Instantiated recipe and converted its plan to IR",
                                          details ) );
  }
  if ( !request.goal.empty() )
    ir.goal = request.goal;
  if ( !request.intent.empty() )
    ir.intent = request.intent;
  // A derived ir_id is content-addressed; the overrides above changed the
  // content, so re-derive (an EXPLICIT ir_id is an identity and stays).
  if ( ir.irId.rfind( "wir-", 0 ) == 0 )
    ir.irId = deriveIrId( ir );
  if ( ir.irId.empty() )
    ir.irId = deriveIrId( ir );

  // Stage: ground (slot facts from the deterministic seam).
  IrAnalysisInput analysisInput;
  Json::Value resolvedSlotPaths( Json::objectValue );
  Json::Value missingSlots( Json::arrayValue );
  for ( const IrInputSlot &slot : ir.inputs )
  {
    const Json::Value facts =
      request.inputFacts.isObject() && request.inputFacts.isMember( slot.name )
        ? request.inputFacts[ slot.name ]
        : Json::Value();
    if ( facts.isObject() && !facts.empty() )
    {
      analysisInput.inputFacts[ slot.name ] = facts;
      const std::string path = pathOfFacts( facts );
      if ( !path.empty() )
        resolvedSlotPaths[ slot.name ] = path;
    }
    else
    {
      missingSlots.append( slot.name );
    }
  }
  analysisInput.modelContracts = request.modelContracts;
  if ( missingSlots.empty() )
  {
    result.stages.push_back( stageReport( "ground", "ok", "All input slots have facts" ) );
  }
  else
  {
    Json::Value details( Json::objectValue );
    details["missing_slots"] = missingSlots;
    result.stages.push_back( stageReport(
      "ground", "skipped", "Unresolved slots — checks degrade to warnings", details ) );
  }

  // Stage: candidates (deterministic alternative ranking).
  {
    Json::Value primaryFacts = Json::Value();
    if ( !ir.inputs.empty() )
    {
      const auto facts = analysisInput.inputFacts.find( ir.inputs.front().name );
      if ( facts != analysisInput.inputFacts.end() )
        primaryFacts = facts->second;
    }
    Json::Value alternatives( Json::arrayValue );
    if ( !ir.intent.empty() )
    {
      const Json::Value candidates = capabilityCandidates( ir.intent, primaryFacts );
      const int cap = 5;
      int index = 0;
      for ( const Json::Value &candidate : candidates.get( "candidates", Json::Value() ) )
      {
        if ( index++ >= cap )
          break;
        alternatives.append( candidate );
      }
    }
    result.alternatives = alternatives;
    if ( !ir.intent.empty() )
      result.missingFacts = missingFactsForIntent( ir.intent, primaryFacts );
    Json::Value details( Json::objectValue );
    details["alternatives"] = static_cast<Json::Int>( alternatives.size() );
    result.stages.push_back( stageReport( "candidates", "ok",
                                          "Deterministic candidate methods ranked", details ) );
  }

  // Stage: analysis (of the un-repaired IR — the honest first verdict).
  {
    const IrAnalysis first = analyzeWorkflowIr( ir, analysisInput );
    Json::Value details( Json::objectValue );
    details["verdict"] = first.verdict;
    details["errors"] = static_cast<Json::Int>( first.errors().size() );
    details["warnings"] = static_cast<Json::Int>( first.warnings().size() );
    result.stages.push_back( stageReport( "analysis", "ok", "Static analysis over typed facts",
                                          details ) );
  }

  // Stage: repair.
  if ( request.applyRepairs )
  {
    const IrCompileFixResult fixed = analyzeRepairAnalyze( ir, analysisInput );
    result.ir = fixed.ir;
    result.analysis = fixed.analysis;
    result.repairs = fixed.repairs;
    result.refusals = fixed.refusals;
    Json::Value details( Json::objectValue );
    details["inserted"] = static_cast<Json::Int>( fixed.repairs.size() );
    details["decisions"] = static_cast<Json::Int>( fixed.refusals.size() );
    details["reanalyzed_verdict"] = fixed.analysis.verdict;
    result.stages.push_back( stageReport( "repair", "ok",
                                          "Rule-table repairs applied; science-changing rules "
                                          "became decisions",
                                          details ) );
  }
  else
  {
    result.ir = ir;
    result.analysis = analyzeWorkflowIr( result.ir, analysisInput );
    result.stages.push_back( stageReport( "repair", "skipped", "apply_repairs=false" ) );
  }

  // Limitations: union of serving capability entries, bounded, deduped.
  {
    Json::Value limitations( Json::arrayValue );
    std::set<std::string> seen;
    for ( const IrNode &node : result.ir.nodes )
    {
      const Json::Value entry =
        CapabilityKnowledge::instance().entryForOperator( node.operatorId, node.params );
      if ( !entry.isMember( "limitations" ) || !entry["limitations"].isArray() )
        continue;
      for ( const Json::Value &limitation : entry["limitations"] )
      {
        if ( !limitation.isString() )
          continue;
        if ( seen.insert( limitation.asString() ).second && limitations.size() < 8 )
          limitations.append( limitation.asString() );
      }
    }
    result.limitations = limitations;
  }

  // Execution gate: a compile whose authoritative verdict is not ok hands
  // over NO executable engine JSON — the plan document remains for audit
  // (review A-6).
  result.executionBlocked = result.analysis.verdict != "ok";

  // Stage: lower (IR -> AgentPlan v2 -> engine JSON).
  {
    AgentPlan plan;
    HarnessError lowerError;
    if ( lowerIrToAgentPlan( result.ir, resolvedSlotPaths, plan, lowerError ) )
    {
      HarnessError compileError;
      const std::string workflowJson = compilePlanToWorkflowJson( plan, compileError );
      if ( workflowJson.empty() )
      {
        result.planError = compileError;
        result.stages.push_back(
          stageReport( "lower", "fail", compileError.summary, compileError.details ) );
      }
      else
      {
        // Compiler provenance rides in the plan's raw document; execute_plan
        // binds it into the run context (ContextLedger) verbatim.
        Json::Value provenance( Json::objectValue );
        provenance["ir_id"] = result.ir.irId;
        provenance["ir_fingerprint"] = workflowIrFingerprint( result.ir );
        provenance["schema_version"] = result.ir.schemaVersion;
        Json::Value repairList( Json::arrayValue );
        for ( const IrRepairRecord &record : result.repairs )
          repairList.append( record.toJson() );
        provenance["repairs"] = repairList;
        Json::Value refusalList( Json::arrayValue );
        for ( const IrRefusal &refusal : result.refusals )
          refusalList.append( refusal.toJson() );
        provenance["refusals"] = refusalList;
        Json::Value raw( Json::objectValue );
        raw["workflow_ir"] = provenance;
        plan.raw = raw;

        result.plan = plan;
        result.workflowJson = workflowJson;
        result.stages.push_back(
          stageReport( "lower", "ok", "IR lowered to AgentPlan v2 and engine JSON" ) );
      }
    }
    else
    {
      result.planError = lowerError;
      result.stages.push_back(
        stageReport( "lower", "fail", lowerError.summary, lowerError.details ) );
    }
  }

  return result;
}

namespace {

/// Converts the ledger's bounded model-contract list into the {id: contract}
/// map the analysis consumes.
Json::Value ledgerModelContracts()
{
  Json::Value map( Json::objectValue );
  for ( const Json::Value &entry : ContextLedger::instance().modelContracts() )
  {
    if ( !entry.isObject() || !entry.isMember( "model_id" ) )
      continue;
    map[ entry["model_id"].asString() ] = entry.get( "contract", Json::Value() );
  }
  return map;
}

class CompileWorkflowTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:compile_workflow"; }
    std::string displayName() const override { return "Workflow Compiler"; }
    std::string description() const override
    {
      return "Scientific workflow compiler: validates a typed WorkflowIR "
             "(or instantiates a recipe into one) through grounding, static "
             "analysis, deterministic repair and lowering — BEFORE anything "
             "executes. Returns the compiled engine JSON, typed issues, "
             "auto-inserted repairs with evidence, science-changing decisions, "
             "candidate alternatives with why/why-not, missing facts and "
             "limitations. Read-only: it never executes the workflow.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "compiler", "workflow", "static-analysis", "repair" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value ir( Json::objectValue );
      ir["type"] = "object";
      ir["description"] = "Typed WorkflowIR document {kind: workflow_ir, "
                          "schema_version: 1.0, nodes: [...]}. Mutually "
                          "exclusive with recipe_id.";
      props["ir"] = ir;
      Json::Value recipeId( Json::objectValue );
      recipeId["type"] = "string";
      recipeId["description"] = "Instantiate this recipe into an IR before "
                                "compiling (e.g. harness.optical_ndvi).";
      props["recipe_id"] = recipeId;
      Json::Value bindings( Json::objectValue );
      bindings["type"] = "object";
      bindings["description"] = "Recipe bindings: {slots, params, output_dir, "
                                "outputs, preset}.";
      props["recipe_bindings"] = bindings;
      Json::Value facts( Json::objectValue );
      facts["type"] = "object";
      facts["description"] = "Optional precomputed facts: {slot: "
                             "dataset_understanding}. Slots without facts are "
                             "grounded live via spatial:understand.";
      props["input_facts"] = facts;
      Json::Value apply( Json::objectValue );
      apply["type"] = "boolean";
      apply["description"] = "Apply shape-preserving repairs (default true); "
                             "science-changing repairs always become decisions.";
      props["apply_repairs"] = apply;
      Json::Value required( Json::arrayValue );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["verdict"] = Json::Value( Json::stringValue );
      props["ir"] = Json::Value( Json::objectValue );
      props["analysis"] = Json::Value( Json::objectValue );
      props["plan"] = Json::Value( Json::objectValue );
      props["workflow_json"] = Json::Value( Json::stringValue );
      props["stages"] = Json::Value( Json::arrayValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isObject() )
        return SpatialToolResult::failure( "missing input object",
                                           error_codes::kInvalidParameter, "validation" );
      CompileWorkflowRequest request;
      request.irDoc = input.get( "ir", Json::Value() );
      request.recipeId = input.get( "recipe_id", "" ).asString();
      request.recipeBindings = input.get( "recipe_bindings", Json::Value( Json::objectValue ) );
      request.inputFacts = input.get( "input_facts", Json::Value( Json::objectValue ) );
      request.applyRepairs = input.get( "apply_repairs", true ).asBool();

      // Fail fast when the caller provided neither IR nor recipe.
      if ( !request.irDoc.isObject() && request.recipeId.empty() )
        return SpatialToolResult::failure(
          "Provide an 'ir' WorkflowIR document or a 'recipe_id'",
          error_codes::kInvalidParameter, "validation" );

      // Live grounding for slots the caller did not precompute: read the IR
      // (or the recipe's plan) first to learn the slot list.
      Json::Value slotsDoc( Json::arrayValue );
      if ( request.irDoc.isObject() )
      {
        WorkflowIr parsed;
        HarnessError parseError;
        if ( readWorkflowIr( request.irDoc, parsed, parseError ) )
        {
          for ( const IrInputSlot &slot : parsed.inputs )
          {
            Json::Value entry( Json::objectValue );
            entry["name"] = slot.name;
            entry["ref"] = slot.reference;
            slotsDoc.append( entry );
          }
        }
      }
      else
      {
        const Json::Value recipes = RecipeCatalog::instance().listRecipes();
        for ( const Json::Value &recipe : recipes )
        {
          if ( recipe.isObject() && recipe.get( "recipe_id", "" ).asString() == request.recipeId &&
               recipe.isMember( "slots" ) && recipe["slots"].isArray() )
          {
            for ( const Json::Value &slot : recipe["slots"] )
            {
              Json::Value entry( Json::objectValue );
              entry["name"] = slot.isString() ? slot.asString() : slot.get( "name", "" ).asString();
              slotsDoc.append( entry );
            }
            break;
          }
        }
      }
      if ( request.irDoc.isObject() || !slotsDoc.empty() )
      {
        for ( const Json::Value &slot : slotsDoc )
        {
          if ( !slot.isObject() || !slot.isMember( "name" ) || !slot.isMember( "ref" ) )
            continue;
          const std::string name = slot["name"].asString();
          if ( request.inputFacts.isMember( name ) )
            continue;
          SpatialToolPtr understand = findTool( "spatial:understand" );
          if ( !understand )
            continue;
          Json::Value understandInput( Json::objectValue );
          understandInput["asset"] = slot["ref"];
          const SpatialToolResult result = understand->execute( understandInput );
          if ( result.success && result.output.isMember( "dataset_understanding" ) )
            request.inputFacts[ name ] = result.output["dataset_understanding"];
        }
      }

      request.modelContracts = ledgerModelContracts();

      HarnessError error;
      const CompiledWorkflow compiled = compileWorkflow( request, error );
      if ( !error.code.empty() )
        return SpatialToolResult::failure( error.summary, error.code, "validation" );

      Json::Value out( Json::objectValue );
      out["verdict"] = compiled.verdict();
      out["stages"] = compiled.stagesJson();
      out["ir"] = workflowIrToJson( compiled.ir );
      out["analysis"] = compiled.analysis.toJson();
      Json::Value repairList( Json::arrayValue );
      for ( const IrRepairRecord &record : compiled.repairs )
        repairList.append( record.toJson() );
      out["repairs"] = repairList;
      Json::Value refusalList( Json::arrayValue );
      for ( const IrRefusal &refusal : compiled.refusals )
        refusalList.append( refusal.toJson() );
      out["decisions"] = refusalList;
      out["alternatives"] = compiled.alternatives;
      out["missing_facts"] = compiled.missingFacts;
      out["limitations"] = compiled.limitations;
      if ( compiled.executionBlocked )
      {
        // Blocked/fixable-with-errors science: the engine JSON is withheld —
        // the plan document stays for audit only.
        out["workflow_json"] = "";
        out["execution_blocked"] = true;
        out["plan_document"] = compiled.workflowJson.empty()
                                 ? Json::Value()
                                 : agentPlanToDocument( compiled.plan );
      }
      else if ( compiled.workflowJson.empty() )
      {
        out["plan"] = Json::Value();
        out["workflow_json"] = "";
        out["lower_error"] = compiled.planError.toJson();
      }
      else
      {
        out["plan"] = planSummary( compiled.plan );
        out["plan_document"] = agentPlanToDocument( compiled.plan );
        out["workflow_json"] = compiled.workflowJson;
      }
      out["next"] =
        compiled.executionBlocked
          ? "resolve the analysis issues (repairs/decisions below), then re-compile"
          : ( compiled.workflowJson.empty()
                ? "resolve the lower error, then re-compile"
                : "harness:execute_plan {plan} — the lowered AgentPlan v2" );
      return SpatialToolResult::ok( std::move( out ) );
    }
};

} // namespace

void registerWorkflowPlannerTools()
{
  SpatialToolRegistry::instance().registerTool( std::make_shared<CompileWorkflowTool>() );
}

} // namespace sicnu::agent::harness
