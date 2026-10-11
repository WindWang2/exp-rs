/***************************************************************************
  providers.h — the five narrow seams through which facts enter the core

  This header is the ONLY place the verification core touches the outside
  world, and it touches it through pure-virtual interfaces returning plain data.
  No file paths, no handles, no Qt types, no storage clients: the core never
  opens anything and never waits for anything.

  Why bother with five separate interfaces instead of one "give me facts"
  callback:

    1. Each can be satisfied by exactly ONE existing source in the repo, so the
       wiring later is an adapter rather than a negotiation. A single
       catch-all interface invites every caller to implement all of it
       partial way, and "partially implemented" reads as "Missing", which is
       indistinguishable from "there genuinely is no such fact".
    2. An unimplemented provider is a compile-time fact, not a runtime
       surprise. A caller that has no provenance source cannot accidentally
       report an empty completeness set as "complete".
    3. Availability is per-fact, per-request. Two facts asked of the same source
       at the same moment can legitimately differ (the sidecar exists, the
       CRS string inside it does not).

  The signatures deliberately return Availability rather than throwing or
  returning a sentinel value: every non-answer must be classified, because
  Missing and Refused drive different failure codes
  (VERIFY.EVIDENCE_UNAVAILABLE vs VERIFY.EVIDENCE_REFUSED) and a check reading
  either one becomes Indeterminate. See availability.h for why that matters.

  @p reason carries the provider's own explanation and is meaningful mainly for
  Refused; it lands in evidence.details so the human report can quote it.
 ***************************************************************************/
#pragma once

#include "verification/availability.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

/// Declared scientific state of an input. Empty means UNDECLARED, which is a
/// legitimate and common condition — and crucially it is not the same as
/// "declared wrongly". Only the second may be called a violation.
struct StateSnapshot
{
    std::string numericDomain;    ///< e.g. "linear" | "db"      ("" = undeclared)
    std::string radiometricState; ///< e.g. "sigma0" | "gamma0"  ("" = undeclared)

    bool hasNumericDomain() const { return !numericDomain.empty(); }
    bool hasRadiometricState() const { return !radiometricState.empty(); }

    Json::Value toJson() const
    {
        Json::Value json{ Json::objectValue };
        json["numeric_domain"] = numericDomain;
        json["radiometric_state"] = radiometricState;
        return json;
    }
};

/// Deliberately mirrors `experiment::EvidenceCompleteness::Dimension` in shape
/// (name / present / detail) so the adapter that eventually fills it moves
/// fields rather than reinterpreting them.
struct ProvenanceDimension
{
    std::string name;
    bool present = false;
    std::string detail;

    Json::Value toJson() const
    {
        Json::Value json{ Json::objectValue };
        json["name"] = name;
        json["present"] = present;
        json["detail"] = detail;
        return json;
    }
};

struct MetricValue
{
    double value = 0.0;
    std::string unit;

    Json::Value toJson() const
    {
        Json::Value json{ Json::objectValue };
        json["value"] = value;
        json["unit"] = unit;
        return json;
    }
};

/// An identity claim about a run. `algorithm` is mandatory to compare: two
/// digests produced by different algorithms are UNCOMPARABLE, which is a third
/// state — not equal and not unequal. See checks_reproducibility.
struct DigestRecord
{
    std::string algorithm;   ///< e.g. "sha256"
    std::string digest;      ///< lowercase hex

    Json::Value toJson() const
    {
        Json::Value json{ Json::objectValue };
        json["algorithm"] = algorithm;
        json["digest"] = digest;
        return json;
    }
};

class StateProvider
{
  public:
    virtual ~StateProvider() = default;
    virtual Availability tryGet( const std::string &subjectId, StateSnapshot &out,
                                 std::string &reason ) const = 0;
};

class ArtifactProvider
{
  public:
    virtual ~ArtifactProvider() = default;
    /// @p facts is keyed by the closed `kFactKeys` vocabulary
    /// (kind / numeric_domain / band_count / extent / crs_authid / ...) so the
    /// verifier never invents a second set of names for the same things.
    virtual Availability describe( const std::string &artifactRef, Json::Value &facts,
                                   std::string &reason ) const = 0;
};

class MetricProvider
{
  public:
    virtual ~MetricProvider() = default;
    virtual Availability metric( const std::string &metricName, MetricValue &out,
                                 std::string &reason ) const = 0;
};

class ProvenanceProvider
{
  public:
    virtual ~ProvenanceProvider() = default;
    /// Returns every dimension it knows about, including the absent ones. A
    /// dimension not mentioned is NOT evidence of absence: it is unknown.
    virtual Availability completeness( const std::string &subjectId,
                                       std::vector<ProvenanceDimension> &out,
                                       std::string &reason ) const = 0;
};

class DigestProvider
{
  public:
    virtual ~DigestProvider() = default;
    virtual Availability record( const std::string &subjectId, DigestRecord &out,
                                 std::string &reason ) const = 0;
};

/// Everything a run may read. Unset members are nullptr, which the runner maps
/// to Missing rather than to an empty value — asking for a fact through a
/// provider that was never wired must not look like "the answer was empty".
struct VerificationInputs
{
    const StateProvider *state = nullptr;
    const ArtifactProvider *artifact = nullptr;
    const MetricProvider *metric = nullptr;
    const ProvenanceProvider *provenance = nullptr;
    const DigestProvider *digest = nullptr;

    /// Human-legible id of the whole provider bundle, recorded on every
    /// evidence record so a report can answer "who told you that?".
    std::string sourceId = "unwired";
};

} // namespace sicnu::verification
