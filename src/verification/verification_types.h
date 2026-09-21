/***************************************************************************
  verification_types.h — the declarative value objects of the verifier core.

  These types are PURE DATA plus serialization: no I/O, no Qt, no clock, no
  threads, no global mutable state. Every one of them round-trips through JSON
  deterministically so a specification is content-addressable (see spec.h's
  specDigest) and a report can be replayed later.

  Nothing here COMPUTES a verdict: checks declare WHAT should hold; the
  checkers in checks_*.cpp and the runner in check_runner.cpp decide whether it
  does, using facts pushed in through the provider interfaces (providers.h).

  `kind` is kept as the WIRE STRING rather than as CheckKind on purpose: an
  unknown kind must survive a round-trip so the runner can later report
  VERIFY.UNSUPPORTED_CHECK_KIND instead of silently dropping the check. The
  enum stays available for dispatch and is resolved on demand.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sicnu::verification
{

/// The seven check families promised by this track. This enum is the DISPATCH
/// vocabulary; the stored spelling is always the wire string (see above).
enum class CheckKind
{
    StateInvariant,
    ArtifactShape,
    NumericRange,
    RelationalConsistency,
    ProvenanceCompleteness,
    ReproducibilityDigest,
    CrossOutputConsistency,
};

const char *checkKindToWire( CheckKind kind );
bool checkKindFromWire( const std::string &wire, CheckKind &out );
std::vector<std::string> allCheckKinds();

/// Resolves the stored wire string. Returns false for anything this build does
/// not implement — callers must turn that into an Indeterminate check carrying
/// VERIFY.UNSUPPORTED_CHECK_KIND, never into a skip.
bool checkKindOf( const struct VerificationCheck &check, CheckKind &out );

/// Resource envelope for one verification run. Every field has a default, and
/// exceeding any of them is a typed refusal rather than a silent slowdown
/// (see the resource-budget rules in plan.md section 8).
struct Budget
{
    std::size_t maxChecks = 256;
    std::size_t maxNodes = 512;
    std::size_t maxEvidenceBytes = 1024u * 1024u;
    int maxDepth = 32;
    std::size_t maxStringChars = 4096;
    std::size_t maxWitnessElements = 5000;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, Budget &out, std::string &error );
};

/// What a check is ASKED ABOUT. Deliberately a small closed shape: either a
/// node-in-plan reference, an artifact reference, a metric name, or "the whole
/// task".
struct SubjectRef
{
    std::string kind;  ///< "node" | "artifact" | "metric" | "task"
    std::string id;    ///< node id / artifact path or ref / metric name / ""

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, SubjectRef &out, std::string &error );
};

/// One declared expectation. This is NOT a result: it is what the caller wants
/// established before the result may be trusted.
struct VerificationCheck
{
    std::string id;
    std::string kind;           ///< wire spelling; unknown kinds survive round-trip
    std::string title;          ///< one human-readable sentence
    SubjectRef subject;
    Json::Value params{ Json::objectValue };       ///< kind-specific expectation
    bool required = true;
    std::string failureCode;    ///< default code to report when this check fails
    Json::Value hints{ Json::objectValue };        ///< repair hints for teaching/agent surfaces

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, VerificationCheck &out, std::string &error );
};

} // namespace sicnu::verification
