/***************************************************************************
 * explanation_sources.h — stable fact-source interfaces for the builder
 *
 * The explain layer owns no scientific truth. Facts enter through three
 * narrow interfaces, each implemented by an adapter over an authoritative
 * store (operator registry / guidance store / provenance records) and by
 * fakes in tests. Anything the sources cannot answer stays out of the
 * explanation — absence is never upgraded into invented content.
 ***************************************************************************/
#pragma once

#include "explain/explain_provenance.h"

#include <json/json.h>

#include <optional>
#include <string>
#include <vector>

namespace sicnu::explain
{

struct ParamFact
{
  std::string name;
  std::string type;
  std::string description;
  Json::Value defaultValue; // null when none declared
  bool required = false;
  std::vector<std::string> enumValues;
  std::optional<double> minimum;
  std::optional<double> maximum;
};

struct OperatorFacts
{
  std::string id;
  std::string displayName;
  std::string group;
  std::string description;
  std::string purpose;
  std::vector<std::string> useCases;
  std::vector<std::string> prerequisites;
  std::vector<std::string> limitations;
  std::vector<std::string> workflowHints;
  std::vector<std::string> tags;
  std::vector<ParamFact> parameters;
};

// Live operator knowledge (adapter: RSOperatorRegistry).
struct IOperatorKnowledge
{
  virtual ~IOperatorKnowledge() = default;
  virtual std::optional<OperatorFacts> findOperator( const std::string &operatorId ) const = 0;
};

struct StepEvidence
{
  std::string status;
  std::optional<long long> elapsedMs;
  std::optional<bool> cacheHit;
  std::string artifactPath;
  std::string artifactDigest;
  std::string startedUtc;
  std::string endedUtc;
  std::string errorMessage;
  std::vector<EvidenceLink> links; // machine-kind links (provenance/derivation/operation_log)
};

// Per-step execution evidence (adapter: provenance records / checkpoints).
struct IExecutionEvidence
{
  virtual ~IExecutionEvidence() = default;
  virtual std::optional<StepEvidence> evidenceFor( const std::string &runId,
                                                   const std::string &stepId ) const = 0;
};

} // namespace sicnu::explain
