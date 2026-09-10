// src/agent/harness/evidence.cpp
#include "evidence.h"

#include "harness_error.h"
#include "harness_verification.h"

#include <workflow/workflow_run.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <json/writer.h>

namespace sicnu::agent::harness::evidence {

namespace {

constexpr int kMaxHarvestFacts = 16;

/// Closed result-payload keys the harness treats as operator-declared
/// uncertainty facts. Keys outside this list are never interpreted.
const char *const kUncertaintyKeys[] = {
  "uncertainty", "uncertaintyOutput", "uncertainty_band", "confidence",
};

std::string compactJson( const Json::Value &doc )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "";
  builder["commentStyle"] = "None";
  return Json::writeString( builder, doc );
}

/// Atomic sidecar write following the engine's own #698 convention
/// (QSaveFile = write to temp + rename in the same directory).
SidecarResult atomicWrite( const std::string &path, const Json::Value &doc )
{
  SidecarResult result;
  result.path = path;
  QSaveFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::WriteOnly ) )
  {
    result.error = "cannot open sidecar for write: " + path;
    return result;
  }
  const QByteArray bytes = QJsonDocument(
    QJsonDocument::fromJson( QByteArray::fromStdString( compactJson( doc ) ) ) ).toJson();
  file.write( bytes );
  if ( !file.commit() )
  {
    result.error = "sidecar commit failed: " + path;
    return result;
  }
  result.written = true;
  return result;
}

Json::Value makeEnvelope( const std::string &kind, const std::string &artifact )
{
  Json::Value doc( Json::objectValue );
  doc["schema_version"] = "1.0";
  doc["kind"] = kind;
  doc["artifact"] = artifact;
  doc["generated_at"] = QDateTime::currentDateTimeUtc().toString( Qt::ISODate ).toStdString();
  return doc;
}

} // namespace

UncertaintyHarvest harvestUncertainty( const std::vector<sicnu::workflow::StepPlan> &steps,
                                       const std::string &outputPath )
{
  UncertaintyHarvest harvest;
  if ( outputPath.empty() )
    return harvest;

  for ( const sicnu::workflow::StepPlan &step : steps )
  {
    if ( step.outputLayerPath != outputPath )
      continue;
    const Json::Value &payload = step.resultPayload;
    if ( !payload.isObject() )
      continue;

    Json::Value facts( Json::objectValue );
    for ( const char *key : kUncertaintyKeys )
      if ( payload.isMember( key ) )
        facts[key] = payload[key];
    if ( facts.empty() )
      continue;

    Json::Value entry( Json::objectValue );
    entry["step_id"] = step.stepId;
    entry["operator_id"] = step.operatorId;
    entry["facts"] = facts;
    harvest.facts.append( entry );
    harvest.declared = true;
    if ( harvest.facts.size() >= kMaxHarvestFacts )
      break;
  }
  return harvest;
}

SidecarResult writeUncertaintySidecar( const std::string &outputPath,
                                       const UncertaintyHarvest &harvest )
{
  if ( !harvest.declared )
    return {}; // honest absence — no fabrication, no file
  Json::Value doc = makeEnvelope( "uncertainty_sidecar", outputPath );
  doc["source"] = "operator_declared";
  doc["facts"] = harvest.facts;
  return atomicWrite( outputPath + ".uncertainty.json", doc );
}

SidecarResult writeProvenanceSidecarIfAbsent( const std::string &outputPath,
                                              const Json::Value &runIdentity )
{
  const std::string sidecarPath = outputPath + ".provenance.json";
  if ( QFileInfo::exists( QString::fromStdString( sidecarPath ) ) )
    return {}; // the engine already wrote derivation provenance — never overwrite

  Json::Value doc = makeEnvelope( "harness_provenance", outputPath );
  doc["source"] = "harness_run_identity";
  doc["run"] = runIdentity;
  return atomicWrite( sidecarPath, doc );
}

SidecarResult writeVerificationEvidence( const std::string &outputPath,
                                         const ArtifactVerification &verification,
                                         const VerificationExpectations &expectations,
                                         const Json::Value &runIdentity,
                                         const UncertaintyHarvest &uncertainty )
{
  Json::Value doc = makeEnvelope( "verification_evidence", outputPath );
  doc["run"] = runIdentity;

  // The verification record itself.
  Json::Value record = verification.toJson();
  record["expectations"] = [&expectations] {
    Json::Value e( Json::objectValue );
    if ( !expectations.kind.empty() )
      e["kind"] = expectations.kind;
    if ( !expectations.crs.empty() )
      e["crs"] = expectations.crs;
    if ( expectations.width )
      e["width"] = expectations.width;
    if ( expectations.height )
      e["height"] = expectations.height;
    if ( expectations.minFiniteFraction > 0.0 )
      e["min_finite_fraction"] = expectations.minFiniteFraction;
    if ( expectations.maxNodataFraction < 1.0 )
      e["max_nodata_fraction"] = expectations.maxNodataFraction;
    if ( expectations.classValues.isArray() && !expectations.classValues.empty() )
      e["class_values"] = expectations.classValues;
    if ( expectations.expectedExtent.isObject() )
      e["expected_extent"] = expectations.expectedExtent;
    e["require_provenance"] = expectations.requireProvenance;
    if ( expectations.requireUncertainty )
      e["require_uncertainty"] = true;
    if ( expectations.expectedBandCount > 0 )
      e["expected_band_count"] = expectations.expectedBandCount;
    return e;
  }();
  doc["verification"] = record;

  // Quality summary: the bounded rollup agents and reports consume.
  Json::Value quality( Json::objectValue );
  quality["verdict"] = verdictToStringWire( verification.verdict );
  int failed = 0;
  int warnings = 0;
  for ( const VerificationCheck &check : verification.checks )
  {
    if ( check.passed )
      continue;
    if ( check.severity == "error" )
      ++failed;
    else
      ++warnings;
  }
  quality["failed_checks"] = failed;
  quality["warning_checks"] = warnings;
  quality["check_count"] = static_cast<Json::Int>( verification.checks.size() );
  quality["uncertainty_declared"] = uncertainty.declared;
  doc["quality"] = quality;

  if ( uncertainty.declared )
    doc["uncertainty_sidecar"] = outputPath + ".uncertainty.json";

  return atomicWrite( outputPath + ".verification.json", doc );
}

} // namespace sicnu::agent::harness::evidence
