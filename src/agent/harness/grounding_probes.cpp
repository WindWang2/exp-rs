// src/agent/harness/grounding_probes.cpp
#include "grounding_probes.h"

#include "context_ledger.h"
#include "entity_resolver.h"
#include "grounding_tools.h"
#include "harness_error.h"
#include "workflow_facts.h"

#include "../spatial_tools/spatial_tool.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value emptyObject() { return Json::Value( Json::objectValue ); }

// Same local schema helper every sibling harness TU carries (each defines its
// own in an anonymous namespace; grounding_tools.cpp:32 is the canonical body).
Json::Value objectSchema( Json::Value properties, Json::Value required )
{
  Json::Value schema( Json::objectValue );
  schema["type"] = "object";
  schema["properties"] = std::move( properties );
  if ( required.isArray() && !required.empty() )
    schema["required"] = std::move( required );
  return schema;
}

/// One scope's projection out of an understanding BODY. Every projection
/// copies only keys the scope owns; absent keys are absent from the
/// projection and their fact_status stays "unknown" (the body's own
/// attachFactStatus verdict is preserved verbatim).
Json::Value projectScope( const std::string &scope, const Json::Value &body,
                          const Json::Value &factStatus )
{
  Json::Value out( Json::objectValue );
  auto copy = [&]( const char *key ) {
    if ( body.isMember( key ) && !body[key].isNull() )
      out[key] = body[key];
    out["fact_status"][key] = factStatus.isMember( key ) ? factStatus[key] : Json::Value( "unknown" );
  };
  if ( scope == probe_scope::kIdentity )
  {
    for ( const char *key : { "source_kind", "path", "driver" } )
      copy( key );
    if ( body.isMember( "entity" ) && body["entity"].isObject() )
    {
      out["entity"] = body["entity"];
      out["fact_status"]["entity"] = "observed";
    }
  }
  else if ( scope == probe_scope::kGrid )
  {
    for ( const char *key : { "size", "pixel_size" } )
      copy( key );
  }
  else if ( scope == probe_scope::kCrs )
  {
    copy( "crs" );
    copy( "crs_authid" );
  }
  else if ( scope == probe_scope::kExtent )
  {
    copy( "extent" );
  }
  else if ( scope == probe_scope::kBands )
  {
    for ( const char *key : { "band_roles", "band_count", "bands", "wavelengths_nm" } )
      copy( key );
  }
  else if ( scope == probe_scope::kTemporal )
  {
    // Raw observed slots + the 2.0 cadence facts (derived here, bounded).
    copy( "acquisition_time" );
    const wfacts::TemporalCadenceFacts cadence = wfacts::temporalCadenceFromUnderstanding( body );
    out["temporal_facts"] = cadence.toJson( "observed" );
  }
  else if ( scope == probe_scope::kNodata )
  {
    copy( "nodata" );
  }
  else if ( scope == probe_scope::kQualityMasks )
  {
    copy( "quality_masks" );
    const wfacts::QualityMaskFacts masks = wfacts::qualityMaskFacts( body );
    out["quality_mask_facts"] = masks.toJson();
  }
  else if ( scope == probe_scope::kRadiometric )
  {
    for ( const char *key : { "radiometric_state", "calibration", "polarizations" } )
      copy( key );
  }
  else if ( scope == probe_scope::kModality )
  {
    copy( "modality" );
  }
  else if ( scope == probe_scope::kProduct )
  {
    for ( const char *key : { "sensor", "product_type", "product_id", "processing_level",
                              "product_metadata" } )
      copy( key );
  }
  return out;
}

/// The agent-facing surface: `harness:probe_facts` — the bounded probe over
/// the same scopes the analysis consumes. Deterministic, read-only, cached.
class FactsProbeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:probe_facts"; }
    std::string displayName() const override { return "Bounded Fact Probe"; }
    std::string description() const override
    {
      return "Answers a CLOSED set of fact scopes about one dataset (identity, "
             "grid, crs, extent, bands, temporal, nodata, quality_masks, "
             "radiometric, modality, product) with budgeted, cache-backed "
             "grounding. Unknown facts are typed 'unknown' — never guessed. "
             "Read-only.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "grounding", "probe", "facts", "bounded" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value ref( Json::objectValue );
      ref["type"] = "string";
      ref["description"] = "Dataset reference: asset-N id, asset UUID, path, "
                           "or display name.";
      props["ref"] = ref;
      Json::Value scopes( Json::objectValue );
      scopes["type"] = "array";
      Json::Value scopeItems( Json::objectValue );
      scopeItems["type"] = "string";
      scopeItems["enum"] = Json::Value( Json::arrayValue );
      for ( const std::string &scope : probe_scope::allProbeScopes() )
        scopeItems["enum"].append( scope );
      scopes["items"] = scopeItems;
      scopes["description"] = "Fact scopes to answer (1..16).";
      props["scopes"] = scopes;
      Json::Value deadline( Json::objectValue );
      deadline["type"] = "integer";
      deadline["description"] = "Wall budget in ms (default 2000).";
      props["deadline_ms"] = deadline;
      Json::Value required( Json::arrayValue );
      required.append( "ref" );
      required.append( "scopes" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["ok"] = Json::Value( Json::booleanValue );
      props["facts"] = Json::Value( Json::objectValue );
      props["entity"] = Json::Value( Json::objectValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isObject() || !input.isMember( "ref" ) || !input["ref"].isString() )
        return SpatialToolResult::failure( "missing string parameter 'ref'",
                                           error_codes::kInvalidParameter, "validation" );
      ProbeRequest request;
      request.ref = input["ref"].asString();
      for ( const Json::Value &scope : input.get( "scopes", Json::Value( Json::arrayValue ) ) )
        if ( scope.isString() )
          request.scopes.push_back( scope.asString() );
      if ( input.isMember( "deadline_ms" ) && input["deadline_ms"].isNumeric() )
        request.deadlineMs = input["deadline_ms"].asInt();
      const ProbeOutcome outcome = probeDatasetFacts( request );
      if ( !outcome.ok )
        return SpatialToolResult::failure( outcome.summary, outcome.code, "validation" );
      return SpatialToolResult::ok( outcome.toJson() );
    }
};

/// `harness:probe_model` — the bounded model-manifest probe (ledger contract
/// + stat-only artifact presence).
class ModelProbeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "harness:probe_model"; }
    std::string displayName() const override { return "Model Manifest Probe"; }
    std::string description() const override
    {
      return "Probes the harness-observed contract for one model id (task, "
             "readiness, cost) plus stat-only presence of its weight file. "
             "An unknown model id is a typed MODEL_NOT_READY — never a "
             "guessed contract. Read-only.";
    }
    std::vector<std::string> tags() const override
    {
      return { "harness", "model", "probe", "manifest", "bounded" };
    }

    Json::Value inputSchema() const override
    {
      Json::Value props( Json::objectValue );
      Json::Value model( Json::objectValue );
      model["type"] = "string";
      model["description"] = "Model catalog id (as select_model recorded it).";
      props["model"] = model;
      Json::Value required( Json::arrayValue );
      required.append( "model" );
      return objectSchema( std::move( props ), std::move( required ) );
    }

    Json::Value outputSchema() const override
    {
      Json::Value props( Json::objectValue );
      props["ok"] = Json::Value( Json::booleanValue );
      props["contract"] = Json::Value( Json::objectValue );
      props["artifact_present"] = Json::Value( Json::booleanValue );
      return objectSchema( std::move( props ), Json::Value() );
    }

    SpatialToolResult execute( const Json::Value &input ) override
    {
      if ( !input.isObject() || !input.isMember( "model" ) || !input["model"].isString() )
        return SpatialToolResult::failure( "missing string parameter 'model'",
                                           error_codes::kInvalidParameter, "validation" );
      const ModelProbeOutcome outcome =
        probeModelManifest( input["model"].asString() );
      if ( !outcome.ok )
        return SpatialToolResult::failure( outcome.summary, outcome.code, "environment" );
      return SpatialToolResult::ok( outcome.toJson() );
    }
};

} // namespace

void registerGroundingProbeTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<FactsProbeTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<ModelProbeTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

namespace probe_scope {
bool isKnownProbeScope( const std::string &scope )
{
  for ( const std::string &known : allProbeScopes() )
    if ( known == scope )
      return true;
  return false;
}

std::vector<std::string> allProbeScopes()
{
  return { kIdentity, kGrid, kCrs, kExtent, kBands, kTemporal, kNodata, kQualityMasks,
           kRadiometric, kModality, kProduct };
}
} // namespace probe_scope

Json::Value probeLimits()
{
  Json::Value limits( Json::objectValue );
  limits["default_deadline_ms"] = ProbeLimits::kDefaultDeadlineMs;
  limits["max_scopes"] = ProbeLimits::kMaxScopes;
  limits["max_scopes_in_facts"] = ProbeLimits::kMaxScopesInFacts;
  limits["logical_byte_budget"] = static_cast<Json::Int64>( ProbeLimits::kLogicalByteBudget );
  return limits;
}

Json::Value ProbeOutcome::toJson() const
{
  Json::Value out( Json::objectValue );
  out["ok"] = ok;
  if ( !code.empty() )
    out["code"] = code;
  if ( !summary.empty() )
    out["summary"] = summary;
  if ( ok )
  {
    out["source"] = source;
    out["elapsed_ms"] = static_cast<Json::Int64>( elapsedMs );
    out["deadline_exceeded"] = deadlineExceeded;
    out["facts"] = facts;
    out["entity"] = entity;
  }
  return out;
}

ProbeOutcome probeDatasetFacts( const ProbeRequest &request )
{
  ProbeOutcome outcome;

  // Scope validation BEFORE any I/O: an unknown scope is a typed error, a
  // duplicate is tolerated (dedup keeps first occurrence), an over-bounds
  // scope list fails without probing.
  std::vector<std::string> scopes;
  for ( const std::string &scope : request.scopes )
  {
    if ( !probe_scope::isKnownProbeScope( scope ) )
    {
      outcome.code = error_codes::kInvalidParameter;
      outcome.summary = "Unknown probe scope '" + scope + "'";
      return outcome;
    }
    if ( std::find( scopes.begin(), scopes.end(), scope ) == scopes.end() )
      scopes.push_back( scope );
  }
  if ( scopes.empty() || static_cast<int>( scopes.size() ) > ProbeLimits::kMaxScopes )
  {
    outcome.code = error_codes::kInvalidParameter;
    outcome.summary = scopes.empty() ? "Probe request needs at least one scope"
                                     : "Probe request exceeds the scope bound";
    return outcome;
  }

  // Anti-hallucination: the ONE resolver decides what the reference is.
  HarnessError resolveError;
  const std::optional<ResolvedDataset> resolved =
    resolveDatasetRef( QString::fromStdString( request.ref ), &resolveError );
  if ( !resolved )
  {
    outcome.code = resolveError.code.empty() ? error_codes::kDatasetNotFound
                                             : resolveError.code;
    outcome.summary = resolveError.summary;
    return outcome;
  }
  outcome.entity = resolved->toJson();

  // Cache-first: an unchanged dataset answers from the shared understanding
  // cache (the SAME key the grounding tools use) without touching the file.
  const QString cacheKey = understandingCacheKeyFor( resolved->path, resolved->revision );
  Json::Value understanding = ContextLedger::instance().cachedUnderstanding( cacheKey );
  bool fromCache = !understanding.isNull();
  if ( !fromCache )
  {
    // Delegate to the ONE understanding tool — the probe never opens
    // datasets itself, so grounding behavior cannot drift between the two
    // surfaces.
    QElapsedTimer timer;
    timer.start();
    SpatialToolPtr tool = SpatialToolRegistry::instance().find( "spatial:understand" )
                            .value_or( nullptr );
    if ( !tool )
    {
      outcome.code = error_codes::kToolNotFound;
      outcome.summary = "spatial:understand is not registered";
      return outcome;
    }
    Json::Value input( Json::objectValue );
    input["asset"] = request.ref;
    const SpatialToolResult result = tool->execute( input );
    outcome.elapsedMs = timer.elapsed();
    outcome.deadlineExceeded =
      outcome.elapsedMs > ( request.deadlineMs > 0 ? request.deadlineMs
                                                   : ProbeLimits::kDefaultDeadlineMs );
    if ( !result.success || result.output.isNull() )
    {
      outcome.code = result.errorCode.empty() ? error_codes::kDatasetNotFound
                                              : result.errorCode;
      outcome.summary = result.error.empty() ? "grounding failed" : result.error;
      return outcome;
    }
    understanding = result.output.get( "dataset_understanding", Json::Value() );
    if ( understanding.isNull() )
    {
      outcome.code = error_codes::kExecutionFailed;
      outcome.summary = "grounding returned no understanding document";
      return outcome;
    }
  }
  outcome.source = fromCache ? "cache" : "live";
  outcome.ok = true;

  // Envelope or body both accepted (mirrors the analysis fact convention).
  Json::Value body = understanding.isObject() && understanding.isMember( "dataset_understanding" )
                       ? understanding["dataset_understanding"]
                       : understanding;
  const Json::Value factStatus = body.get( "fact_status", Json::Value( Json::objectValue ) );

  Json::Value facts( Json::objectValue );
  for ( const std::string &scope : scopes )
  {
    const Json::Value projected = projectScope( scope, body, factStatus );
    for ( const std::string &key : projected.getMemberNames() )
      facts[key] = projected[key];
  }
  outcome.facts = facts;
  return outcome;
}

Json::Value ModelProbeOutcome::toJson() const
{
  Json::Value out( Json::objectValue );
  out["ok"] = ok;
  if ( !code.empty() )
    out["code"] = code;
  if ( !summary.empty() )
    out["summary"] = summary;
  if ( !contract.isNull() && contract.isObject() && !contract.empty() )
    out["contract"] = contract;
  out["artifact_present"] = artifactPresent;
  if ( artifactBytes >= 0 )
    out["artifact_bytes"] = static_cast<Json::Int64>( artifactBytes );
  return out;
}

ModelProbeOutcome probeModelManifest( const std::string &modelId )
{
  ModelProbeOutcome out;
  if ( modelId.empty() )
  {
    out.code = error_codes::kInvalidParameter;
    out.summary = "model id is empty";
    return out;
  }
  const Json::Value contracts = ContextLedger::instance().modelContracts();
  if ( !contracts.isObject() || !contracts.isMember( modelId ) ||
       !contracts[modelId].isObject() )
  {
    out.code = error_codes::kModelNotReady;
    out.summary = "no model contract observed for '" + modelId + "'";
    return out;
  }
  out.contract = contracts[modelId];
  out.ok = true;

  // Stat-only artifact check: presence + size, never a content read.
  std::string artifactPath;
  for ( const char *key : { "artifact_path", "path" } )
  {
    if ( out.contract.isMember( key ) && out.contract[key].isString() &&
         !out.contract[key].asString().empty() )
    {
      artifactPath = out.contract[key].asString();
      break;
    }
  }
  if ( !artifactPath.empty() )
  {
    const QFileInfo info( QString::fromStdString( artifactPath ) );
    out.artifactPresent = info.exists() && info.isFile();
    if ( out.artifactPresent )
      out.artifactBytes = info.size();
  }
  return out;
}

} // namespace sicnu::agent::harness
