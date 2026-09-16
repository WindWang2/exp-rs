// src/agent/harness/provenance_projection.cpp
#include "provenance_projection.h"

#include "workflow_facts.h"

#include <QFile>
#include <QIODevice>
#include <QSaveFile>

#include <json/writer.h>

#include <QCryptographicHash>
#include <QString>

#include <algorithm>
#include <set>
#include <utility>

namespace sicnu::agent::harness::projection {

namespace {

Json::Value emptyObject() { return Json::Value( Json::objectValue ); }

/// Deterministic compact serialization (sorted members via jsoncpp).
std::string canonicalJson( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, value );
}

} // namespace

Json::Value projectionLimits()
{
  Json::Value limits( Json::objectValue );
  limits["max_text_chars"] = ProjectionLimits::kMaxTextChars;
  limits["max_checks_echoed"] = ProjectionLimits::kMaxChecksEchoed;
  limits["max_repairs"] = ProjectionLimits::kMaxRepairs;
  limits["max_refusals"] = ProjectionLimits::kMaxRefusals;
  return limits;
}

Json::Value compilerProjection( const WorkflowIr &ir, const IrAnalysis &analysis,
                                const std::vector<IrRepairRecord> &repairs,
                                const std::vector<IrRefusal> &refusals )
{
  Json::Value block( Json::objectValue );
  block["schema_version"] = kProjectionSchemaVersion;

  // Long free-form strings are clamped to the bound; every clamp is counted
  // in `truncated_keys` so the projection never lies about its own shape.
  const auto clamp = [ &truncated = block[ "truncated_keys" ] ]( std::string text ) {
    if ( static_cast<int>( text.size() ) <= ProjectionLimits::kMaxTextChars )
      return text;
    truncated.append( "text" );
    return text.substr( 0, ProjectionLimits::kMaxTextChars );
  };

  block["ir_fingerprint"] = clamp( workflowIrFingerprint( ir ) );
  const std::string irId = ir.irId.empty() ? deriveIrId( ir ) : ir.irId;
  block["ir_id"] = clamp( irId );
  if ( !ir.intent.empty() )
    block["intent"] = clamp( ir.intent );

  // Facts echo: per input slot, the merged facts digest + fact_status —
  // enough to audit every verdict without duplicating the understanding
  // documents.
  Json::Value slots( Json::objectValue );
  for ( const std::string &slotName : analysis.facts.getMemberNames() )
  {
    Json::Value entry( Json::objectValue );
    entry["facts_digest"] = wfacts::workflowFactsDigest( analysis.facts[ slotName ] );
    if ( analysis.factStatus.isMember( slotName ) )
      entry["fact_status"] = analysis.factStatus[ slotName ];
    slots[ slotName ] = entry;
  }
  if ( !slots.empty() )
    block["input_facts"] = slots;
  block["analysis_verdict"] = analysis.verdict;
  block["ir_fingerprint_analyzed"] = clamp( analysis.irFingerprint );

  // Checks ledger echo (bounded): check name, status, count.
  Json::Value checks( Json::arrayValue );
  int checkIndex = 0;
  for ( const Json::Value &row : analysis.checks )
  {
    if ( checkIndex++ >= ProjectionLimits::kMaxChecksEchoed )
    {
      block["truncated_keys"].append( "checks" );
      break;
    }
    if ( !row.isObject() )
      continue;
    Json::Value entry( Json::objectValue );
    entry["check"] = row.get( "check", "" );
    entry["status"] = row.get( "status", "" );
    entry["count"] = row.get( "count", 0 );
    if ( row.isMember( "code" ) )
      entry["code"] = row["code"];
    checks.append( entry );
  }
  block["checks"] = checks;

  // Repairs + refusals: the compiler's mutations and its refusals to mutate,
  // both bounded and carried verbatim (their own toJson shapes).
  Json::Value repairsJson( Json::arrayValue );
  int repairIndex = 0;
  for ( const IrRepairRecord &record : repairs )
  {
    if ( repairIndex++ >= ProjectionLimits::kMaxRepairs )
    {
      block["truncated_keys"].append( "repairs" );
      break;
    }
    repairsJson.append( record.toJson() );
  }
  if ( !repairsJson.empty() )
    block["repairs"] = repairsJson;

  Json::Value refusalsJson( Json::arrayValue );
  int refusalIndex = 0;
  for ( const IrRefusal &refusal : refusals )
  {
    if ( refusalIndex++ >= ProjectionLimits::kMaxRefusals )
    {
      block["truncated_keys"].append( "refusals" );
      break;
    }
    refusalsJson.append( refusal.toJson() );
  }
  if ( !refusalsJson.empty() )
    block["refusals"] = refusalsJson;

  if ( block["truncated_keys"].empty() )
    block.removeMember( "truncated_keys" );
  return block;
}

std::string projectionDigest( const Json::Value &projection )
{
  const QByteArray digest = QCryptographicHash::hash(
    QByteArray::fromStdString( canonicalJson( projection ) ), QCryptographicHash::Sha256 );
  return QString::fromLatin1( digest.left( 16 ).toHex() ).toStdString();
}

Json::Value attachToWorkflowJson( const Json::Value &workflowDef, const Json::Value &projection )
{
  Json::Value out = workflowDef;
  if ( !out.isObject() )
    return out;
  if ( !out.isMember( "metadata" ) || !out["metadata"].isObject() )
    out["metadata"] = emptyObject();

  Json::Value metadata = out["metadata"];
  if ( metadata.isMember( kCompilerMetadataKey ) )
  {
    const Json::Value existing = metadata[ kCompilerMetadataKey ];
    const std::string existingDigest = existing.isObject() && existing.isMember( "digest" )
                                         ? existing["digest"].asString()
                                         : projectionDigest( existing );
    if ( existingDigest != projectionDigest( projection ) )
      metadata[ "compiler_superseded" ] = existing; // bounded: one level
  }
  Json::Value block = projection;
  block[ "digest" ] = projectionDigest( block );
  metadata[ kCompilerMetadataKey ] = block;
  out[ "metadata" ] = metadata;
  return out;
}

SidecarResult writeCompileSidecar( const std::string &outputPath,
                                   const Json::Value &projection )
{
  SidecarResult result;
  result.path = outputPath + ".compile.json";

  const QString sidecarPath = QString::fromStdString( result.path );
  QSaveFile file( sidecarPath );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
  {
    result.error = "cannot open sidecar for writing: " + result.path;
    return result;
  }
  const QByteArray body = QByteArray::fromStdString( canonicalJson( projection ) + "\n" );
  if ( file.write( body ) != body.size() )
  {
    file.cancelWriting();
    result.error = "short write on sidecar: " + result.path;
    return result;
  }
  if ( !file.commit() )
  {
    result.error = "sidecar commit failed: " + result.path;
    return result;
  }
  result.written = true;
  return result;
}

} // namespace sicnu::agent::harness::projection
