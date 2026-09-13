// src/agent/harness/workflow_analysis.cpp
#include "workflow_analysis.h"

#include "band_facts.h"
#include "capability_catalog.h"
#include "capability_knowledge.h"
#include "capability_relations.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/atomic_algorithm_registry.h"

#include <json/writer.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

namespace sicnu::agent::harness {

namespace {

using facts::bandFacts;
using facts::crsOf;
using facts::gridFacts;
using facts::lowered;

/// Families whose kernels are continuous arithmetic — a categorical input is
/// a contract mismatch there. Closed list, pinned by tests.
const char *const kContinuousFamilies[] = {
  "spectral_index", "change", "filter", "terrain", "spectral_transform", "fusion",
};

/// Operators of the Model Execution Seam (CONTEXT.md) — the only nodes whose
/// `model` param must be compatible with an upstream artifact.
const char *const kModelSeamOperators[] = {
  "rs:infer", "rs:segment", "rs:detect", "rs:embedding",
};

/// Authids treated as geographic (unprojected) by the requires_projected
/// check without invoking GDAL. Closed list; a WKT-only CRS degrades the
/// check to a skip.
const char *const kGeographicAuthids[] = {
  "EPSG:4326", "EPSG:4269", "EPSG:4258", "EPSG:4610",
};

bool isModelSeamOperator( const std::string &operatorId )
{
  for ( const char *candidate : kModelSeamOperators )
    if ( operatorId == candidate )
      return true;
  return false;
}

bool isContinuousFamily( const std::string &family )
{
  for ( const char *candidate : kContinuousFamilies )
    if ( family == candidate )
      return true;
  return false;
}

/// Roles with a canonical wavelength window (nm) — the same physical windows
/// band_facts.cpp uses for role inference, factored once here for the
/// declared-role-vs-wavelength contradiction check.
struct RoleWindow
{
    const char *role;
    double low;
    double high;
};

const RoleWindow kRoleWindows[] = {
  { "coastal", 400.0, 460.0 },  { "blue", 430.0, 520.0 },   { "green", 500.0, 600.0 },
  { "red", 600.0, 700.0 },      { "red_edge", 700.0, 745.0 },
  { "rededge", 700.0, 745.0 },  { "nir", 750.0, 1100.0 },
  { "swir", 1550.0, 1750.0 },   { "swir1", 1550.0, 1750.0 },
  { "swir2", 2080.0, 2350.0 },
};

Json::Value emptyObject() { return Json::Value( Json::objectValue ); }

/// Unwraps a DatasetUnderstanding document: the grounding tools return the
/// envelope {schema_version, kind, <facts...>}; both that shape and a nested
/// {"dataset_understanding": {...}} are accepted. Missing/null stays null.
Json::Value understandingBody( const Json::Value &doc )
{
  if ( !doc.isObject() )
    return Json::Value();
  if ( doc.isMember( "dataset_understanding" ) && doc["dataset_understanding"].isObject() )
    return doc["dataset_understanding"];
  return doc;
}

bool statusIsFact( const Json::Value &factStatus, const std::string &key )
{
  // observed/declared/derived are facts; assumed is a heuristic. The caller
  // decides the severity, but only real facts may ground errors.
  const std::string status = factStatus.get( key, "" ).asString();
  return status == "observed" || status == "declared" || status == "derived";
}

std::string domainStatus( const Json::Value &factStatus )
{
  return factStatus.get( "numeric_domain", "" ).asString();
}

/// Number of occurrences of `role` in the merged band_roles array.
int countRole( const Json::Value &facts, const std::string &role )
{
  int count = 0;
  if ( facts.isMember( "band_roles" ) && facts["band_roles"].isArray() )
  {
    for ( const Json::Value &entry : facts["band_roles"] )
    {
      if ( !entry.isString() )
        continue;
      if ( lowered( entry.asString() ) == role )
        ++count;
    }
  }
  return count;
}

/// Center wavelengths (nm) per band from the typed wavelengths_nm fact.
std::vector<double> wavelengthCenters( const Json::Value &facts )
{
  std::vector<double> centers;
  if ( facts.isMember( "wavelengths_nm" ) && facts["wavelengths_nm"].isArray() )
  {
    for ( const Json::Value &entry : facts["wavelengths_nm"] )
    {
      if ( entry.isObject() && entry.isMember( "center" ) && entry["center"].isNumeric() )
        centers.push_back( entry["center"].asDouble() );
      else if ( entry.isNumeric() )
        centers.push_back( entry.asDouble() );
    }
  }
  return centers;
}

/// Wavelength centers of the bands whose role string matches `role`.
std::vector<double> wavelengthCentersForRole( const Json::Value &facts, const std::string &role )
{
  std::vector<double> centers;
  const Json::Value bands = facts.isMember( "bands" ) && facts["bands"].isArray()
                              ? facts["bands"]
                              : Json::Value();
  if ( bands.isNull() )
    return centers;
  for ( const Json::Value &band : bands )
  {
    if ( !band.isObject() )
      continue;
    const std::string bandRole = lowered( band.get( "role", "" ).asString() );
    if ( bandRole != role )
      continue;
    if ( band.isMember( "wavelength" ) && band["wavelength"].isNumeric() )
    {
      double nm = band["wavelength"].asDouble();
      const std::string units = lowered( band.get( "wavelengthUnits", "nm" ).asString() );
      if ( units == "µm" || units == "um" )
        nm *= 1000.0;
      centers.push_back( nm );
    }
  }
  return centers;
}

bool roleSatisfiedByWavelength( const Json::Value &facts, const std::string &role )
{
  for ( const RoleWindow &window : kRoleWindows )
  {
    if ( role != window.role )
      continue;
    for ( double center : wavelengthCenters( facts ) )
      if ( center >= window.low && center <= window.high )
        return true;
    for ( double center : wavelengthCentersForRole( facts, role ) )
      if ( center >= window.low && center <= window.high )
        return true;
  }
  return false;
}

bool isGeographicCrs( const Json::Value &facts )
{
  const std::string authid = lowered( facts.get( "crs_authid", "" ).asString() );
  if ( authid.empty() )
  {
    const Json::Value &crs = facts.get( "crs", Json::Value() );
    if ( crs.isObject() )
    {
      const std::string wkt = crs.get( "wkt", "" ).asString();
      return wkt.find( "GEOGCS" ) != std::string::npos &&
             wkt.find( "PROJCS" ) == std::string::npos;
    }
    return false;
  }
  for ( const char *geo : kGeographicAuthids )
    if ( authid == lowered( geo ) )
      return true;
  return false;
}

/// Shape-tolerant grid facts (review A-4): the inspect tools emit
/// `size` {width,height} / `pixel_size` {x,y} OBJECTS while some declared
/// facts use [w,h]/[x,y] ARRAYS; both shapes parse. Zero dimensions mean
/// unknown — checks skip, never fake a verdict.
struct GridShapeFacts
{
    bool known = false;
    long long width = 0;
    long long height = 0;
    double pixelSizeX = 0;
    double pixelSizeY = 0;
};

double numberOr( const Json::Value &value, const char *key, Json::ArrayIndex index )
{
  if ( value.isObject() && value.isMember( key ) && value[key].isNumeric() )
    return value[key].asDouble();
  if ( value.isArray() && index < value.size() && value[index].isNumeric() )
    return value[index].asDouble();
  return 0.0;
}

GridShapeFacts gridShapeFacts( const Json::Value &facts )
{
  GridShapeFacts g;
  if ( !facts.isMember( "size" ) )
    return g;
  const Json::Value &size = facts["size"];
  g.width = static_cast<long long>( numberOr( size, "width", 0 ) );
  g.height = static_cast<long long>( numberOr( size, "height", 1 ) );
  g.pixelSizeX = numberOr( facts.get( "pixel_size", Json::Value() ), "x", 0 );
  g.pixelSizeY = numberOr( facts.get( "pixel_size", Json::Value() ), "y", 1 );
  g.known = g.width > 0 && g.height > 0;
  return g;
}

enum class GridCompare
{
    Equal,
    Conflict,
    Unknown
};

GridCompare compareGrids( const Json::Value &a, const Json::Value &b )
{
  const GridShapeFacts ga = gridShapeFacts( a );
  const GridShapeFacts gb = gridShapeFacts( b );
  if ( !ga.known || !gb.known )
    return GridCompare::Unknown; // unknown facts never fake a pass
  if ( ga.width != gb.width || ga.height != gb.height )
    return GridCompare::Conflict;
  if ( ga.pixelSizeX > 0 && gb.pixelSizeX > 0 &&
       std::fabs( ga.pixelSizeX - gb.pixelSizeX ) > 1e-9 )
    return GridCompare::Conflict;
  if ( ga.pixelSizeY > 0 && gb.pixelSizeY > 0 &&
       std::fabs( ga.pixelSizeY - gb.pixelSizeY ) > 1e-9 )
    return GridCompare::Conflict;
  return GridCompare::Equal;
}

std::string gridSummary( const Json::Value &facts )
{
  const GridShapeFacts g = gridShapeFacts( facts );
  return std::to_string( g.width ) + "x" + std::to_string( g.height ) + " @" +
         std::to_string( g.pixelSizeX );
}

struct NodeFactEnvironment
{
    /// slot name -> merged facts (declared slot artifact + observed understanding)
    std::map<std::string, Json::Value> slotFacts;
    std::map<std::string, Json::Value> slotStatus;
    /// node id -> port -> merged effective output facts
    std::map<std::string, std::map<std::string, Json::Value>> nodeOutputs;
    std::map<std::string, std::map<std::string, Json::Value>> nodeOutputStatus;
};

/// Keys that pass through an operator unchanged when the operator's contract
/// does not declare them. Radiometry (numeric_domain) passes as "assumed".
const char *const kInheritedKeys[] = {
  "crs",       "crs_authid", "size",     "pixel_size",     "extent",
  "band_roles", "band_count", "bands",   "wavelengths_nm", "modality",
  "sensor",    "dtype",      "nodata",   "quality_masks",  "polarizations",
  "feature_count", "geometry_type",
};

void mergeTier( Json::Value &merged, Json::Value &status, const Json::Value &tier,
                const char *label, const bool inheritNumericAsAssumed = false,
                const Json::Value *inheritedSource = nullptr )
{
  if ( !tier.isObject() )
    return;
  for ( const std::string &key : tier.getMemberNames() )
  {
    merged[key] = tier[key];
    status[key] = label;
  }
  if ( inheritNumericAsAssumed && inheritedSource && inheritedSource->isMember( "numeric_domain" ) &&
       !merged.isMember( "numeric_domain" ) )
  {
    merged["numeric_domain"] = ( *inheritedSource )["numeric_domain"];
    status["numeric_domain"] = "assumed";
  }
}

} // namespace

std::string deriveNumericDomain( const Json::Value &facts )
{
  const std::string explicitDomain = facts.get( "numeric_domain", "" ).asString();
  if ( !explicitDomain.empty() && explicitDomain != artifact_facts::kDomainUnknown )
    return explicitDomain;
  const std::string radiometric = lowered( facts.get( "radiometric_state", "" ).asString() );
  if ( radiometric.find( "surface_reflectance" ) != std::string::npos )
    return artifact_facts::kDomainSurfaceReflectance;
  if ( radiometric.find( "toa" ) != std::string::npos )
    return artifact_facts::kDomainToa;
  if ( radiometric == "dn" || radiometric.find( "digital_number" ) != std::string::npos )
    return artifact_facts::kDomainDn;
  const std::string calibration = lowered( facts.get( "calibration", "" ).asString() );
  if ( calibration == "sigma0" || calibration == "gamma0" || calibration == "beta0" )
    return artifact_facts::kDomainLinearPower;
  if ( calibration == "db" )
    return artifact_facts::kDomainDb;
  return {};
}

Json::Value wavelengthWindowForRole( const std::string &role )
{
  for ( const RoleWindow &window : kRoleWindows )
  {
    if ( role == window.role )
    {
      Json::Value range( Json::arrayValue );
      range.append( window.low );
      range.append( window.high );
      return range;
    }
  }
  return Json::Value( Json::arrayValue );
}

Json::Value IrIssue::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["code"] = code;
  doc["severity"] = severity;
  if ( !node.empty() )
    doc["node"] = node;
  if ( !port.empty() )
    doc["port"] = port;
  doc["message"] = message;
  doc["repairable"] = repairable;
  doc["details"] = details;
  return doc;
}

Json::Value IrAnalysis::toJson() const
{
  Json::Value doc( Json::objectValue );
  doc["verdict"] = verdict;
  doc["ir_fingerprint"] = irFingerprint;
  Json::Value issueList( Json::arrayValue );
  for ( const IrIssue &issue : issues )
    issueList.append( issue.toJson() );
  doc["issues"] = issueList;
  doc["checks"] = checks;
  doc["facts"] = facts;
  doc["fact_status"] = factStatus;
  return doc;
}

bool IrAnalysis::blocked() const { return verdict == "blocked"; }

std::vector<IrIssue> IrAnalysis::errors() const
{
  std::vector<IrIssue> out;
  for ( const IrIssue &issue : issues )
    if ( issue.severity == "error" )
      out.push_back( issue );
  return out;
}

std::vector<IrIssue> IrAnalysis::warnings() const
{
  std::vector<IrIssue> out;
  for ( const IrIssue &issue : issues )
    if ( issue.severity == "warning" )
      out.push_back( issue );
  return out;
}

namespace {

class AnalysisBuilder
{
  public:
    AnalysisBuilder( Json::Value checks ) : mChecks( std::move( checks ) ) {}

    void fail( const char *check, const std::string &code, const std::string &node,
               const std::string &port, const std::string &message, bool repairable,
               Json::Value details = Json::Value() )
    {
      IrIssue issue;
      issue.code = code;
      issue.severity = "error";
      issue.node = node;
      issue.port = port;
      issue.message = message;
      issue.repairable = repairable;
      issue.details = details.isObject() ? details : emptyObject();
      mIssues.push_back( std::move( issue ) );
      record( check, "fail", code, details );
    }

    void warn( const char *check, const std::string &code, const std::string &node,
               const std::string &port, const std::string &message, Json::Value details = Json::Value() )
    {
      IrIssue issue;
      issue.code = code;
      issue.severity = "warning";
      issue.node = node;
      issue.port = port;
      issue.message = message;
      issue.repairable = false;
      issue.details = details.isObject() ? details : emptyObject();
      mIssues.push_back( std::move( issue ) );
      record( check, "warn", code, details );
    }

    void pass( const char *check, Json::Value details = Json::Value() )
    {
      record( check, "pass", "", details );
    }

    /// Ledger-only advisory: records a warn-status row WITHOUT emitting an
    /// issue (for coverage notes that are not science findings).
    void note( const char *check, const std::string &why )
    {
      Json::Value details = emptyObject();
      details["why"] = why;
      record( check, "warn", "", details );
    }

    void skip( const char *check, const std::string &why, Json::Value details = Json::Value() )
    {
      if ( !details.isObject() )
        details = emptyObject();
      details["why"] = why;
      record( check, "skip", "", details );
    }

    Json::Value finish( const std::string &verdict, const std::string &fingerprint,
                        const Json::Value &facts, const Json::Value &factStatus )
    {
      IrAnalysis analysis;
      analysis.verdict = verdict;
      analysis.irFingerprint = fingerprint;
      analysis.facts = facts;
      analysis.factStatus = factStatus;
      std::sort( mIssues.begin(), mIssues.end(),
                 []( const IrIssue &a, const IrIssue &b )
                 {
                   return std::make_tuple( a.code, a.node, a.port, a.message ) <
                          std::make_tuple( b.code, b.node, b.port, b.message );
                 } );
      analysis.issues = std::move( mIssues );
      analysis.checks = mChecks;
      return analysis.toJson();
    }

    std::vector<IrIssue> takeIssues() { return std::move( mIssues ); }
    Json::Value checksJson() const { return mChecks; }

  private:
    void record( const char *check, const char *status, const std::string &code,
                 const Json::Value &details )
    {
      // One ledger row per check name: first record wins the status, later
      // rows fold into a counted summary so per-edge checks stay one row.
      for ( Json::Value &row : mChecks )
      {
        if ( row.isObject() && row["check"].asString() == check )
        {
          if ( std::string( status ) == "fail" )
            row["status"] = "fail";
          else if ( std::string( status ) == "warn" && row["status"].asString() == "pass" )
            row["status"] = "warn";
          row["count"] = row.get( "count", 0 ).asInt() + 1;
          if ( !code.empty() && !row.isMember( "code" ) )
            row["code"] = code;
          return;
        }
      }
      Json::Value row( Json::objectValue );
      row["check"] = check;
      row["status"] = status;
      row["count"] = 1;
      if ( !code.empty() )
        row["code"] = code;
      if ( details.isObject() && !details.empty() )
        row["details"] = details;
      mChecks.append( row );
    }

    std::vector<IrIssue> mIssues;
    Json::Value mChecks;
};

NodeFactEnvironment buildFactEnvironment( const WorkflowIr &ir, const IrAnalysisInput &input,
                                          Json::Value &conflictsOut )
{
  NodeFactEnvironment env;
  for ( const IrInputSlot &slot : ir.inputs )
  {
    const Json::Value observed = understandingBody(
      input.inputFacts.count( slot.name ) ? input.inputFacts.at( slot.name ) : Json::Value() );
    Json::Value status;
    Json::Value conflicts;
    Json::Value merged = mergeArtifactFacts( slot.artifact, observed, status, &conflicts );
    for ( const Json::Value &conflict : conflicts )
    {
      Json::Value entry = conflict;
      entry["slot"] = slot.name;
      conflictsOut.append( entry );
    }
    env.slotFacts[ slot.name ] = merged;
    env.slotStatus[ slot.name ] = status;
  }

  // Capability knowledge + catalog singletons (loaded lazily by the layers).
  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  for ( const IrNode &node : ir.nodes )
  {
    const Json::Value entry = knowledge.entryForOperator( node.operatorId, node.params );

    // Effective input facts for the node's FIRST edge carry the inheritance
    // chain (primary input). Node outputs inherit per kInheritedKeys.
    Json::Value primaryFacts = emptyObject();
    if ( !node.inputs.empty() )
    {
      const IrNodeInput &edge = node.inputs.front();
      if ( !edge.node.empty() )
      {
        const auto upstream = env.nodeOutputs.find( edge.node );
        if ( upstream != env.nodeOutputs.end() )
        {
          const auto port = upstream->second.find( edge.output );
          if ( port != upstream->second.end() )
            primaryFacts = port->second;
        }
      }
      else
      {
        const auto slot = env.slotFacts.find( edge.input );
        if ( slot != env.slotFacts.end() )
          primaryFacts = slot->second;
      }
    }

    Json::Value portFacts = emptyObject();
    Json::Value portStatus = emptyObject();
    // Tier 1: inheritance (assumed / observed pass-through).
    if ( primaryFacts.isObject() )
    {
      for ( const char *key : kInheritedKeys )
      {
        if ( primaryFacts.isMember( key ) )
        {
          portFacts[key] = primaryFacts[key];
          portStatus[key] = "inherited";
        }
      }
      if ( primaryFacts.isMember( "numeric_domain" ) )
      {
        portFacts["numeric_domain"] = primaryFacts["numeric_domain"];
        portStatus["numeric_domain"] = "assumed";
      }
    }
    // Tier 2: capability output contract (derived).
    if ( entry.isMember( "artifacts" ) && entry["artifacts"].isObject() )
    {
      for ( const IrPort &port : node.outputs )
      {
        if ( entry["artifacts"].isMember( port.name ) &&
             entry["artifacts"][ port.name ].isObject() )
        {
          const Json::Value &contract = entry["artifacts"][ port.name ];
          for ( const std::string &key : contract.getMemberNames() )
          {
            portFacts[key] = contract[key];
            portStatus[key] = "derived";
          }
        }
      }
      if ( entry["artifacts"].isMember( "output" ) && node.outputs.empty() &&
           entry["artifacts"]["output"].isObject() )
      {
        const Json::Value &contract = entry["artifacts"]["output"];
        for ( const std::string &key : contract.getMemberNames() )
        {
          portFacts[key] = contract[key];
          portStatus[key] = "derived";
        }
      }
    }
    // Tier 3: declared port artifacts.
    for ( const IrPort &port : node.outputs )
    {
      if ( port.artifact.isObject() )
        mergeTier( portFacts, portStatus, port.artifact, "declared" );
    }
    env.nodeOutputs[ node.id ][ "output" ] = portFacts;
    env.nodeOutputStatus[ node.id ][ "output" ] = portStatus;
    for ( const IrPort &port : node.outputs )
    {
      env.nodeOutputs[ node.id ][ port.name ] = portFacts;
      env.nodeOutputStatus[ node.id ][ port.name ] = portStatus;
    }
  }
  return env;
}

} // namespace

Json::Value effectiveEdgeFacts( const WorkflowIr &ir, const IrNode &node,
                                const IrNodeInput &edge, const IrAnalysisInput &input )
{
  Json::Value conflicts;
  const NodeFactEnvironment env = buildFactEnvironment( ir, input, conflicts );
  if ( !edge.node.empty() )
  {
    const auto upstream = env.nodeOutputs.find( edge.node );
    if ( upstream == env.nodeOutputs.end() )
      return Json::Value();
    const auto port = upstream->second.find( edge.output );
    if ( port == upstream->second.end() )
      return Json::Value();
    return port->second;
  }
  const auto slot = env.slotFacts.find( edge.input );
  if ( slot == env.slotFacts.end() )
    return Json::Value();
  return slot->second;
}

IrAnalysis analyzeWorkflowIr( WorkflowIr &ir, const IrAnalysisInput &input )
{
  normalizeWorkflowIr( ir );
  const std::string fingerprint = workflowIrFingerprint( ir );

  AnalysisBuilder builder( Json::Value( Json::arrayValue ) );
  Json::Value factsEcho( Json::objectValue );
  Json::Value statusEcho( Json::objectValue );
  Json::Value conflicts( Json::arrayValue );

  const NodeFactEnvironment env = buildFactEnvironment( ir, input, conflicts );
  for ( const auto &slot : env.slotFacts )
  {
    factsEcho[ slot.first ] = slot.second;
    statusEcho[ slot.first ] = env.slotStatus.at( slot.first );
  }

  // c0: structural issues (wiring, cycles, ports) — typed, non-repairable
  // here because the author must edit the document.
  for ( const AgentPlanIssue &structural : validateIrStructure( ir ) )
  {
    Json::Value details = structural.error.details;
    builder.fail( "structure", structural.error.code, structural.stepId, "",
                  structural.error.summary, false, details );
  }

  // Document-level checks: resource budget, output path collisions.
  // (resource)
  long long totalEstimateMb = 0;
  for ( const IrNode &node : ir.nodes )
    totalEstimateMb += node.resourceEstimateMb;
  const long long maxRamMb = ir.expectations.get( "max_ram_mb", 0 ).asInt64();
  if ( maxRamMb > 0 && totalEstimateMb > 0 && totalEstimateMb > maxRamMb )
  {
    Json::Value details = emptyObject();
    details["estimated_mb"] = static_cast<Json::Int64>( totalEstimateMb );
    details["budget_mb"] = static_cast<Json::Int64>( maxRamMb );
    builder.fail( "resource_over_budget", error_codes::kResourceOverBudget, "", "",
                  "Declared per-node resource estimates exceed the plan budget", false, details );
  }
  else if ( maxRamMb <= 0 )
  {
    builder.skip( "resource_over_budget", "no max_ram_mb declared in expectations" );
  }
  else
  {
    builder.pass( "resource_over_budget" );
  }

  // (output path collision)
  {
    std::map<std::string, std::string> pathOwner;
    bool any = false;
    for ( const IrNode &node : ir.nodes )
    {
      if ( node.params.isObject() && node.params.isMember( "output" ) &&
           node.params["output"].isString() && !node.params["output"].asString().empty() )
      {
        any = true;
        const std::string path = node.params["output"].asString();
        const auto existing = pathOwner.find( path );
        if ( existing == pathOwner.end() )
          pathOwner[ path ] = node.id;
        else if ( existing->second != node.id )
        {
          Json::Value details = emptyObject();
          details["path"] = path;
          details["first_node"] = existing->second;
          builder.fail( "output_path_collision", error_codes::kOutputPathCollision, node.id, "",
                        "Output path already produced by node " + existing->second, false,
                        details );
        }
      }
    }
    for ( const IrOutputDecl &output : ir.outputs )
    {
      if ( output.path.empty() )
        continue;
      any = true;
      const auto existing = pathOwner.find( output.path );
      if ( existing == pathOwner.end() )
        pathOwner[ output.path ] = "output:" + output.name;
      else if ( existing->second != "output:" + output.name )
      {
        Json::Value details = emptyObject();
        details["path"] = output.path;
        details["first_owner"] = existing->second;
        builder.fail( "output_path_collision", error_codes::kOutputPathCollision, output.node, "",
                      "Declared output path collides with " + existing->second, false, details );
      }
    }
    if ( !any )
      builder.skip( "output_path_collision", "no explicit output paths declared" );
    else
      builder.pass( "output_path_collision" );
  }

  CapabilityKnowledge &knowledge = CapabilityKnowledge::instance();
  CapabilityCatalog &catalog = CapabilityCatalog::instance();
  CapabilityRelations &relations = CapabilityRelations::instance();

  for ( const IrNode &node : ir.nodes )
  {
    const Json::Value entry = knowledge.entryForOperator( node.operatorId, node.params );
    const Json::Value capability = catalog.capability( node.operatorId );

    // c1: operator existence.
    const bool exists =
      sicnu::operators::RSOperatorRegistry::instance().create( node.operatorId ) ||
      sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter( node.operatorId ) !=
        nullptr;
    if ( !exists )
    {
      Json::Value details = emptyObject();
      details["operator"] = node.operatorId;
      // Not repairable: no rule inserts operators — the author fixes the id.
      builder.fail( "known_operator", error_codes::kInvalidPlan, node.id, "",
                    "Unknown operator id: " + node.operatorId, false, details );
      // No contract facts exist for an unknown operator; the remaining
      // per-node checks would all be noise. Record skips and continue.
      builder.skip( "required_params", "unknown operator" );
      builder.skip( "required_input_ports", "unknown operator" );
      builder.skip( "modality", "unknown operator" );
      builder.skip( "band_roles", "unknown operator" );
      continue;
    }
    builder.pass( "known_operator" );
    if ( !capability.isObject() || capability.empty() )
    {
      // Coverage advisory only: no contract facts exist here, so every
      // knowledge check below degrades to skip. This is NOT a science
      // finding — it must not become an issue (the registry-wide coverage
      // floor lives in test_capability_drift).
      builder.note( "known_operator", "no capability knowledge entry for this operator; "
                                      "contract checks degrade to skip" );
    }

    // c2: required params (capability io.parameters).
    bool checkedParams = false;
    if ( capability.isMember( "io" ) && capability["io"].isObject() &&
         capability["io"].isMember( "parameters" ) && capability["io"]["parameters"].isArray() )
    {
      checkedParams = true;
      for ( const Json::Value &param : capability["io"]["parameters"] )
      {
        if ( !param.isObject() || !param.isMember( "name" ) )
          continue;
        const bool required = param.get( "required", false ).asBool();
        if ( !required )
          continue;
        const std::string name = param["name"].asString();
        // The compiler derives deterministic output paths under
        // expectations.output_dir at lowering; a declared output_dir makes
        // the required 'output' param satisfied-by-derivation.
        const bool derivableOutput =
          name == "output" && ir.expectations.get( "output_dir", "" ).asString().empty() == false;
        if ( !derivableOutput &&
             ( !node.params.isMember( name ) || node.params[name].isNull() ||
               ( node.params[name].isString() && node.params[name].asString().empty() ) ) )
        {
          Json::Value details = emptyObject();
          details["parameter"] = name;
          builder.fail( "required_params", error_codes::kInvalidParameter, node.id, "",
                        "Missing required parameter '" + name + "' for " + node.operatorId, true,
                        details );
        }
      }
      builder.pass( "required_params" );
    }
    if ( !checkedParams )
      builder.skip( "required_params", "no capability io.parameters for this operator" );

    // c3: required input ports (capability io.inputs required names).
    bool checkedPorts = false;
    if ( capability.isMember( "io" ) && capability["io"].isObject() &&
         capability["io"].isMember( "inputs" ) && capability["io"]["inputs"].isArray() )
    {
      checkedPorts = true;
      std::set<std::string> boundPorts;
      for ( const IrNodeInput &edge : node.inputs )
        boundPorts.insert( edge.as );
      // Plan/recipe style binds inputs through PARAMS ("input": path) rather
      // than wiring edges — a present param satisfies the port exactly like
      // an edge does (compilePlanToWorkflowJson treats them the same).
      if ( node.params.isObject() )
        for ( const std::string &key : node.params.getMemberNames() )
          boundPorts.insert( key );
      for ( const Json::Value &input : capability["io"]["inputs"] )
      {
        if ( !input.isObject() || !input.isMember( "name" ) )
          continue;
        if ( !input.get( "required", false ).asBool() )
          continue;
        const std::string name = input["name"].asString();
        if ( !boundPorts.count( name ) )
        {
          Json::Value details = emptyObject();
          details["required_port"] = name;
          builder.fail( "required_input_ports", error_codes::kInvalidPlan, node.id, "",
                        "Input port '" + name + "' of " + node.operatorId + " is not wired", true,
                        details );
        }
      }
      builder.pass( "required_input_ports" );
    }
    if ( !checkedPorts )
      builder.skip( "required_input_ports", "no capability io.inputs for this operator" );

    // c4: declared port kind vs the capability output contract.
    {
      bool checked = false;
      if ( entry.isMember( "artifacts" ) && entry["artifacts"].isObject() )
      {
        for ( const IrPort &port : node.outputs )
        {
          if ( !port.artifact.isObject() || !port.artifact.isMember( "kind" ) )
            continue;
          if ( entry["artifacts"].isMember( port.name ) &&
               entry["artifacts"][ port.name ].isMember( "kind" ) )
          {
            checked = true;
            const std::string declared = port.artifact["kind"].asString();
            const std::string contracted = entry["artifacts"][ port.name ]["kind"].asString();
            if ( declared != contracted )
            {
              Json::Value details = emptyObject();
              details["declared"] = declared;
              details["contracted"] = contracted;
              details["port"] = port.name;
              builder.fail( "port_kind", error_codes::kInvalidPlan, node.id, port.name,
                            "Declared artifact kind contradicts the operator output contract",
                            false, details );
            }
          }
        }
      }
      if ( checked )
        builder.pass( "port_kind" );
      else
        builder.skip( "port_kind", "no output contract for the declared ports" );
    }

    // Per-input-edge checks.
    for ( const IrNodeInput &edge : node.inputs )
    {
      Json::Value edgeFacts = emptyObject();
      Json::Value edgeStatus = emptyObject();
      if ( !edge.node.empty() )
      {
        const auto upstream = env.nodeOutputs.find( edge.node );
        if ( upstream != env.nodeOutputs.end() )
        {
          const auto port = upstream->second.find( edge.output );
          if ( port != upstream->second.end() )
          {
            edgeFacts = port->second;
            const auto portStatus = env.nodeOutputStatus.find( edge.node );
            if ( portStatus != env.nodeOutputStatus.end() )
            {
              const auto status = portStatus->second.find( edge.output );
              if ( status != portStatus->second.end() )
                edgeStatus = status->second;
            }
          }
        }
      }
      else
      {
        const auto slotFacts = env.slotFacts.find( edge.input );
        const auto slotStatus = env.slotStatus.find( edge.input );
        if ( slotFacts != env.slotFacts.end() )
        {
          edgeFacts = slotFacts->second;
          if ( slotStatus != env.slotStatus.end() )
            edgeStatus = slotStatus->second;
        }
      }
      if ( edgeFacts.empty() )
        continue; // unresolved facts: the slot-level skips already reported

      // c5: modality.
      if ( entry.isMember( "modality" ) && entry["modality"].isArray() &&
           !entry["modality"].empty() && edgeFacts.isMember( "modality" ) )
      {
        const std::string modality = lowered( edgeFacts["modality"].asString() );
        if ( !modality.empty() && modality != artifact_facts::kModalityUnknown )
        {
          bool allowed = false;
          for ( const Json::Value &candidate : entry["modality"] )
            if ( candidate.asString() == modality )
              allowed = true;
          if ( !allowed )
          {
            const std::string status = edgeStatus.get( "modality", "" ).asString();
            Json::Value details = emptyObject();
            details["modality"] = modality;
            details["required"] = entry["modality"];
            details["fact_status"] = status;
            if ( status == "observed" || status == "declared" )
            {
              builder.fail( "modality", error_codes::kModalityMismatch, node.id, edge.as,
                            "Input modality '" + modality + "' is not accepted by " +
                              node.operatorId,
                            false, details );
            }
            else
            {
              builder.warn( "modality", error_codes::kModalityMismatch, node.id, edge.as,
                            "Input modality '" + modality + "' (assumed) is not accepted by " +
                              node.operatorId,
                            details );
            }
          }
          else
          {
            builder.pass( "modality" );
          }
        }
        else
        {
          builder.skip( "modality", "input modality unknown" );
        }
      }
      else
      {
        builder.skip( "modality", "facts or contract missing" );
      }

      // c6: band roles.
      bool bandRolesChecked = false;
      if ( entry.isMember( "band_roles" ) && entry["band_roles"].isObject() &&
           !entry["band_roles"].empty() )
      {
        const facts::BandFacts inputBands = bandFacts( edgeFacts );
        bandRolesChecked = true;
        for ( const std::string &role : entry["band_roles"].getMemberNames() )
        {
          const int minimum = CapabilityKnowledge::bandRoleMinimum( entry, role );
          if ( minimum <= 0 )
            continue;
          const int present = countRole( edgeFacts, role ) > 0
                                ? countRole( edgeFacts, role )
                                : ( roleSatisfiedByWavelength( edgeFacts, role ) ? 1 : 0 );
          if ( present < minimum )
          {
            Json::Value details = emptyObject();
            details["role"] = role;
            details["minimum"] = minimum;
            details["present"] = present;
            details["band_count"] = inputBands.bandCount;
            builder.fail( "band_roles", error_codes::kBandRoleUnresolved, node.id, edge.as,
                          "Input lacks " + std::to_string( minimum - present ) + " band(s) of role '" +
                            role + "' required by " + node.operatorId,
                          // A missing physical band cannot be synthesized by any
                          // deterministic repair — the dataset choice is the fix.
                          false, details );
          }
        }
        builder.pass( "band_roles" );
      }
      if ( !bandRolesChecked )
        builder.skip( "band_roles", "no band-role demands in the capability entry" );

      // c7: declared role vs wavelength window.
      {
        bool checked = false;
        const std::vector<double> centers = wavelengthCenters( edgeFacts );
        if ( !centers.empty() && edgeFacts.isMember( "band_roles" ) &&
             edgeFacts["band_roles"].isArray() )
        {
          int index = 0;
          for ( const Json::Value &roleValue : edgeFacts["band_roles"] )
          {
            if ( roleValue.isString() )
            {
              const std::string role = lowered( roleValue.asString() );
              for ( const RoleWindow &window : kRoleWindows )
              {
                if ( role != window.role )
                  continue;
                checked = true;
                if ( index < static_cast<int>( centers.size() ) )
                {
                  const double center = centers[ index ];
                  if ( center < window.low || center > window.high )
                  {
                    Json::Value details = emptyObject();
                    details["role"] = role;
                    details["center_nm"] = center;
                    details["window_nm"] = wavelengthWindowForRole( role );
                    const std::string status = edgeStatus.get( "wavelengths_nm", "" ).asString();
                    if ( status == "observed" )
                      builder.fail( "wavelength", error_codes::kWavelengthIncompatible, node.id,
                                    edge.as,
                                    "Band role '" + role + "' carries a wavelength outside its "
                                    "physical window",
                                    false, details );
                    else
                      builder.warn( "wavelength", error_codes::kWavelengthIncompatible, node.id,
                                    edge.as,
                                    "Band role '" + role + "' wavelength (declared) is outside "
                                    "its physical window",
                                    details );
                  }
                }
              }
            }
            ++index;
          }
        }
        if ( checked )
          builder.pass( "wavelength" );
        else
          builder.skip( "wavelength", "no wavelengths observed on this input" );
      }

      // c10: temporal.
      {
        const Json::Value temporalDemand =
          entry.isMember( "temporal" ) && entry["temporal"].isObject() ? entry["temporal"]
                                                                       : Json::Value();
        const Json::Value temporalFacts =
          edgeFacts.isMember( "temporal_facts" ) && edgeFacts["temporal_facts"].isObject()
            ? edgeFacts["temporal_facts"]
            : ( edgeFacts.isMember( "temporal" ) && edgeFacts["temporal"].isObject()
                  ? edgeFacts["temporal"]
                  : Json::Value() );
        bool checked = false;
        if ( temporalDemand.isObject() && temporalFacts.isObject() &&
             temporalDemand.isMember( "min_scenes" ) && temporalFacts.isMember( "scene_count" ) )
        {
          checked = true;
          const int minScenes = temporalDemand["min_scenes"].asInt();
          const int sceneCount = temporalFacts.get( "scene_count", 0 ).asInt();
          if ( sceneCount > 0 && sceneCount < minScenes )
          {
            Json::Value details = emptyObject();
            details["min_scenes"] = minScenes;
            details["scene_count"] = sceneCount;
            builder.fail( "temporal", error_codes::kTemporalMisalignment, node.id, edge.as,
                          "Temporal coverage (" + std::to_string( sceneCount ) +
                            " scenes) is below the contract minimum (" +
                            std::to_string( minScenes ) + ")",
                          false, details );
          }
          else
          {
            builder.pass( "temporal" );
          }
        }
        if ( temporalDemand.isObject() &&
             temporalDemand.get( "requires_acquisition_time", false ).asBool() &&
             facts::acquisitionTime( edgeFacts ).empty() && temporalFacts.isNull() )
        {
          checked = true;
          builder.warn( "temporal", error_codes::kTemporalMisalignment, node.id, edge.as,
                        "Contract expects acquisition times; no temporal facts observed on this "
                        "input" );
        }
        if ( !checked )
          builder.skip( "temporal", "no temporal demand or no temporal facts" );
      }

      // c11/c12: numeric domain (dB vs linear, DN vs reflectance, categorical).
      {
        const std::string domain = deriveNumericDomain( edgeFacts );
        if ( domain.empty() )
        {
          builder.skip( "numeric_domain", "no radiometric facts on this input" );
        }
        else
        {
          const bool factBacked = statusIsFact( edgeStatus, "numeric_domain" ) ||
                                  edgeFacts.isMember( "radiometric_state" ) ||
                                  edgeFacts.isMember( "calibration" );
          // SAR calibration-domain check.
          const Json::Value sarDemand =
            entry.isMember( "sar" ) && entry["sar"].isObject() ? entry["sar"] : Json::Value();
          bool sarChecked = false;
          if ( sarDemand.isObject() && sarDemand.isMember( "calibration" ) &&
               sarDemand["calibration"].isArray() && !sarDemand["calibration"].empty() &&
               ( domain == artifact_facts::kDomainDb || domain == artifact_facts::kDomainDn ) )
          {
            sarChecked = true;
            Json::Value details = emptyObject();
            details["numeric_domain"] = domain;
            details["expected_calibration"] = sarDemand["calibration"];
            if ( !factBacked )
            {
              // Assumed-only domain: severity degrades to a warning — facts
              // narrow checks, they never fake errors (review A-5).
              builder.warn( "numeric_domain", error_codes::kCalibrationMismatch, node.id, edge.as,
                            "SAR input domain '" + domain +
                              "' (assumed) does not match the calibration contract of " +
                              node.operatorId,
                            details );
            }
            else if ( domain == artifact_facts::kDomainDn )
            {
              details["repair"] = "rs:sar_calibrate";
              builder.fail( "numeric_domain", error_codes::kCalibrationMismatch, node.id, edge.as,
                            "SAR input is uncalibrated DN; " + node.operatorId +
                              " expects calibrated backscatter",
                            true, details );
            }
            else
            {
              builder.fail( "numeric_domain", error_codes::kCalibrationMismatch, node.id, edge.as,
                            "SAR input is in dB; " + node.operatorId +
                              " expects linear-power backscatter — conversion changes the value "
                              "semantics and needs an explicit decision",
                            false, details );
            }
          }
          // Radiometric acceptability check (optical families).
          const Json::Value radiometric =
            entry.isMember( "radiometric" ) && entry["radiometric"].isObject()
              ? entry["radiometric"]
              : Json::Value();
          bool radiometricChecked = false;
          if ( radiometric.isObject() && radiometric.isMember( "acceptable" ) &&
               radiometric["acceptable"].isArray() && !radiometric["acceptable"].empty() )
          {
            radiometricChecked = true;
            bool acceptable = false;
            bool warnOnly = false;
            for ( const Json::Value &candidate : radiometric["acceptable"] )
              if ( candidate.asString() == domain )
                acceptable = true;
            if ( !acceptable && radiometric.isMember( "warn" ) &&
                 radiometric["warn"].isArray() )
              for ( const Json::Value &candidate : radiometric["warn"] )
                if ( candidate.asString() == domain )
                  warnOnly = true;
            if ( !acceptable && !warnOnly )
            {
              Json::Value details = emptyObject();
              details["numeric_domain"] = domain;
              details["acceptable"] = radiometric["acceptable"];
              const bool repairable = domain == artifact_facts::kDomainDn;
              if ( repairable )
                details["repair"] = "rs:radiometric_calibration";
              if ( factBacked )
              {
                builder.fail( "numeric_domain", error_codes::kInvalidRadiometry, node.id, edge.as,
                              "Input numeric domain '" + domain + "' is not acceptable for " +
                                node.operatorId,
                              repairable, details );
              }
              else
              {
                builder.warn( "numeric_domain", error_codes::kInvalidRadiometry, node.id, edge.as,
                              "Input numeric domain '" + domain +
                                "' (assumed) is not acceptable for " + node.operatorId,
                              details );
              }
            }
            else if ( warnOnly )
            {
              Json::Value details = emptyObject();
              details["numeric_domain"] = domain;
              details["note"] = "acceptable-with-warning";
              builder.warn( "numeric_domain", error_codes::kInvalidRadiometry, node.id, edge.as,
                            "Input numeric domain '" + domain +
                              "' is degraded for " + node.operatorId + " (warn class)",
                            details );
            }
            else
            {
              builder.pass( "numeric_domain" );
            }
          }
          // Categorical inputs into continuous kernels.
          if ( domain == artifact_facts::kDomainCategorical )
          {
            const std::string family = entry.get( "family", "" ).asString();
            if ( isContinuousFamily( family ) )
            {
              Json::Value details = emptyObject();
              details["family"] = family;
              if ( factBacked )
                builder.fail( "numeric_domain", error_codes::kCategoricalMismatch, node.id,
                              edge.as,
                              "Categorical input fed into the continuous '" + family +
                                "' kernel " + node.operatorId,
                              false, details );
              else
                builder.warn( "numeric_domain", error_codes::kCategoricalMismatch, node.id,
                              edge.as,
                              "Categorical input (assumed) fed into the continuous '" + family +
                                "' kernel " + node.operatorId,
                              details );
            }
          }
          if ( !sarChecked && !radiometricChecked &&
               domain != artifact_facts::kDomainCategorical )
          {
            builder.skip( "numeric_domain",
                          "domain known (" + domain + ") but no radiometric contract on " +
                            node.operatorId );
          }
        }
      }
    }

    // c8/c9 (node level, hoisted out of the edge loop — review A-11): CRS
    // conflicts, requires_projected, and shared-grid demands are properties
    // of the NODE's inputs, not of one edge; one issue per node, no dupes.
    {
      std::vector<Json::Value> siblingFacts;
      for ( const IrNodeInput &sibling : node.inputs )
      {
        if ( !sibling.node.empty() )
        {
          const auto upstream = env.nodeOutputs.find( sibling.node );
          if ( upstream != env.nodeOutputs.end() )
          {
            const auto port = upstream->second.find( sibling.output );
            if ( port != upstream->second.end() )
              siblingFacts.push_back( port->second );
          }
        }
        else
        {
          const auto slotFacts = env.slotFacts.find( sibling.input );
          if ( slotFacts != env.slotFacts.end() )
            siblingFacts.push_back( slotFacts->second );
        }
      }

      // Shared CRS normalizer (review A-7): both fact keys, case-folded —
      // `epsg:4326` vs `EPSG:4326` is one CRS, not a conflict.
      {
        bool crsChecked = false;
        std::string firstCrs;
        for ( const Json::Value &sibling : siblingFacts )
        {
          const std::string authid = normalizedCrsAuthid( sibling );
          if ( authid.empty() )
            continue;
          if ( firstCrs.empty() )
          {
            firstCrs = authid;
            continue;
          }
          if ( authid != firstCrs )
          {
            crsChecked = true;
            Json::Value details = emptyObject();
            details["crs_a"] = firstCrs;
            details["crs_b"] = authid;
            builder.fail( "crs", error_codes::kCrsMismatch, node.id, "",
                          "Rasters feed " + node.operatorId + " in different CRS (" + firstCrs +
                            " vs " + authid + ")",
                          true, details );
            break; // one CRS conflict per node; details carry the pair
          }
        }
        if ( entry.isMember( "crs" ) && entry["crs"].isObject() &&
             entry["crs"].get( "requires_projected", false ).asBool() )
        {
          // Every sibling that declares a CRS must be projected (review A-13).
          for ( const Json::Value &sibling : siblingFacts )
          {
            if ( normalizedCrsAuthid( sibling ).empty() )
              continue;
            crsChecked = true;
            if ( isGeographicCrs( sibling ) )
            {
              Json::Value details = emptyObject();
              details["reason"] = "requires_projected";
              builder.fail( "crs", error_codes::kCrsMismatch, node.id, "",
                            node.operatorId + " requires a projected CRS; input is geographic",
                            false, details );
            }
          }
        }
        if ( !crsChecked && siblingFacts.size() >= 2 )
          builder.skip( "crs", "CRS facts missing or equal" );
        else if ( !crsChecked && siblingFacts.size() < 2 )
          builder.skip( "crs", "fewer than two rasters on this node" );
      }

      {
        const bool needsSharedGrid =
          catalog.requiresGrid( node.operatorId ) || relations.requiresGrid( node.operatorId );
        if ( needsSharedGrid && siblingFacts.size() >= 2 )
        {
          const GridCompare verdict = compareGrids( siblingFacts[ 0 ], siblingFacts[ 1 ] );
          if ( verdict == GridCompare::Conflict )
          {
            Json::Value details = emptyObject();
            details["grid_a"] = gridSummary( siblingFacts[ 0 ] );
            details["grid_b"] = gridSummary( siblingFacts[ 1 ] );
            builder.fail( "grid", error_codes::kGridMismatch, node.id, "",
                          node.operatorId +
                            " demands one shared grid (ADR 0098); inputs disagree",
                          true, details );
          }
          else if ( verdict == GridCompare::Equal )
          {
            builder.pass( "grid" );
          }
          else
          {
            builder.skip( "grid", "grid shapes unknown — never a faked pass (review A-4)" );
          }
        }
        else
        {
          builder.skip( "grid", needsSharedGrid ? "grid facts missing"
                                                : "operator tolerates independent grids" );
        }
      }
    }

    // c13: model compatibility (Model Execution Seam operators).
    if ( isModelSeamOperator( node.operatorId ) ||
         entry.get( "family", "" ).asString() == "inference" )
    {
      const std::string modelId =
        node.params.isObject() && node.params.isMember( "model" ) && node.params["model"].isString()
          ? node.params["model"].asString()
          : std::string();
      if ( modelId.empty() )
      {
        Json::Value details = emptyObject();
        details["parameter"] = "model";
        builder.fail( "model_compatibility", error_codes::kInvalidParameter, node.id, "",
                      node.operatorId + " needs a 'model' parameter (catalog id)", true,
                      details );
      }
      else if ( input.modelContracts.isObject() &&
                input.modelContracts.isMember( modelId ) &&
                input.modelContracts[ modelId ].isObject() )
      {
        const Json::Value &contract = input.modelContracts[ modelId ];
        Json::Value primaryFacts = emptyObject();
        if ( !node.inputs.empty() )
          primaryFacts =
            effectiveEdgeFacts( ir, node, node.inputs.front(), input );
        bool checked = false;
        if ( contract.isMember( "input_band_roles" ) && contract["input_band_roles"].isObject() &&
            primaryFacts.isObject() && !primaryFacts.empty() )
        {
          for ( const std::string &role : contract["input_band_roles"].getMemberNames() )
          {
            const int minimum = contract["input_band_roles"][ role ].asInt();
            const int present =
              countRole( primaryFacts, role ) > 0
                ? countRole( primaryFacts, role )
                : ( roleSatisfiedByWavelength( primaryFacts, role ) ? 1 : 0 );
            checked = true;
            if ( present < minimum )
            {
              Json::Value details = emptyObject();
              details["model"] = modelId;
              details["role"] = role;
              details["minimum"] = minimum;
              details["present"] = present;
              builder.fail( "model_compatibility", error_codes::kModelIncompatible, node.id, "",
                            "Model '" + modelId + "' needs " + std::to_string( minimum ) +
                              " band(s) of role '" + role + "'; the upstream artifact has " +
                              std::to_string( present ),
                            false, details );
            }
          }
        }
        if ( checked )
          builder.pass( "model_compatibility" );
        else
          builder.skip( "model_compatibility", "model contract lacks input band facts" );
      }
      else
      {
        builder.skip( "model_compatibility",
                      "no model contract recorded for '" + modelId + "' — run spatial:select_model" );
      }
    }
  }

  // c14: non-deterministic chains.
  {
    const bool wantsDeterministic = ir.expectations.get( "deterministic", false ).asBool();
    for ( const IrNode &node : ir.nodes )
    {
      const bool stochastic = catalog.isStochastic( node.operatorId ) ||
                              node.determinism == artifact_facts::kDeterminismStochastic;
      if ( stochastic && wantsDeterministic )
      {
        Json::Value details = emptyObject();
        details["operator"] = node.operatorId;
        details["expectation"] = "deterministic";
        builder.warn( "nondeterministic_chain", error_codes::kNondeterministicChain, node.id, "",
                      node.operatorId +
                        " may be stochastic; the plan declares deterministic expectations",
                      details );
      }
      if ( node.determinism == artifact_facts::kDeterminismBitExact &&
           catalog.isStochastic( node.operatorId ) )
      {
        Json::Value details = emptyObject();
        details["operator"] = node.operatorId;
        builder.warn( "nondeterministic_chain", error_codes::kNondeterministicChain, node.id, "",
                      "Node declares bit_exact but the capability catalog marks the operator "
                      "stochastic",
                      details );
      }
    }
    builder.pass( "nondeterministic_chain" );
  }

  // c15: fact conflicts recorded during merges.
  for ( const Json::Value &conflict : conflicts )
  {
    Json::Value details = conflict;
    builder.warn( "fact_conflict", error_codes::kFactConflict,
                  conflict.get( "node", "" ).asString(), "",
                  "Declared and observed facts disagree for '" +
                    conflict.get( "key", "" ).asString() + "'; the observed value wins",
                  details );
  }
  if ( conflicts.empty() )
    builder.pass( "fact_conflict" );

  // Verdict: errors all repairable -> fixable; any non-repairable error -> blocked.
  std::vector<IrIssue> issues = builder.takeIssues();
  IrAnalysis analysis;
  bool anyError = false;
  bool allRepairable = true;
  for ( const IrIssue &issue : issues )
  {
    if ( issue.severity != "error" )
      continue;
    anyError = true;
    if ( !issue.repairable )
      allRepairable = false;
  }
  std::sort( issues.begin(), issues.end(),
             []( const IrIssue &a, const IrIssue &b )
             {
               return std::make_tuple( a.code, a.node, a.port, a.message ) <
                      std::make_tuple( b.code, b.node, b.port, b.message );
             } );
  analysis.verdict = !anyError ? "ok" : ( allRepairable ? "fixable" : "blocked" );
  analysis.irFingerprint = fingerprint;
  analysis.facts = factsEcho;
  analysis.factStatus = statusEcho;
  analysis.issues = issues;
  // The checks ledger keeps first-seen order — already the fixed check order.
  analysis.checks = builder.checksJson();
  return analysis;
}

} // namespace sicnu::agent::harness
