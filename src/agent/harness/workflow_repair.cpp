// src/agent/harness/workflow_repair.cpp
#include "workflow_repair.h"

#include "capability_knowledge.h"
#include "intent_vocabulary.h"

#include <algorithm>
#include <map>
#include <set>

namespace sicnu::agent::harness {

namespace {

const char *const kRuleReproject = "reproject_to_reference";
const char *const kRuleAlign = "align_to_reference";
const char *const kRuleCalibrateToa = "calibrate_toa_reflectance";
const char *const kRuleSarCalibrate = "sar_dn_calibration";
const char *const kRuleQaMask = "apply_qa_mask";
const char *const kRuleGapFill = "temporal_gap_fill";
const char *const kRuleSelectDataset = "select_other_dataset";

IrRefusal makeRefusal( const std::string &ruleId, const std::string &issueCode,
                       const std::string &why, std::vector<std::string> missingFacts = {} )
{
  IrRefusal refusal;
  refusal.ruleId = ruleId;
  refusal.issueCode = issueCode;
  refusal.why = why;
  Json::Value facts( Json::arrayValue );
  for ( const std::string &fact : missingFacts )
    facts.append( fact );
  refusal.missingFacts = facts;
  refusal.decisionRequired = true;
  return refusal;
}

/// Finds the input edge of `node` whose local port is `port` (first match in
/// normalized edge order — deterministic).
const IrNodeInput *findEdge( const IrNode &node, const std::string &port )
{
  for ( const IrNodeInput &edge : node.inputs )
  {
    if ( edge.as == port )
      return &edge;
  }
  return nullptr;
}

/// Deterministic unique node id: base, then base_2, base_3, ...
std::string uniqueNodeId( const WorkflowIr &ir, const std::string &base )
{
  std::set<std::string> taken;
  for ( const IrNode &node : ir.nodes )
    taken.insert( node.id );
  if ( !taken.count( base ) )
    return base;
  for ( int suffix = 2;; ++suffix )
  {
    const std::string candidate = base + "_" + std::to_string( suffix );
    if ( !taken.count( candidate ) )
      return candidate;
  }
}

/// The CRS authid of a facts document (mirrors facts::crsOf without pulling
/// the whole band_facts surface in).
std::string crsAuthidOf( const Json::Value &facts )
{
  const Json::Value &crs = facts.get( "crs", Json::Value() );
  if ( crs.isString() )
    return crs.asString();
  if ( crs.isObject() )
    return crs.get( "authid", "" ).asString();
  return facts.get( "crs_authid", "" ).asString();
}

bool factsHaveProductMetadata( const Json::Value &facts )
{
  return facts.isMember( "product_metadata" ) ||
         facts.isMember( "processing_level" ) ||
         ( facts.isMember( "sensor" ) && facts["sensor"].isString() &&
           !facts["sensor"].asString().empty() );
}

/// True when the repaired wire (slot -> repair node -> consumer) would shadow
/// an existing identical repair — keeps the pass idempotent under re-runs.
bool hasRepairNodeFor( const WorkflowIr &ir, const IrNode &consumer,
                       const std::string &slotName, const std::string &ruleId )
{
  for ( const IrNodeInput &edge : consumer.inputs )
  {
    if ( edge.as != slotName || edge.node.empty() )
      continue;
    for ( const IrNode &node : ir.nodes )
    {
      if ( node.id == edge.node && node.source == "repair:" + ruleId )
        return true;
    }
  }
  return false;
}

} // namespace

const std::vector<IrRepairRuleSpec> &repairRuleTable()
{
  static const std::vector<IrRepairRuleSpec> kTable = {
    { kRuleReproject, "CRS_MISMATCH", repair_risk::kShapePreserving, "io:reproject",
      "Raster inputs in different CRS: warp the non-reference input to the "
      "reference CRS (targetCrs = reference input's CRS)." },
    { kRuleAlign, "GRID_MISMATCH", repair_risk::kShapePreserving, "rs:align",
      "Shared-grid consumer over divergent grids: warp+resample the "
      "non-reference input onto the reference grid (ADR 0098 canonical fixer)." },
    { kRuleCalibrateToa, "INVALID_RADIOMETRY", repair_risk::kRadiometric,
      "rs:radiometric_calibration",
      "DN input into a reflectance-consuming operator: radiometric calibration "
      "to TOA reflectance. Auto-inserted only with observed product metadata." },
    { kRuleSarCalibrate, "CALIBRATION_MISMATCH", repair_risk::kRadiometric, "rs:sar_calibrate",
      "DN input into a calibrated-backscatter operator: SAR radiometric "
      "calibration. Requires calibration coefficients in observed metadata; "
      "otherwise a decision (defaults would fabricate science)." },
    { kRuleQaMask, "", repair_risk::kScienceChanging, "rs:apply_mask",
      "Opportunity rule: observed quality masks exist for an optical "
      "index/change consumer. Applying them changes pixel semantics — "
      "decision required, never auto-inserted." },
    { kRuleGapFill, "TEMPORAL_MISALIGNMENT", repair_risk::kScienceChanging,
      "rs:temporal_gap_fill",
      "Temporal coverage below contract: gap filling changes the sample. "
      "Decision required." },
    { kRuleSelectDataset, "", repair_risk::kScienceChanging, "",
      "Missing physical bands, categorical inputs into continuous kernels, or "
      "dB inputs needing conversion: no deterministic repair composes these — "
      "the dataset or method choice belongs to the caller." },
  };
  return kTable;
}

bool repairRuleKnown( const std::string &ruleId )
{
  for ( const IrRepairRuleSpec &spec : repairRuleTable() )
    if ( spec.ruleId == ruleId )
      return true;
  return false;
}

Json::Value IrRepairOutcome::repairsJson() const
{
  Json::Value list( Json::arrayValue );
  for ( const IrRepairRecord &record : repairs )
    list.append( record.toJson() );
  return list;
}

Json::Value IrRepairOutcome::refusalsJson() const
{
  Json::Value list( Json::arrayValue );
  for ( const IrRefusal &refusal : refusals )
    list.append( refusal.toJson() );
  return list;
}

IrRepairOutcome planRepairs( const WorkflowIr &ir, const IrAnalysis &analysis,
                             const IrAnalysisInput &input )
{
  IrRepairOutcome outcome;
  outcome.ir = ir;

  // Opportunity scan: quality masks available but unused (decision only).
  for ( const IrNode &node : outcome.ir.nodes )
  {
    const Json::Value entry =
      CapabilityKnowledge::instance().entryForOperator( node.operatorId, node.params );
    const std::string family = entry.get( "family", "" ).asString();
    if ( family != "spectral_index" && family != "change" )
      continue;
    for ( const IrNodeInput &edge : node.inputs )
    {
      if ( edge.as == "mask" )
        continue; // a mask is already wired
      const Json::Value facts = effectiveEdgeFacts( outcome.ir, node, edge, input );
      if ( facts.isMember( "quality_masks" ) && facts["quality_masks"].isArray() &&
           !facts["quality_masks"].empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleQaMask, "", "Observed quality masks exist for node '" + node.id +
                             "'; applying them changes which pixels the science is computed "
                             "over — wire rs:apply_mask explicitly to opt in" ) );
        break; // one refusal per node
      }
    }
  }

  // Issue-driven rules, in the analysis's deterministic issue order.
  for ( const IrIssue &issue : analysis.issues )
  {
    if ( issue.severity != "error" )
      continue;

    // --- CRS_MISMATCH: reproject the slot-side input to the reference CRS.
    if ( issue.code == error_codes::kCrsMismatch && issue.repairable )
    {
      const IrNode *consumer = nullptr;
      for ( const IrNode &node : outcome.ir.nodes )
        if ( node.id == issue.node )
          consumer = &node;
      if ( !consumer )
        continue;
      // The offending edge (issue.port == local port) must be a slot edge for
      // a reproject to be insertable; node-to-node CRS conflicts mean the
      // upstream producer must be repaired instead (decision).
      const IrNodeInput *edge = findEdge( *consumer, issue.port );
      if ( !edge || edge->input.empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleReproject, issue.code,
          "CRS conflict between two upstream nodes of '" + issue.node +
            "'; insert the reprojection at the producing stage (decision)" ) );
        continue;
      }
      if ( hasRepairNodeFor( outcome.ir, *consumer, edge->input, kRuleReproject ) )
        continue;
      // Reference CRS: the first sibling edge with a known, different CRS.
      std::string referenceCrs;
      for ( const IrNodeInput &sibling : consumer->inputs )
      {
        if ( sibling.as == edge->as )
          continue;
        const Json::Value facts = effectiveEdgeFacts( outcome.ir, *consumer, sibling, input );
        const std::string authid = crsAuthidOf( facts );
        if ( !authid.empty() && authid != crsAuthidOf(
                                    effectiveEdgeFacts( outcome.ir, *consumer, *edge, input ) ) )
        {
          referenceCrs = authid;
          break;
        }
      }
      if ( referenceCrs.empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleReproject, issue.code, "No sibling input carries a reference CRS for node '" +
                                        issue.node + "'",
          { "reference_crs" } ) );
        continue;
      }
      const std::string outputDir = outcome.ir.expectations.get( "output_dir", "" ).asString();
      if ( outputDir.empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleReproject, issue.code,
          "expectations.output_dir is not declared; derived repair artifacts have no home",
          { "output_dir" } ) );
        continue;
      }
      const std::string newId =
        uniqueNodeId( outcome.ir, consumer->id + "_" + edge->input + "_reprojected" );
      IrNode repairNode;
      repairNode.id = newId;
      repairNode.operatorId = "io:reproject";
      repairNode.params["targetCrs"] = referenceCrs;
      repairNode.params["output"] =
        outputDir + "/" + outcome.ir.irId + "_" + newId + ".tif";
      IrNodeInput repairEdge;
      repairEdge.input = edge->input;
      repairEdge.as = "input";
      repairNode.inputs.push_back( repairEdge );
      IrPort repairPort;
      repairPort.name = "output";
      repairPort.artifact["kind"] = artifact_facts::kKindRaster;
      repairPort.artifact["crs"] = referenceCrs;
      repairNode.outputs.push_back( repairPort );
      repairNode.source = "repair:" + std::string( kRuleReproject );
      repairNode.semanticOutput = "input slot '" + edge->input + "' reprojected to " + referenceCrs;

      // Rewire the consumer onto the repair node (keep the port binding).
      for ( IrNode &nodeIter : outcome.ir.nodes )
      {
        if ( nodeIter.id != consumer->id )
          continue;
        for ( IrNodeInput &wire : nodeIter.inputs )
        {
          if ( wire.as == edge->as && wire.input == edge->input )
          {
            wire.node = newId;
            wire.output = "output";
            wire.input.clear();
          }
        }
      }
      outcome.ir.nodes.push_back( std::move( repairNode ) );
      IrRepairRecord record;
      record.ruleId = kRuleReproject;
      record.issueCode = issue.code;
      record.insertedNode = newId;
      record.risk = repair_risk::kShapePreserving;
      record.factsUsed["target_crs"] = referenceCrs;
      record.factsUsed["slot"] = edge->input;
      outcome.repairs.push_back( std::move( record ) );
      outcome.changed = true;
      continue;
    }

    // --- GRID_MISMATCH: align the slot-side input onto the reference grid.
    if ( issue.code == error_codes::kGridMismatch && issue.repairable )
    {
      const IrNode *consumer = nullptr;
      for ( const IrNode &node : outcome.ir.nodes )
        if ( node.id == issue.node )
          consumer = &node;
      if ( !consumer )
        continue;
      const std::string outputDir = outcome.ir.expectations.get( "output_dir", "" ).asString();
      if ( outputDir.empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleAlign, issue.code,
          "expectations.output_dir is not declared; derived repair artifacts have no home",
          { "output_dir" } ) );
        continue;
      }
      // Align every SLOT-side raster edge onto the first slot-side edge whose
      // facts carry a grid (deterministic reference = normalized edge order).
      // The edge list is copied before mutation: inserting repair nodes
      // reallocates the node vector, so nothing may read `consumer` after a
      // push_back.
      const std::vector<IrNodeInput> consumerEdges = consumer->inputs;
      std::string referenceSlot;
      Json::Value referenceFacts;
      for ( const IrNodeInput &sibling : consumerEdges )
      {
        if ( sibling.input.empty() )
          continue;
        const Json::Value facts = effectiveEdgeFacts( outcome.ir, *consumer, sibling, input );
        if ( facts.isMember( "size" ) && facts.isMember( "pixel_size" ) )
        {
          referenceSlot = sibling.input;
          referenceFacts = facts;
          break;
        }
      }
      if ( referenceSlot.empty() )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleAlign, issue.code,
          "No slot input with observed grid facts can serve as the align reference for node '" +
            issue.node + "'",
          { "reference_grid" } ) );
        continue;
      }
      for ( const IrNodeInput &edge : consumerEdges )
      {
        if ( edge.input.empty() || edge.input == referenceSlot )
          continue;
        if ( hasRepairNodeFor( outcome.ir, *consumer, edge.input, kRuleAlign ) )
          continue;
        const std::string newId =
          uniqueNodeId( outcome.ir, consumer->id + "_" + edge.input + "_aligned" );
        IrNode repairNode;
        repairNode.id = newId;
        repairNode.operatorId = "rs:align";
        repairNode.params["output"] = outputDir + "/" + outcome.ir.irId + "_" + newId + ".tif";
        IrNodeInput inputEdge;
        inputEdge.input = edge.input;
        inputEdge.as = "input";
        repairNode.inputs.push_back( inputEdge );
        IrNodeInput referenceEdge;
        referenceEdge.input = referenceSlot;
        referenceEdge.as = "reference";
        repairNode.inputs.push_back( referenceEdge );
        IrPort repairPort;
        repairPort.name = "output";
        repairPort.artifact["kind"] = artifact_facts::kKindRaster;
        repairPort.artifact["size"] = referenceFacts["size"];
        repairPort.artifact["pixel_size"] = referenceFacts["pixel_size"];
        if ( referenceFacts.isMember( "crs" ) )
          repairPort.artifact["crs"] = referenceFacts["crs"];
        repairNode.outputs.push_back( repairPort );
        repairNode.source = "repair:" + std::string( kRuleAlign );
        repairNode.semanticOutput = "input slot '" + edge.input +
                                    "' warped onto the reference grid of '" + referenceSlot + "'";
        for ( IrNode &nodeIter : outcome.ir.nodes )
        {
          if ( nodeIter.id != consumer->id )
            continue;
          for ( IrNodeInput &wire : nodeIter.inputs )
          {
            if ( wire.as == edge.as && wire.input == edge.input )
            {
              wire.node = newId;
              wire.output = "output";
              wire.input.clear();
            }
          }
        }
        outcome.ir.nodes.push_back( std::move( repairNode ) );
        IrRepairRecord record;
        record.ruleId = kRuleAlign;
        record.issueCode = issue.code;
        record.insertedNode = newId;
        record.risk = repair_risk::kShapePreserving;
        record.factsUsed["reference_slot"] = referenceSlot;
        record.factsUsed["aligned_slot"] = edge.input;
        outcome.repairs.push_back( std::move( record ) );
        outcome.changed = true;
      }
      continue;
    }

    // --- INVALID_RADIOMETRY (DN / degraded radiometry): prepared decision.
    // The knowledge contracts grade DN as warn-class (degraded, not invalid)
    // for optical kernels, so no auto-insertion path exists: calibration
    // changes pixel semantics and belongs to the caller. The refusal carries
    // the exact wiring decision so Pi/human can act in one step.
    if ( issue.code == error_codes::kInvalidRadiometry )
    {
      const IrNode *consumer = nullptr;
      for ( const IrNode &node : outcome.ir.nodes )
        if ( node.id == issue.node )
          consumer = &node;
      if ( !consumer )
        continue;
      const IrNodeInput *edge = findEdge( *consumer, issue.port );
      Json::Value facts;
      if ( edge )
        facts = effectiveEdgeFacts( outcome.ir, *consumer, *edge, input );
      const bool metadataAvailable = factsHaveProductMetadata( facts );
      IrRefusal refusal = makeRefusal(
        kRuleCalibrateToa, issue.code,
        std::string( "Radiometric repair for node '" ) + issue.node +
          "' is a prepared decision: wire rs:radiometric_calibration (unit=toa_reflectance) "
          "ahead of the consumer" +
          ( metadataAvailable ? " — product metadata was observed, so the calibration is "
                                "fact-backed"
                              : " — no product metadata observed; calibrating would fabricate "
                                "coefficients" ) );
      refusal.missingFacts = Json::Value( Json::arrayValue );
      if ( !metadataAvailable )
        refusal.missingFacts.append( "product_metadata" );
      outcome.refusals.push_back( std::move( refusal ) );
      continue;
    }

    // --- CALIBRATION_MISMATCH (SAR DN): guarded by explicit coefficients.
    if ( issue.code == error_codes::kCalibrationMismatch && issue.repairable )
    {
      const IrNode *consumer = nullptr;
      for ( const IrNode &node : outcome.ir.nodes )
        if ( node.id == issue.node )
          consumer = &node;
      if ( !consumer )
        continue;
      const IrNodeInput *edge = findEdge( *consumer, issue.port );
      Json::Value facts;
      if ( edge )
        facts = effectiveEdgeFacts( outcome.ir, *consumer, *edge, input );
      const Json::Value metadata =
        facts.isMember( "product_metadata" ) && facts["product_metadata"].isObject()
          ? facts["product_metadata"]
          : Json::Value();
      const bool hasCoefficients =
        ( metadata.isObject() && ( metadata.isMember( "calibrationA" ) ||
                                   metadata.isMember( "sigma0" ) ) ) ||
        facts.isMember( "calibration_coefficients" );
      if ( !hasCoefficients )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleSarCalibrate, issue.code,
          "SAR DN calibration needs calibration coefficients and an incidence angle from "
          "observed metadata; rs:sar_calibrate defaults would fabricate the science",
          { "calibration_coefficients", "incidence_angle" } ) );
      }
      else
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleSarCalibrate, issue.code,
          "Coefficients are present in metadata for node '" + issue.node +
            "'; the SAR calibration unit choice (sigma0/gamma0) still changes the physical "
            "quantity — wire rs:sar_calibrate explicitly" ) );
      }
      continue;
    }

    // --- TEMPORAL_MISALIGNMENT: gap fill changes the sample — decision only.
    if ( issue.code == error_codes::kTemporalMisalignment )
    {
      outcome.refusals.push_back( makeRefusal(
        kRuleGapFill, issue.code,
        "Temporal coverage issue at node '" + issue.node +
          "'; gap filling or calendar alignment changes the analyzed sample — decide explicitly" ) );
      continue;
    }

    // --- Missing bands / categorical: dataset or method choice.
    if ( issue.code == error_codes::kBandRoleUnresolved ||
         issue.code == error_codes::kCategoricalMismatch )
    {
      outcome.refusals.push_back( makeRefusal(
        kRuleSelectDataset, issue.code,
        "No deterministic repair composes this: " + issue.message +
          " — choose a different dataset or method (decision)" ) );
      continue;
    }
  }

  if ( outcome.changed )
    normalizeWorkflowIr( outcome.ir );
  // Deterministic refusal order: by rule id, then issue code, then why.
  std::sort( outcome.refusals.begin(), outcome.refusals.end(),
             []( const IrRefusal &a, const IrRefusal &b )
             {
               return std::make_tuple( a.ruleId, a.issueCode, a.why ) <
                      std::make_tuple( b.ruleId, b.issueCode, b.why );
             } );
  outcome.refusals.erase(
    std::unique( outcome.refusals.begin(), outcome.refusals.end(),
                 []( const IrRefusal &a, const IrRefusal &b )
                 { return a.ruleId == b.ruleId && a.issueCode == b.issueCode && a.why == b.why; } ),
    outcome.refusals.end() );
  return outcome;
}

IrCompileFixResult analyzeRepairAnalyze( WorkflowIr ir, const IrAnalysisInput &input )
{
  IrCompileFixResult result;
  const IrAnalysis first = analyzeWorkflowIr( ir, input );
  const IrRepairOutcome outcome = planRepairs( ir, first, input );
  result.ir = outcome.ir;
  result.repairs = outcome.repairs;
  result.refusals = outcome.refusals;
  result.analysis = analyzeWorkflowIr( result.ir, input );
  return result;
}

} // namespace sicnu::agent::harness
