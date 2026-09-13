// src/agent/harness/workflow_ir.cpp
#include "workflow_ir.h"

#include "intent_vocabulary.h"

#include <QCryptographicHash>
#include <QString>

#include <json/writer.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace sicnu::agent::harness {

namespace {

const char *const kArtifactKinds[] = {
  artifact_facts::kKindRaster, artifact_facts::kKindVector, artifact_facts::kKindTable,
  artifact_facts::kKindModel, artifact_facts::kKindStructured,
};

const char *const kNumericDomains[] = {
  artifact_facts::kDomainSurfaceReflectance, artifact_facts::kDomainToa,
  artifact_facts::kDomainDn, artifact_facts::kDomainDb, artifact_facts::kDomainLinearPower,
  artifact_facts::kDomainIndex, artifact_facts::kDomainCategorical,
  artifact_facts::kDomainMasked, artifact_facts::kDomainUnknown,
};

/// Closed artifact-fact key set -> expected value type ("string"|"number"|
/// "object"|"array"). Unknown keys are rejected (fail-closed, like every
/// harness knowledge layer).
struct FactKeySpec
{
    const char *key;
    const char *type;
};

const FactKeySpec kFactKeys[] = {
  // IR-typed facts (this module's vocabulary).
  { "kind", "string" },
  { "numeric_domain", "string" },
  { "dtype", "string" },
  { "wavelengths_nm", "array" },
  { "temporal", "object" },
  { "calibration", "string" },
  { "polarizations", "array" },
  { "class_count", "number" },
  // Understanding-native facts (DatasetUnderstanding documents carry these;
  // keeping them in the closed set lets observed facts flow through merges
  // into checks without a second vocabulary).
  { "crs", "object" },
  { "crs_authid", "string" },
  { "size", "array" },
  { "pixel_size", "array" },
  { "extent", "array" },
  { "band_roles", "array" },
  { "band_count", "number" },
  { "bands", "array" },
  { "modality", "string" },
  { "sensor", "string" },
  { "nodata", "array" },
  { "quality_masks", "array" },
  { "radiometric_state", "string" },
  { "acquisition_time", "string" },
  { "processing_level", "string" },
  { "product_type", "string" },
  { "product_id", "string" },
  { "product_metadata", "object" },
  { "temporal_facts", "object" },
  { "feature_count", "number" },
  { "geometry_type", "string" },
};

std::string typeOfJson( const Json::Value &value )
{
  if ( value.isObject() )
    return "object";
  if ( value.isArray() )
    return "array";
  if ( value.isNumeric() )
    return "number";
  if ( value.isString() )
    return "string";
  return "other";
}

bool isIdChar( char c )
{
  return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
         c == '_' || c == '.' || c == '-';
}

std::string compactWrite( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

std::string sha25616( const std::string &bytes )
{
  const QByteArray digest = QCryptographicHash::hash(
    QByteArray::fromStdString( bytes ), QCryptographicHash::Sha256 );
  return QString::fromLatin1( digest.left( 16 ).toHex() ).toStdString();
}

std::string portNameOf( const Json::Value &port )
{
  if ( port.isObject() && port.isMember( "name" ) && port["name"].isString() )
    return port["name"].asString();
  return {};
}

std::string defaultString( const Json::Value &object, const char *key, const char *fallback )
{
  if ( object.isObject() && object.isMember( key ) && object[key].isString() &&
       !object[key].asString().empty() )
    return object[key].asString();
  return fallback;
}

} // namespace

bool isKnownNumericDomain( const std::string &domain )
{
  for ( const char *candidate : kNumericDomains )
    if ( domain == candidate )
      return true;
  return false;
}

bool isKnownArtifactKind( const std::string &kind )
{
  for ( const char *candidate : kArtifactKinds )
    if ( kind == candidate )
      return true;
  return false;
}

bool isLinearReflectiveDomain( const std::string &domain )
{
  return domain == artifact_facts::kDomainSurfaceReflectance ||
         domain == artifact_facts::kDomainToa || domain == artifact_facts::kDomainLinearPower;
}

Json::Value irLimits()
{
  Json::Value limits( Json::objectValue );
  limits["max_nodes"] = IrLimits::kMaxNodes;
  limits["max_inputs_per_node"] = IrLimits::kMaxInputsPerNode;
  limits["max_outputs_per_node"] = IrLimits::kMaxOutputsPerNode;
  limits["max_declared_outputs"] = IrLimits::kMaxDeclaredOutputs;
  limits["max_document_inputs"] = IrLimits::kMaxDocumentInputs;
  limits["max_artifacts"] = IrLimits::kMaxArtifacts;
  limits["max_id_chars"] = static_cast<Json::Int>( IrLimits::kMaxIdChars );
  limits["max_text_chars"] = static_cast<Json::Int>( IrLimits::kMaxTextChars );
  return limits;
}

Json::Value IrRepairRecord::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["rule_id"] = ruleId;
  doc["issue_code"] = issueCode;
  doc["inserted_node"] = insertedNode;
  doc["risk"] = risk;
  doc["facts_used"] = factsUsed;
  return doc;
}

Json::Value IrRefusal::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["rule_id"] = ruleId;
  doc["issue_code"] = issueCode;
  doc["why"] = why;
  doc["missing_facts"] = missingFacts;
  doc["decision_required"] = decisionRequired;
  return doc;
}

std::vector<std::string> validateArtifactFacts( const Json::Value &facts )
{
  std::vector<std::string> problems;
  if ( !facts.isObject() )
  {
    problems.push_back( "artifact facts must be an object" );
    return problems;
  }
  for ( const std::string &key : facts.getMemberNames() )
  {
    const auto *spec = static_cast<const FactKeySpec *>( nullptr );
    for ( const FactKeySpec &candidate : kFactKeys )
    {
      if ( key == candidate.key )
      {
        spec = &candidate;
        break;
      }
    }
    if ( !spec )
    {
      problems.push_back( "unknown artifact fact key: " + key );
      continue;
    }
    // CRS arrives either as an {authid, wkt} object (raster inspect) or as a
    // plain authid string (vector inspect / derived facts).
    const bool crsFlexible = key == "crs";
    if ( !crsFlexible && typeOfJson( facts[key] ) != spec->type )
    {
      problems.push_back( "artifact fact '" + key + "' must be " + spec->type );
      continue;
    }
    if ( crsFlexible && typeOfJson( facts[key] ) != "object" &&
         typeOfJson( facts[key] ) != "string" )
    {
      problems.push_back( "artifact fact 'crs' must be a string or {authid, wkt} object" );
      continue;
    }
    if ( key == std::string( "kind" ) && !isKnownArtifactKind( facts[key].asString() ) )
      problems.push_back( "unknown artifact kind: " + facts[key].asString() );
    if ( key == std::string( "numeric_domain" ) &&
         !isKnownNumericDomain( facts[key].asString() ) )
      problems.push_back( "unknown numeric domain: " + facts[key].asString() );
    if ( key == std::string( "modality" ) )
    {
      const std::string modality = facts[key].asString();
      if ( modality != artifact_facts::kModalityOptical &&
           modality != artifact_facts::kModalitySar && modality != artifact_facts::kModalityDem &&
           modality != artifact_facts::kModalityUnknown )
        problems.push_back( "unknown modality: " + modality );
    }
  }
  return problems;
}

bool readWorkflowIr( const Json::Value &doc, WorkflowIr &ir, HarnessError &error )
{
  if ( !doc.isObject() )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, "Workflow IR must be a JSON object" );
    return false;
  }
  const std::string kind = doc.get( "kind", kWorkflowIrKind ).asString();
  if ( kind != kWorkflowIrKind )
  {
    Json::Value details( Json::objectValue );
    details["found"] = kind;
    details["expected"] = kWorkflowIrKind;
    error = HarnessError::make( error_codes::kInvalidPlan, "IR envelope kind must be 'workflow_ir'",
                                details );
    return false;
  }
  const std::string version = doc.get( "schema_version", kWorkflowIrSchemaVersion ).asString();
  if ( version != kWorkflowIrSchemaVersion )
  {
    Json::Value details( Json::objectValue );
    details["found"] = version;
    details["supported"] = kWorkflowIrSchemaVersion;
    error = HarnessError::make( error_codes::kInvalidPlan, "Unsupported workflow IR schema version",
                                details );
    return false;
  }

  ir = WorkflowIr{};
  ir.schemaVersion = kWorkflowIrSchemaVersion;
  ir.raw = doc;
  ir.irId = doc.get( "ir_id", "" ).asString();
  ir.goal = doc.get( "goal", "" ).asString();
  ir.intent = doc.get( "intent", "" ).asString();
  if ( !isKnownIntent( ir.intent ) )
  {
    Json::Value details( Json::objectValue );
    details["intent"] = ir.intent;
    error = HarnessError::make( error_codes::kInvalidPlan, "Unknown intent", details );
    return false;
  }
  if ( ir.irId.size() > IrLimits::kMaxIdChars )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, "ir_id exceeds the id length bound" );
    return false;
  }
  if ( ir.goal.size() > IrLimits::kMaxTextChars )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, "goal exceeds the text length bound" );
    return false;
  }

  // Document input slots.
  if ( doc.isMember( "inputs" ) )
  {
    if ( !doc["inputs"].isArray() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, "inputs must be an array" );
      return false;
    }
    if ( static_cast<int>( doc["inputs"].size() ) > IrLimits::kMaxDocumentInputs )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, "too many document inputs" );
      return false;
    }
    std::set<std::string> slotNames;
    for ( const Json::Value &slot : doc["inputs"] )
    {
      if ( !slot.isObject() || !slot.isMember( "name" ) || !slot["name"].isString() ||
           slot["name"].asString().empty() || !slot.isMember( "ref" ) || !slot["ref"].isString() ||
           slot["ref"].asString().empty() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    "every input slot needs string 'name' and 'ref'" );
        return false;
      }
      const std::string name = slot["name"].asString();
      if ( !slotNames.insert( name ).second )
      {
        Json::Value details( Json::objectValue );
        details["slot"] = name;
        error = HarnessError::make( error_codes::kInvalidPlan, "duplicate input slot name", details );
        return false;
      }
      IrInputSlot parsed;
      parsed.name = name;
      parsed.reference = slot["ref"].asString();
      if ( slot.isMember( "artifact" ) )
      {
        const std::string artifactError =
          validateArtifactFacts( slot["artifact"] ).empty()
            ? std::string()
            : "invalid artifact facts on slot " + name;
        if ( !artifactError.empty() )
        {
          error = HarnessError::make( error_codes::kInvalidPlan, artifactError );
          return false;
        }
        parsed.artifact = slot["artifact"];
      }
      ir.inputs.push_back( std::move( parsed ) );
    }
  }

  // Nodes.
  if ( !doc.isMember( "nodes" ) || !doc["nodes"].isArray() || doc["nodes"].empty() )
  {
    error = HarnessError::makeWithAction( error_codes::kInvalidPlan, "Workflow IR has no nodes",
                                          "harness:compile_workflow", Json::Value() );
    return false;
  }
  if ( static_cast<int>( doc["nodes"].size() ) > IrLimits::kMaxNodes )
  {
    error = HarnessError::make( error_codes::kInvalidPlan, "too many nodes" );
    return false;
  }
  std::set<std::string> nodeIds;
  int nodeIndex = 0;
  for ( const Json::Value &node : doc["nodes"] )
  {
    const std::string where = "nodes[" + std::to_string( nodeIndex++ ) + "]";
    if ( !node.isObject() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, where + " must be an object" );
      return false;
    }
    IrNode parsed;
    parsed.id = node.get( "id", "" ).asString();
    if ( parsed.id.empty() || parsed.id.size() > IrLimits::kMaxIdChars )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  where + " needs a non-empty id within the length bound" );
      return false;
    }
    for ( char c : parsed.id )
    {
      if ( !isIdChar( c ) )
      {
        Json::Value details( Json::objectValue );
        details["id"] = parsed.id;
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    "node id may only contain [A-Za-z0-9_.-]", details );
        return false;
      }
    }
    if ( !nodeIds.insert( parsed.id ).second )
    {
      Json::Value details( Json::objectValue );
      details["id"] = parsed.id;
      error = HarnessError::make( error_codes::kInvalidPlan, "duplicate node id", details );
      return false;
    }
    parsed.operatorId = defaultString( node, "operator", "" );
    if ( parsed.operatorId.empty() )
      parsed.operatorId = defaultString( node, "operator_id", "" );
    if ( parsed.operatorId.empty() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  where + " (" + parsed.id + ") is missing 'operator'" );
      return false;
    }
    if ( node.isMember( "params" ) )
    {
      if ( !node["params"].isObject() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    where + " params must be an object" );
        return false;
      }
      parsed.params = node["params"];
    }

    // Wiring edges.
    if ( node.isMember( "inputs" ) )
    {
      if ( !node["inputs"].isArray() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    where + " inputs must be an array" );
        return false;
      }
      if ( static_cast<int>( node["inputs"].size() ) > IrLimits::kMaxInputsPerNode )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    where + " exceeds the per-node input bound" );
        return false;
      }
      for ( const Json::Value &edge : node["inputs"] )
      {
        if ( !edge.isObject() )
        {
          error = HarnessError::make( error_codes::kInvalidPlan,
                                      where + " input edges must be objects" );
          return false;
        }
        IrNodeInput parsedEdge;
        parsedEdge.node = edge.get( "node", "" ).asString();
        parsedEdge.output = defaultString( edge, "output", "output" );
        parsedEdge.input = edge.get( "input", "" ).asString();
        parsedEdge.as = defaultString( edge, "as", "input" );
        const bool nodeForm = !parsedEdge.node.empty();
        const bool slotForm = !parsedEdge.input.empty();
        if ( nodeForm == slotForm )
        {
          error = HarnessError::make(
            error_codes::kInvalidPlan,
            where + " input edges need exactly one of 'node' (upstream) or 'input' (slot)" );
          return false;
        }
        parsed.inputs.push_back( std::move( parsedEdge ) );
      }
    }

    // Typed output ports.
    if ( node.isMember( "outputs" ) )
    {
      if ( !node["outputs"].isArray() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    where + " outputs must be an array" );
        return false;
      }
      if ( static_cast<int>( node["outputs"].size() ) > IrLimits::kMaxOutputsPerNode )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    where + " exceeds the per-node output bound" );
        return false;
      }
      std::set<std::string> portNames;
      for ( const Json::Value &port : node["outputs"] )
      {
        IrPort parsedPort;
        parsedPort.name = portNameOf( port );
        if ( parsedPort.name.empty() )
        {
          error = HarnessError::make( error_codes::kInvalidPlan,
                                      where + " output ports need string 'name'" );
          return false;
        }
        if ( !portNames.insert( parsedPort.name ).second )
        {
          error = HarnessError::make( error_codes::kInvalidPlan,
                                      where + " duplicate output port " + parsedPort.name );
          return false;
        }
        if ( port.isMember( "artifact" ) && port["artifact"].isObject() )
        {
          const auto problems = validateArtifactFacts( port["artifact"] );
          if ( !problems.empty() )
          {
            Json::Value details( Json::objectValue );
            Json::Value list( Json::arrayValue );
            for ( const std::string &problem : problems )
              list.append( problem );
            details["problems"] = list;
            details["node"] = parsed.id;
            error = HarnessError::make( error_codes::kInvalidPlan,
                                        "invalid artifact facts on node output", details );
            return false;
          }
          parsedPort.artifact = port["artifact"];
        }
        parsed.outputs.push_back( std::move( parsedPort ) );
      }
    }

    parsed.verification = defaultString( node, "verification", "" );
    if ( !parsed.verification.empty() && parsed.verification != "raster" &&
         parsed.verification != "vector" && parsed.verification != "skip" )
    {
      error = HarnessError::make(
        error_codes::kInvalidPlan,
        where + " verification must be raster|vector|skip (got " + parsed.verification + ")" );
      return false;
    }
    if ( node.isMember( "resource_estimate_mb" ) && node["resource_estimate_mb"].isNumeric() )
      parsed.resourceEstimateMb = node["resource_estimate_mb"].asInt64();
    parsed.device = defaultString( node, "device", "" );
    if ( !parsed.device.empty() && parsed.device != "cpu" && parsed.device != "gpu" )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  where + " device must be cpu|gpu (got " + parsed.device + ")" );
      return false;
    }
    parsed.determinism = defaultString( node, "determinism", "" );
    if ( !parsed.determinism.empty() && parsed.determinism != artifact_facts::kDeterminismBitExact &&
         parsed.determinism != artifact_facts::kDeterminismTolerance &&
         parsed.determinism != artifact_facts::kDeterminismStochastic )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  where + " determinism must be bit_exact|tolerance|stochastic" );
      return false;
    }
    parsed.semanticOutput = node.get( "semantic_output", "" ).asString();
    if ( parsed.semanticOutput.size() > IrLimits::kMaxTextChars )
    {
      error = HarnessError::make( error_codes::kInvalidPlan,
                                  where + " semantic_output exceeds the text length bound" );
      return false;
    }
    parsed.source = defaultString( node, "source", "agent" );
    ir.nodes.push_back( std::move( parsed ) );
  }

  // Declared outputs.
  if ( doc.isMember( "outputs" ) )
  {
    if ( !doc["outputs"].isArray() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, "outputs must be an array" );
      return false;
    }
    if ( static_cast<int>( doc["outputs"].size() ) > IrLimits::kMaxDeclaredOutputs )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, "too many declared outputs" );
      return false;
    }
    for ( const Json::Value &output : doc["outputs"] )
    {
      if ( !output.isObject() || !output.isMember( "name" ) || !output["name"].isString() ||
           output["name"].asString().empty() || !output.isMember( "node" ) ||
           !output["node"].isString() || output["node"].asString().empty() )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    "every declared output needs string 'name' and 'node'" );
        return false;
      }
      IrOutputDecl parsed;
      parsed.name = output["name"].asString();
      parsed.node = output["node"].asString();
      parsed.port = defaultString( output, "port", "output" );
      parsed.kind = output.get( "kind", "" ).asString();
      if ( !parsed.kind.empty() && !isKnownArtifactKind( parsed.kind ) )
      {
        error = HarnessError::make( error_codes::kInvalidPlan,
                                    "declared output '" + parsed.name + "' has unknown kind" );
        return false;
      }
      parsed.path = output.get( "path", "" ).asString();
      parsed.semantic = output.get( "semantic", "" ).asString();
      ir.outputs.push_back( std::move( parsed ) );
    }
  }

  if ( doc.isMember( "expectations" ) )
  {
    if ( !doc["expectations"].isObject() )
    {
      error = HarnessError::make( error_codes::kInvalidPlan, "expectations must be an object" );
      return false;
    }
    ir.expectations = doc["expectations"];
  }

  normalizeWorkflowIr( ir );
  if ( ir.irId.empty() )
    ir.irId = deriveIrId( ir );
  return true;
}

std::vector<AgentPlanIssue> validateIrStructure( const WorkflowIr &ir )
{
  std::vector<AgentPlanIssue> issues;
  auto add = [ &issues ]( const std::string &code, const std::string &summary,
                          const std::string &nodeId, bool repairable, Json::Value details )
  {
    AgentPlanIssue issue;
    issue.stepId = nodeId;
    issue.repairable = repairable;
    if ( !details.isObject() )
      details = Json::Value( Json::objectValue );
    issue.error = HarnessError::make( code, summary, details );
    issues.push_back( std::move( issue ) );
  };

  std::set<std::string> ids;
  for ( const IrNode &node : ir.nodes )
    ids.insert( node.id );

  std::map<std::string, std::set<std::string>> portsByNode;
  for ( const IrNode &node : ir.nodes )
  {
    std::set<std::string> &ports = portsByNode[ node.id ];
    if ( node.outputs.empty() )
      ports.insert( "output" ); // implicit default port
    for ( const IrPort &port : node.outputs )
      ports.insert( port.name );
  }

  std::set<std::string> slotNames;
  for ( const IrInputSlot &slot : ir.inputs )
    slotNames.insert( slot.name );

  for ( const IrNode &node : ir.nodes )
  {
    for ( const IrNodeInput &edge : node.inputs )
    {
      if ( !edge.node.empty() )
      {
        if ( !ids.count( edge.node ) )
        {
          Json::Value details( Json::objectValue );
          details["upstream"] = edge.node;
          add( error_codes::kInvalidPlan,
               "node input references unknown upstream node: " + edge.node, node.id, true,
               details );
        }
        else if ( edge.node == node.id )
        {
          add( error_codes::kInvalidPlan, "node feeds itself: " + node.id, node.id, true,
               Json::Value() );
        }
        else
        {
          const auto ports = portsByNode.find( edge.node );
          if ( ports != portsByNode.end() && !ports->second.count( edge.output ) )
          {
            Json::Value details( Json::objectValue );
            details["upstream"] = edge.node;
            details["port"] = edge.output;
            add( error_codes::kInvalidPlan,
                 "upstream node has no output port '" + edge.output + "'", node.id, true, details );
          }
        }
      }
      else if ( !slotNames.count( edge.input ) )
      {
        Json::Value details( Json::objectValue );
        details["slot"] = edge.input;
        add( error_codes::kInvalidPlan,
             "node input references undeclared input slot: " + edge.input, node.id, true,
             details );
      }
    }
  }

  for ( const IrOutputDecl &output : ir.outputs )
  {
    if ( !ids.count( output.node ) )
    {
      Json::Value details( Json::objectValue );
      details["node"] = output.node;
      add( error_codes::kInvalidPlan,
           "declared output '" + output.name + "' references unknown node", "", true, details );
      continue;
    }
    const auto ports = portsByNode.find( output.node );
    if ( ports != portsByNode.end() && !ports->second.count( output.port ) )
    {
      Json::Value details( Json::objectValue );
      details["node"] = output.node;
      details["port"] = output.port;
      add( error_codes::kInvalidPlan,
           "declared output '" + output.name + "' references missing port", "", true, details );
    }
  }

  // Cycle detection (iterative DFS with the three-color scheme; deterministic
  // order because ir.nodes is in document order).
  enum class Mark
  {
    None,
    Open,
    Done
  };
  std::map<std::string, Mark> marks;
  std::map<std::string, std::vector<std::string>> upstream;
  for ( const IrNode &node : ir.nodes )
    for ( const IrNodeInput &edge : node.inputs )
      if ( !edge.node.empty() )
        upstream[ node.id ].push_back( edge.node );

  // A cycle is reported once, at the node where the DFS re-enters an open
  // node; the node ids along the cycle ride in details.
  std::function<void( const std::string &, std::vector<std::string> & )> visit;
  visit = [ & ]( const std::string &nodeId, std::vector<std::string> &stack )
  {
    if ( marks[ nodeId ] == Mark::Done )
      return;
    if ( marks[ nodeId ] == Mark::Open )
    {
      Json::Value details( Json::objectValue );
      Json::Value cycle( Json::arrayValue );
      const auto start = std::find( stack.begin(), stack.end(), nodeId );
      for ( auto it = start == stack.end() ? stack.begin() : start; it != stack.end(); ++it )
        cycle.append( *it );
      cycle.append( nodeId );
      details["cycle"] = cycle;
      add( error_codes::kInvalidPlan, "workflow IR contains a cycle", nodeId, false, details );
      marks[ nodeId ] = Mark::Done; // report each cycle once
      return;
    }
    marks[ nodeId ] = Mark::Open;
    stack.push_back( nodeId );
    for ( const std::string &parent : upstream[ nodeId ] )
      if ( ids.count( parent ) )
        visit( parent, stack );
    stack.pop_back();
    marks[ nodeId ] = Mark::Done;
  };
  for ( const IrNode &node : ir.nodes )
  {
    std::vector<std::string> stack;
    visit( node.id, stack );
  }

  return issues;
}

void normalizeWorkflowIr( WorkflowIr &ir )
{
  // Stable topological order: Kahn's algorithm over document-order fronts
  // (deterministic; independents keep document order).
  const size_t count = ir.nodes.size();
  std::map<std::string, size_t> indexById;
  for ( size_t i = 0; i < count; ++i )
    indexById[ ir.nodes[ i ].id ] = i;

  std::vector<size_t> inDegree( count, 0 );
  std::vector<std::vector<size_t>> downstream( count );
  std::vector<std::vector<size_t>> upstream( count );
  for ( size_t i = 0; i < count; ++i )
  {
    for ( const IrNodeInput &edge : ir.nodes[ i ].inputs )
    {
      if ( edge.node.empty() )
        continue;
      const auto parent = indexById.find( edge.node );
      if ( parent == indexById.end() || parent->second == i )
        continue; // structural issues are validateIrStructure's job
      upstream[ i ].push_back( parent->second );
      downstream[ parent->second ].push_back( i );
    }
  }
  for ( size_t i = 0; i < count; ++i )
  {
    std::sort( upstream[ i ].begin(), upstream[ i ].end() );
    upstream[ i ].erase( std::unique( upstream[ i ].begin(), upstream[ i ].end() ),
                         upstream[ i ].end() );
    inDegree[ i ] = upstream[ i ].size();
  }

  std::vector<size_t> order;
  order.reserve( count );
  // Front = indices with inDegree 0, in document order.
  std::vector<size_t> front;
  for ( size_t i = 0; i < count; ++i )
    if ( inDegree[ i ] == 0 )
      front.push_back( i );
  std::vector<bool> removed( count, false );
  while ( !front.empty() )
  {
    // Take the smallest remaining index (document order) — deterministic.
    const size_t pick =
      *std::min_element( front.begin(), front.end(),
                         []( size_t a, size_t b ) { return a < b; } );
    front.erase( std::remove( front.begin(), front.end(), pick ), front.end() );
    order.push_back( pick );
    removed[ pick ] = true;
    for ( size_t child : downstream[ pick ] )
    {
      if ( removed[ child ] )
        continue;
      // Decrement only once per distinct parent edge.
      if ( --inDegree[ child ] == 0 )
        front.push_back( child );
    }
  }
  // Cyclic remainder keeps document order (validateIrStructure reports it).
  if ( order.size() < count )
    for ( size_t i = 0; i < count; ++i )
      if ( !removed[ i ] )
        order.push_back( i );

  std::vector<IrNode> ordered;
  ordered.reserve( count );
  for ( size_t index : order )
    ordered.push_back( std::move( ir.nodes[ index ] ) );
  ir.nodes = std::move( ordered );

  // Sort each node's edges: node-form edges by (node, output, as), slot-form
  // edges by (input, as), node-form before slot-form for stability.
  for ( IrNode &node : ir.nodes )
  {
    std::sort( node.inputs.begin(), node.inputs.end(),
               []( const IrNodeInput &a, const IrNodeInput &b )
               {
                 const bool aNode = !a.node.empty();
                 const bool bNode = !b.node.empty();
                 if ( aNode != bNode )
                   return aNode; // node-form first
                 if ( aNode )
                   return std::make_tuple( a.node, a.output, a.as ) <
                          std::make_tuple( b.node, b.output, b.as );
                 return std::make_tuple( a.input, a.as ) < std::make_tuple( b.input, b.as );
               } );
  }
}

std::string workflowIrFingerprint( const WorkflowIr &ir )
{
  Json::Value content( Json::objectValue );
  content["intent"] = ir.intent;
  content["goal"] = ir.goal;
  Json::Value inputs( Json::arrayValue );
  for ( const IrInputSlot &slot : ir.inputs )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = slot.name;
    entry["ref"] = slot.reference;
    if ( slot.artifact.isObject() && !slot.artifact.empty() )
      entry["artifact"] = slot.artifact;
    inputs.append( entry );
  }
  content["inputs"] = inputs;
  Json::Value nodes( Json::arrayValue );
  for ( const IrNode &node : ir.nodes )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = node.id;
    entry["operator"] = node.operatorId;
    entry["params"] = node.params;
    Json::Value edges( Json::arrayValue );
    for ( const IrNodeInput &edge : node.inputs )
    {
      Json::Value wire( Json::objectValue );
      if ( !edge.node.empty() )
      {
        wire["node"] = edge.node;
        wire["output"] = edge.output;
      }
      else
      {
        wire["input"] = edge.input;
      }
      wire["as"] = edge.as;
      edges.append( wire );
    }
    entry["inputs"] = edges;
    if ( !node.outputs.empty() )
    {
      Json::Value ports( Json::arrayValue );
      for ( const IrPort &port : node.outputs )
      {
        Json::Value wire( Json::objectValue );
        wire["name"] = port.name;
        if ( port.artifact.isObject() && !port.artifact.empty() )
          wire["artifact"] = port.artifact;
        ports.append( wire );
      }
      entry["outputs"] = ports;
    }
    if ( !node.verification.empty() )
      entry["verification"] = node.verification;
    if ( node.resourceEstimateMb > 0 )
      entry["resource_estimate_mb"] = static_cast<Json::Int64>( node.resourceEstimateMb );
    if ( !node.determinism.empty() )
      entry["determinism"] = node.determinism;
    nodes.append( entry );
  }
  content["nodes"] = nodes;
  Json::Value outputs( Json::arrayValue );
  for ( const IrOutputDecl &output : ir.outputs )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = output.name;
    entry["node"] = output.node;
    entry["port"] = output.port;
    if ( !output.kind.empty() )
      entry["kind"] = output.kind;
    outputs.append( entry );
  }
  content["outputs"] = outputs;

  return sha25616( compactWrite( content ) );
}

Json::Value workflowIrToJson( const WorkflowIr &ir )
{
  Json::Value doc( Json::objectValue );
  doc["kind"] = kWorkflowIrKind;
  doc["schema_version"] = ir.schemaVersion;
  doc["ir_id"] = ir.irId;
  if ( !ir.goal.empty() )
    doc["goal"] = ir.goal;
  if ( !ir.intent.empty() )
    doc["intent"] = ir.intent;
  Json::Value inputs( Json::arrayValue );
  for ( const IrInputSlot &slot : ir.inputs )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = slot.name;
    entry["ref"] = slot.reference;
    if ( slot.artifact.isObject() && !slot.artifact.empty() )
      entry["artifact"] = slot.artifact;
    inputs.append( entry );
  }
  if ( !inputs.empty() )
    doc["inputs"] = inputs;
  Json::Value nodes( Json::arrayValue );
  for ( const IrNode &node : ir.nodes )
  {
    Json::Value entry( Json::objectValue );
    entry["id"] = node.id;
    entry["operator"] = node.operatorId;
    if ( node.params.isObject() && !node.params.empty() )
      entry["params"] = node.params;
    if ( !node.inputs.empty() )
    {
      Json::Value edges( Json::arrayValue );
      for ( const IrNodeInput &edge : node.inputs )
      {
        Json::Value wire( Json::objectValue );
        if ( !edge.node.empty() )
        {
          wire["node"] = edge.node;
          if ( edge.output != "output" )
            wire["output"] = edge.output;
        }
        else
        {
          wire["input"] = edge.input;
        }
        if ( edge.as != "input" )
          wire["as"] = edge.as;
        edges.append( wire );
      }
      entry["inputs"] = edges;
    }
    if ( !node.outputs.empty() )
    {
      Json::Value ports( Json::arrayValue );
      for ( const IrPort &port : node.outputs )
      {
        Json::Value wire( Json::objectValue );
        wire["name"] = port.name;
        if ( port.artifact.isObject() && !port.artifact.empty() )
          wire["artifact"] = port.artifact;
        ports.append( wire );
      }
      entry["outputs"] = ports;
    }
    if ( !node.verification.empty() )
      entry["verification"] = node.verification;
    if ( node.resourceEstimateMb > 0 )
      entry["resource_estimate_mb"] = static_cast<Json::Int64>( node.resourceEstimateMb );
    if ( !node.device.empty() )
      entry["device"] = node.device;
    if ( !node.determinism.empty() )
      entry["determinism"] = node.determinism;
    if ( !node.semanticOutput.empty() )
      entry["semantic_output"] = node.semanticOutput;
    if ( !node.source.empty() && node.source != "agent" )
      entry["source"] = node.source;
    nodes.append( entry );
  }
  doc["nodes"] = nodes;
  if ( !ir.outputs.empty() )
  {
    Json::Value outputs( Json::arrayValue );
    for ( const IrOutputDecl &output : ir.outputs )
    {
      Json::Value entry( Json::objectValue );
      entry["name"] = output.name;
      entry["node"] = output.node;
      if ( output.port != "output" )
        entry["port"] = output.port;
      if ( !output.kind.empty() )
        entry["kind"] = output.kind;
      if ( !output.path.empty() )
        entry["path"] = output.path;
      if ( !output.semantic.empty() )
        entry["semantic"] = output.semantic;
      outputs.append( entry );
    }
    doc["outputs"] = outputs;
  }
  if ( ir.expectations.isObject() && !ir.expectations.empty() )
    doc["expectations"] = ir.expectations;
  return doc;
}

Json::Value factStatusFor( const Json::Value &declared, const Json::Value &observed )
{
  Json::Value status( Json::objectValue );
  const auto mark = [ &status ]( const Json::Value &source, const char *label )
  {
    if ( !source.isObject() )
      return;
    for ( const std::string &key : source.getMemberNames() )
      status[key] = label;
  };
  mark( declared, "declared" );
  mark( observed, "observed" ); // observed wins
  return status;
}

Json::Value mergeArtifactFacts( const Json::Value &declared, const Json::Value &observed,
                                Json::Value &factStatus, Json::Value *conflicts )
{
  Json::Value merged( Json::objectValue );
  if ( declared.isObject() )
    for ( const std::string &key : declared.getMemberNames() )
      merged[key] = declared[key];
  if ( observed.isObject() )
  {
    for ( const std::string &key : observed.getMemberNames() )
    {
      // Understanding documents carry envelope/bookkeeping keys that are not
      // artifact facts; only the closed fact keys participate in the merge.
      bool known = false;
      for ( const FactKeySpec &spec : kFactKeys )
      {
        if ( key == spec.key )
        {
          known = true;
          break;
        }
      }
      if ( !known )
        continue;
      if ( merged.isMember( key ) && compactWrite( merged[key] ) != compactWrite( observed[key] ) )
      {
        if ( conflicts )
        {
          Json::Value entry( Json::objectValue );
          entry["key"] = key;
          entry["declared"] = merged[key];
          entry["observed"] = observed[key];
          conflicts->append( entry );
        }
      }
      merged[key] = observed[key]; // observed wins, conflict recorded
    }
  }
  factStatus = factStatusFor( declared, observed );
  return merged;
}

std::string deriveIrId( const WorkflowIr &ir )
{
  return "wir-" + workflowIrFingerprint( ir );
}

std::string derivedOutputPath( const WorkflowIr &ir, const IrNode &node,
                               const std::string &outputDir )
{
  // 1) The node may already carry a concrete output path param.
  if ( node.params.isObject() && node.params.isMember( "output" ) &&
       node.params["output"].isString() && !node.params["output"].asString().empty() )
    return node.params["output"].asString();
  // 2) A declared IR output naming this node may carry one.
  for ( const IrOutputDecl &output : ir.outputs )
  {
    if ( output.node == node.id && !output.path.empty() )
      return output.path;
  }
  // 3) Deterministic derivation under the declared output dir.
  if ( outputDir.empty() )
    return {};
  const std::string extension = ".tif";
  if ( outputDir.back() == '/' )
    return outputDir + ir.irId + "_" + node.id + extension;
  return outputDir + "/" + ir.irId + "_" + node.id + extension;
}

} // namespace sicnu::agent::harness
