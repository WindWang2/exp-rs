// src/agent/harness/workflow_repair.cpp
#include "workflow_repair.h"

#include "capability_knowledge.h"
#include "capability_relations.h"
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

/// Finds a node by id in the CURRENT (possibly already mutated) document —
/// the only safe way to touch a node across insertions (review B-1a).
const IrNode *findNodeById( const WorkflowIr &ir, const std::string &nodeId )
{
  for ( const IrNode &node : ir.nodes )
    if ( node.id == nodeId )
      return &node;
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
      "DN input into a reflectance-consuming operator: a PREPARED DECISION to "
      "wire radiometric calibration to TOA reflectance — never auto-inserted; "
      "the refusal notes whether observed product metadata backs it." },
    { kRuleSarCalibrate, "CALIBRATION_MISMATCH", repair_risk::kRadiometric, "rs:sar_calibrate",
      "DN input into a calibrated-backscatter operator: a prepared decision to "
      "wire SAR calibration. Coefficients must come from observed metadata — "
      "defaults would fabricate science." },
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
      const std::string consumerId = issue.node;
      const IrNode *consumer = findNodeById( outcome.ir, consumerId );
      if ( !consumer )
        continue;
      // The offending edge must be a SLOT edge for a reproject to be
      // insertable; node-to-node CRS conflicts mean the upstream producer
      // must be repaired instead (decision). The node-level issue carries no
      // port, so the offending slot edge is re-derived deterministically:
      // the first slot edge whose CRS differs from the reference sibling.
      const IrNodeInput *edgePtr = nullptr;
      std::string offendingCrs;
      {
        std::string referenceCrsProbe;
        for ( const IrNodeInput &sibling : consumer->inputs )
        {
          const std::string authid = crsAuthidOf(
            effectiveEdgeFacts( outcome.ir, *consumer, sibling, input ) );
          if ( authid.empty() )
            continue;
          if ( referenceCrsProbe.empty() )
          {
            referenceCrsProbe = authid;
            continue;
          }
          if ( authid != referenceCrsProbe )
          {
            referenceCrsProbe = authid;
            break;
          }
        }
        for ( const IrNodeInput &candidate : consumer->inputs )
        {
          if ( candidate.input.empty() )
            continue;
          const std::string authid = crsAuthidOf(
            effectiveEdgeFacts( outcome.ir, *consumer, candidate, input ) );
          if ( !authid.empty() && !referenceCrsProbe.empty() && authid != referenceCrsProbe )
          {
            edgePtr = &candidate;
            offendingCrs = authid;
            break;
          }
        }
      }
      if ( !edgePtr )
      {
        outcome.refusals.push_back( makeRefusal(
          kRuleReproject, issue.code,
          "CRS conflict between two upstream nodes of '" + issue.node +
            "'; insert the reprojection at the producing stage (decision)" ) );
        continue;
      }
      if ( hasRepairNodeFor( outcome.ir, *consumer, edgePtr->input, kRuleReproject ) )
        continue;
      // Reference CRS: the first sibling edge with a known, different CRS.
      std::string referenceCrs;
      for ( const IrNodeInput &sibling : consumer->inputs )
      {
        if ( sibling.as == edgePtr->as )
          continue;
        const Json::Value facts = effectiveEdgeFacts( outcome.ir, *consumer, sibling, input );
        const std::string authid = crsAuthidOf( facts );
        if ( !authid.empty() &&
             authid != crsAuthidOf(
                         effectiveEdgeFacts( outcome.ir, *consumer, *edgePtr, input ) ) )
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
      const IrNodeInput edge = *edgePtr; // value copy — see review A-3
      const std::string newId =
        uniqueNodeId( outcome.ir, consumerId + "_" + edge.input + "_reprojected" );
      IrNode repairNode;
      repairNode.id = newId;
      repairNode.operatorId = "io:reproject";
      repairNode.params["targetCrs"] = referenceCrs;
      repairNode.params["output"] =
        outputDir + "/" + outcome.ir.irId + "_" + newId + ".tif";
      IrNodeInput repairEdge;
      repairEdge.input = edge.input;
      repairEdge.as = "input";
      repairNode.inputs.push_back( repairEdge );
      IrPort repairPort;
      repairPort.name = "output";
      repairPort.artifact["kind"] = artifact_facts::kKindRaster;
      repairPort.artifact["crs"] = referenceCrs;
      repairNode.outputs.push_back( repairPort );
      repairNode.source = "repair:" + std::string( kRuleReproject );
      repairNode.semanticOutput = "input slot '" + edge.input + "' reprojected to " + referenceCrs;

      // Rewire the consumer onto the repair node (keep the port binding).
      for ( IrNode &nodeIter : outcome.ir.nodes )
      {
        if ( nodeIter.id != consumerId )
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
      record.ruleId = kRuleReproject;
      record.issueCode = issue.code;
      record.insertedNode = newId;
      record.risk = repair_risk::kShapePreserving;
      record.factsUsed["target_crs"] = referenceCrs;
      record.factsUsed["slot"] = edge.input;
      record.params["targetCrs"] = referenceCrs;
      record.params["output"] =
        outputDir + "/" + outcome.ir.irId + "_" + newId + ".tif";
      outcome.repairs.push_back( std::move( record ) );
      outcome.changed = true;
      continue;
    }

    // --- GRID_MISMATCH: align the slot-side input onto the reference grid.
    if ( issue.code == error_codes::kGridMismatch && issue.repairable )
    {
      const std::string consumerId = issue.node;
      bool consumerFound = false;
      for ( const IrNode &node : outcome.ir.nodes )
        if ( node.id == consumerId )
          consumerFound = true;
      if ( !consumerFound )
        continue;
      const IrNode *consumer = findNodeById( outcome.ir, consumerId );
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
      // The edge list is copied and the consumer is re-found BY ID on every
      // use: inserting repair nodes reallocates the node vector, so no
      // pointer into it survives a push_back (review B-1a).
      const std::vector<IrNodeInput> consumerEdges = findNodeById( outcome.ir, consumerId )->inputs;
      std::string referenceSlot;
      Json::Value referenceFacts;
      for ( const IrNodeInput &sibling : consumerEdges )
      {
        if ( sibling.input.empty() )
          continue;
        const Json::Value facts = effectiveEdgeFacts(
          outcome.ir, *findNodeById( outcome.ir, consumerId ), sibling, input );
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
        const IrNode &consumerRef = *findNodeById( outcome.ir, consumerId );
        if ( hasRepairNodeFor( outcome.ir, consumerRef, edge.input, kRuleAlign ) )
          continue;
        const std::string newId =
          uniqueNodeId( outcome.ir, consumerId + "_" + edge.input + "_aligned" );
        IrNode repairNode;
        repairNode.id = newId;
        repairNode.operatorId = CapabilityRelations::instance().gridFixer(); // ADR 0098 canonical fixer
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
          if ( nodeIter.id != consumerId )
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
        record.params["output"] = outputDir + "/" + outcome.ir.irId + "_" + newId + ".tif";
        record.params["reference"] = referenceSlot;
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

  // Prepared decisions from WARNINGS: a warn-class radiometry finding (e.g.
  // DN per the spectral_index family contract) never blocks, but the caller
  // still gets the calibration decision spelled out — refused, never applied
  // silently (review B-3).
  for ( const IrIssue &issue : analysis.warnings() )
  {
    if ( issue.code != error_codes::kInvalidRadiometry )
      continue;
    const IrNode *consumer = findNodeById( outcome.ir, issue.node );
    Json::Value facts;
    if ( consumer )
    {
      if ( const IrNodeInput *edge = findEdge( *consumer, issue.port ) )
        facts = effectiveEdgeFacts( outcome.ir, *consumer, *edge, input );
    }
    const bool metadataAvailable = factsHaveProductMetadata( facts );
    IrRefusal refusal = makeRefusal(
      kRuleCalibrateToa, issue.code,
      std::string( "Radiometric repair for node '" ) + issue.node +
        "' is a prepared decision: wire rs:radiometric_calibration (unit=toa_reflectance) "
        "ahead of the consumer" +
        ( metadataAvailable
            ? " — product metadata was observed, so the calibration is fact-backed"
            : " — no product metadata observed; calibrating would fabricate coefficients" ) );
    refusal.missingFacts = Json::Value( Json::arrayValue );
    if ( !metadataAvailable )
      refusal.missingFacts.append( "product_metadata" );
    outcome.refusals.push_back( std::move( refusal ) );
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

// ---------------------------------------------------------------------------
// Repair 2.0: prepared decisions (compiler & grounding 11)
// ---------------------------------------------------------------------------

namespace {

/// Closed zh-CN one-liner per rule (decision documents must be readable by
/// the student without opening the ledger). Drift-pinned by tests.
std::string ruleWhyZh( const std::string &ruleId )
{
  if ( ruleId == kRuleReproject )
    return "自动修复：将非参考输入重投影到参考 CRS（仅改变几何形态，不改科学含义）";
  if ( ruleId == kRuleAlign )
    return "自动修复：将非参考输入重采样对齐到参考网格（仅改变几何形态）";
  if ( ruleId == kRuleCalibrateToa )
    return "待决策：辐射定标会改变像素语义（DN→TOA 反射率），需确认后手动接线";
  if ( ruleId == kRuleSarCalibrate )
    return "待决策：SAR 定标系数必须来自实测元数据，不能默认生成";
  if ( ruleId == kRuleQaMask )
    return "待决策：应用质量掩膜会改变参与计算像元集合，需明确选择";
  if ( ruleId == kRuleGapFill )
    return "待决策：时间插值填补会改变样本，需要明确同意";
  if ( ruleId == kRuleSelectDataset )
    return "待决策：缺少必需物理波段或数值域不可自动转换，需要更换数据或方法";
  return "修复规则 " + ruleId + " 需要处理";
}

} // namespace

int decisionCostRank( const std::string &ruleId )
{
  // Closed convention: how invasive the wiring is. Documented ordering
  // device only — never a wall-clock claim.
  if ( ruleId == kRuleAlign )
    return 1;
  if ( ruleId == kRuleReproject )
    return 2;
  if ( ruleId == kRuleQaMask )
    return 3;
  if ( ruleId == kRuleCalibrateToa || ruleId == kRuleSarCalibrate )
    return 4;
  if ( ruleId == kRuleGapFill )
    return 6;
  if ( ruleId == kRuleSelectDataset )
    return 7;
  return 9; // unknown rule: last
}

Json::Value IrPreparedDecision::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["rule_id"] = ruleId;
  if ( !issueCode.empty() )
    doc["issue_code"] = issueCode;
  doc["risk_class"] = riskClass;
  if ( !operatorId.empty() )
    doc["operator_id"] = operatorId;
  doc["auto_applicable"] = autoApplicable;
  doc["cost_rank"] = costRank;
  doc["evidence_rank"] = evidenceRank;
  if ( !consumerNode.empty() )
    doc["consumer_node"] = consumerNode;
  if ( !insertedNode.empty() )
    doc["inserted_node"] = insertedNode;
  if ( params.isObject() && !params.empty() )
    doc["params"] = params;
  if ( factsUsed.isObject() && !factsUsed.empty() )
    doc["facts_used"] = factsUsed;
  if ( missingFacts.isArray() && !missingFacts.empty() )
    doc["missing_facts"] = missingFacts;
  if ( !why.empty() )
    doc["why"] = why;
  if ( !whyZh.empty() )
    doc["why_zh"] = whyZh;
  return doc;
}

Json::Value IrRepairPlan::toJson() const
{
  Json::Value doc( Json::objectValue );
  Json::Value list( Json::arrayValue );
  for ( const IrPreparedDecision &decision : decisions )
    list.append( decision.toJson() );
  doc["decisions"] = list;
  return doc;
}

IrRepairPlan planPreparedDecisions( const IrRepairOutcome &outcome )
{
  IrRepairPlan plan;

  for ( const IrRepairRecord &record : outcome.repairs )
  {
    IrPreparedDecision decision;
    decision.ruleId = record.ruleId;
    decision.issueCode = record.issueCode;
    decision.riskClass = record.risk;
    for ( const IrRepairRuleSpec &spec : repairRuleTable() )
      if ( spec.ruleId == record.ruleId )
        decision.operatorId = spec.operatorId;
    decision.autoApplicable = record.risk == repair_risk::kShapePreserving;
    decision.costRank = decisionCostRank( record.ruleId );
    decision.evidenceRank = record.factsUsed.isObject()
                              ? static_cast<int>( record.factsUsed.size() )
                              : 0;
    decision.insertedNode = record.insertedNode;
    decision.params = record.params;
    decision.factsUsed = record.factsUsed;
    decision.whyZh = ruleWhyZh( record.ruleId );
    plan.decisions.push_back( std::move( decision ) );
  }

  for ( const IrRefusal &refusal : outcome.refusals )
  {
    IrPreparedDecision decision;
    decision.ruleId = refusal.ruleId;
    decision.issueCode = refusal.issueCode;
    for ( const IrRepairRuleSpec &spec : repairRuleTable() )
      if ( spec.ruleId == refusal.ruleId )
      {
        decision.riskClass = spec.riskClass;
        decision.operatorId = spec.operatorId;
      }
    decision.autoApplicable = false; // refusals are NEVER auto-inserted
    decision.costRank = decisionCostRank( refusal.ruleId );
    decision.evidenceRank = 0; // no usable facts — that is why it was refused
    decision.missingFacts = refusal.missingFacts;
    decision.why = refusal.why;
    decision.whyZh = ruleWhyZh( refusal.ruleId );
    plan.decisions.push_back( std::move( decision ) );
  }

  std::stable_sort(
    plan.decisions.begin(), plan.decisions.end(),
    []( const IrPreparedDecision &a, const IrPreparedDecision &b ) {
      auto riskOrder = []( const std::string &risk ) {
        if ( risk == repair_risk::kShapePreserving )
          return 0;
        if ( risk == repair_risk::kRadiometric )
          return 1;
        return 2;
      };
      if ( riskOrder( a.riskClass ) != riskOrder( b.riskClass ) )
        return riskOrder( a.riskClass ) < riskOrder( b.riskClass );
      if ( a.costRank != b.costRank )
        return a.costRank < b.costRank;
      if ( a.evidenceRank != b.evidenceRank )
        return a.evidenceRank > b.evidenceRank;
      if ( a.ruleId != b.ruleId )
        return a.ruleId < b.ruleId;
      return a.consumerNode < b.consumerNode;
    } );
  return plan;
}

} // namespace sicnu::agent::harness
