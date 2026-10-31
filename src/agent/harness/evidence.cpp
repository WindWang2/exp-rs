// src/agent/harness/evidence.cpp
#include "evidence.h"

#include "harness_error.h"
#include "harness_verification.h"

#include <workflow/workflow_run.h>

#include "platform/durable_sidecar.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <json/reader.h>
#include <json/writer.h>

#include <optional>

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
/// (QSaveFile = write to temp + rename in the same directory). The jsoncpp
/// serialization is written verbatim — no second parser round-trip that
/// could silently produce an empty file.
SidecarResult atomicWrite( const std::string &path, const Json::Value &doc )
{
  SidecarResult result;
  result.path = path;
  const QByteArray bytes = QByteArray::fromStdString( compactJson( doc ) );
  if ( bytes.isEmpty() )
  {
    result.error = "refusing to write an empty sidecar: " + path;
    return result;
  }
  // R6: the single sidecar write authority — O_EXCL temp, fsync gate, atomic
  // publish, verify read-back (the old QSaveFile commit() never checked the
  // bytes actually landed, and wrote no fsync at all).
  const sicnu::platform::sidecar::WriteResult write = sicnu::platform::sidecar::write(
    { path, std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) ), "" } );
  if ( !write )
  {
    result.error = "sidecar publish failed: " + path + " (" + write.error + ")";
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

/// R26: classifies an existing provenance sidecar for the replace decision.
/// Only a parseable harness-written envelope (kind=harness_provenance)
/// naming a DIFFERENT run is stale bookkeeping the harness owns and may
/// replace; anything else (engine DerivationRecord — no envelope — or an
/// unparseable file) keeps the never-overwrite contract.
bool provenanceIsStaleHarnessEnvelope( const std::string &sidecarPath,
                                       const Json::Value &runIdentity )
{
  QFile file( QString::fromStdString( sidecarPath ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    return false;
  const QByteArray raw = file.readAll();
  Json::Value doc;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &doc, &errors ) )
    return false; // unknown owner — the engine's lane, never overwrite
  if ( doc.get( "kind", "" ).asString() != "harness_provenance" )
    return false; // engine DerivationRecord shape: engine provenance wins
  // runIdentity carries run_id at its top level (see runIdentityFor); the
  // sidecar nests that same object under "run".
  const std::string currentRun = runIdentity.get( "run_id", "" ).asString();
  const std::string recordedRun = doc.get( "run", Json::Value() )
                                    .get( "run_id", "" )
                                    .asString();
  // A harness envelope whose run block is missing or malformed names no
  // run at all: it cannot be attributed to a stale previous run, so the
  // never-overwrite contract applies (fail closed, R1).
  if ( recordedRun.empty() )
    return false;
  if ( currentRun.empty() )
    return false; // no run identity to attribute the replacement to
  return recordedRun != currentRun;
}

SidecarResult writeProvenanceSidecarIfAbsent( const std::string &outputPath,
                                              const Json::Value &runIdentity )
{
  const std::string sidecarPath = outputPath + ".provenance.json";
  if ( QFileInfo::exists( QString::fromStdString( sidecarPath ) ) )
  {
    // The engine's own derivation provenance always wins. A harness
    // envelope from a PREVIOUS run is stale bookkeeping for re-executed
    // artifact bytes: keeping it would attribute the new output to the old
    // run, so it is replaced atomically (R26).
    if ( !provenanceIsStaleHarnessEnvelope( sidecarPath, runIdentity ) )
      return {};
  }

  Json::Value doc = makeEnvelope( "harness_provenance", outputPath );
  doc["source"] = "harness_run_identity";
  doc["run"] = runIdentity;
  // Narrow the exists-then-write window once more: an engine sidecar landing
  // between the first check and here must win.
  if ( QFileInfo::exists( QString::fromStdString( sidecarPath ) ) &&
       !provenanceIsStaleHarnessEnvelope( sidecarPath, runIdentity ) )
    return {};
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

EvidenceInspection inspectVerificationEvidence( const std::string &outputPath,
                                                 const std::string &runId )
{
  EvidenceInspection inspection;
  QFile file( QString::fromStdString( outputPath + ".verification.json" ) );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    inspection.state = EvidenceState::Absent;
    return inspection;
  }
  const QByteArray raw = file.readAll();
  Json::Value doc;
  Json::CharReaderBuilder builder;
  std::string errors;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  if ( !reader->parse( raw.constData(), raw.constData() + raw.size(), &doc, &errors ) )
  {
    inspection.state = EvidenceState::Corrupt;
    inspection.reason = "unparseable verification sidecar: " + errors;
    return inspection;
  }
  if ( doc.get( "kind", "" ).asString() != "verification_evidence" )
  {
    inspection.state = EvidenceState::Corrupt;
    inspection.reason = "sidecar kind is not verification_evidence";
    return inspection;
  }
  const std::string recordedRun =
    doc.get( "run", Json::Value() ).get( "run_id", "" ).asString();
  // R1: a verification_evidence sidecar whose run block is missing or
  // malformed names no run. Treating that as ForeignRun would recompute
  // and then overwrite an unattributable record — the exact audit-trail
  // loss the Corrupt branch exists to prevent.
  if ( recordedRun.empty() )
  {
    inspection.state = EvidenceState::Corrupt;
    inspection.reason = "verification sidecar carries no run identity";
    return inspection;
  }
  if ( recordedRun != runId )
  {
    // A different run overwrote the artifact — re-evaluation is the honest
    // path for the new run.
    inspection.state = EvidenceState::ForeignRun;
    return inspection;
  }

  const Json::Value &record = doc.get( "verification", Json::Value() );
  if ( !record.isObject() )
  {
    inspection.state = EvidenceState::Corrupt;
    inspection.reason = "verification record is not an object";
    return inspection;
  }

  ArtifactVerification artifact;
  artifact.path = outputPath;
  artifact.kind = record.get( "kind", "" ).asString();
  const std::string verdict = record.get( "verdict", "" ).asString();
  if ( verdict == "PASS" )
    artifact.verdict = Verdict::Pass;
  else if ( verdict == "PASS_WITH_WARNINGS" )
    artifact.verdict = Verdict::PassWithWarnings;
  else if ( verdict == "FAIL" )
    artifact.verdict = Verdict::Fail;
  else
  {
    inspection.state = EvidenceState::Corrupt;
    inspection.reason = "unknown verdict string: " + verdict;
    return inspection;
  }

  for ( const Json::Value &entry : record.get( "checks", Json::Value( Json::arrayValue ) ) )
  {
    if ( !entry.isObject() || !entry.isMember( "check" ) || !entry.isMember( "passed" ) )
    {
      inspection.state = EvidenceState::Corrupt;
      inspection.reason = "malformed verification check entry";
      return inspection;
    }
    VerificationCheck check;
    check.check = entry.get( "check", "" ).asString();
    check.passed = entry.get( "passed", false ).asBool();
    check.severity = entry.get( "severity", check.passed ? "info" : "error" ).asString();
    check.code = entry.get( "code", "" ).asString();
    check.details = entry.get( "details", Json::Value( Json::objectValue ) );
    artifact.checks.push_back( std::move( check ) );
  }
  inspection.state = EvidenceState::Reusable;
  inspection.verification = std::move( artifact );
  return inspection;
}

std::optional<ArtifactVerification> readVerificationEvidence( const std::string &outputPath,
                                                              const std::string &runId )
{
  EvidenceInspection inspection = inspectVerificationEvidence( outputPath, runId );
  if ( inspection.state != EvidenceState::Reusable )
    return std::nullopt;
  return std::move( inspection.verification );
}

} // namespace sicnu::agent::harness::evidence
