/***************************************************************************
 * step_explanation.h — versioned value objects for per-step explanations
 *
 * Wire schema: "exp.step_explanation.v1" (byte-stable canonical JSON).
 * Parsing is strict: unknown keys are rejected, closed vocabularies are
 * pinned, and SystemFact content without machine-kind evidence is refused
 * so a hand-forged document cannot impersonate a runtime fact.
 ***************************************************************************/
#pragma once

#include "explain/explain_provenance.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::explain
{

constexpr const char *StepExplanationSchemaV1 = "exp.step_explanation.v1";

struct SourceReference
{
  std::string title;
  std::string kind; // textbook | paper | standard | doc
  std::string locator;
  std::string note;

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct GroundedText
{
  std::string text;
  FactProvenance provenance = FactProvenance::InferredExplanation;
  std::vector<EvidenceLink> evidence;

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct ParameterRationale
{
  std::string parameter;
  Json::Value chosenValue; // null when not bound at explanation time
  std::string rationale;
  std::string misconfigurationConsequence;
  FactProvenance provenance = FactProvenance::AuthoredGuidance;
  std::vector<EvidenceLink> evidence;

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct StateTransition
{
  std::string aspect; // radiometric_state | crs | resolution | band_count
  std::string before; // state token, or "mixed"/"" when unresolvable
  std::string after;
  std::string explanation;
  FactProvenance provenance = FactProvenance::InferredExplanation;
  std::vector<EvidenceLink> evidence;

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct SkippedStepConsequence
{
  std::string summary;
  std::string detail;
  std::vector<std::string> downstreamRoles;
  FactProvenance provenance = FactProvenance::AuthoredGuidance;
  std::vector<EvidenceLink> evidence;

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct ExecutionFacts
{
  std::string status;
  std::optional<long long> elapsedMs;
  std::optional<bool> cacheHit;
  std::string artifactPath;
  std::string artifactDigest;
  std::string startedUtc;
  std::string endedUtc;
  std::string errorMessage;
  std::vector<EvidenceLink> evidence; // machine-kind required

  Json::Value toJson() const;
  bool fromJson( const Json::Value &value, std::string &error );
};

struct StepExplanation
{
  std::string schemaVersion = StepExplanationSchemaV1;
  std::string workflowKind;
  std::string workflowId;
  std::string stepId;
  std::string stepTitle;
  std::string stepKind;
  std::string operatorId;
  std::string operatorDisplayName;

  std::vector<GroundedText> purpose;
  std::vector<GroundedText> prerequisites;
  std::vector<GroundedText> scientificAssumptions;
  std::vector<StateTransition> stateChanges;
  std::vector<ParameterRationale> parameterRationale;
  std::optional<SkippedStepConsequence> skipConsequence;
  std::optional<ExecutionFacts> execution;
  std::vector<EvidenceLink> evidenceLinks;
  std::vector<SourceReference> sourceReferences;
  std::vector<std::string> trustNotes;

  Json::Value toJson() const;
  // Strict: returns false and fills @p error on any schema violation.
  bool fromJson( const Json::Value &value, std::string &error );
};

// Canonical, byte-stable serialization (compact separators, sorted object
// members). Identical explanations always serialize to identical bytes.
std::string stepExplanationToCanonicalJson( const StepExplanation &explanation );

} // namespace sicnu::explain
