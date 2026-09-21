/***************************************************************************
  spec.h — the declarative verification specification (RS14-10, Slice A)

  A VerificationSpec is WHAT SHOULD BE ESTABLISHED before a result may be
  trusted. It is deliberately not a verdict: nothing here inspects data, opens
  a file, or evaluates anything. Facts arrive later, through providers, at run
  time (see providers.h).

  Because the core does no I/O and reads no clock, identical inputs must give
  identical reports. That property is what makes a report REPLAYABLE, and it
  rests on one thing: a spec must have a stable identity computed from its own
  content. Hence `specDigest()` — sha256 over the CANONICAL JSON of the spec.
  Two spellings of the same expectations (different member order, numerically
  equivalent floats) must collide to one digest; one changed character must
  produce a different one. Without that, caching, diffs and "was this the spec
  that produced this report?" all degrade into guesswork.

  There is deliberately no "empty spec means nothing to do" reading anywhere in
  this module. Zero checks is zero evidence, and the lattice in
  status_lattice.h turns that into Indeterminate — not Pass.
 ***************************************************************************/
#pragma once

#include "verification/status_lattice.h"
#include "verification/verification_types.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

/// Schema version of the declarative types below. Readers refuse foreign
/// versions instead of guessing how to reinterpret them.
inline constexpr int kVerificationSpecSchemaVersion = 1;
inline constexpr const char *kVerificationSpecSchema = "exp.verification.spec.v1";

/// A composable set of checks plus how to treat "we could not tell".
struct VerificationSpec
{
    int schemaVersion = kVerificationSpecSchemaVersion;
    std::string specId;
    std::string specVersion = "1";
    IndeterminatePolicy indeterminatePolicy = IndeterminatePolicy::Keep;
    Budget budget;
    std::vector<VerificationCheck> checks;

    /// Which evidence kinds MUST be present for anything to be concluded at
    /// all. Absence makes the whole spec Indeterminate rather than letting a
    /// partial check set pass.
    std::vector<std::string> requiredEvidence;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, VerificationSpec &out, std::string &error );
};

/// Structural validation. Structural means "shape", not "meaning": it cannot
/// tell you the expectations are scientifically sound, only that every lane
/// downstream can read them.
///
/// Notably it does NOT reject unknown check kinds — those are legitimate input
/// that must survive to become VERIFY.UNSUPPORTED_CHECK_KIND at run time — nor
/// duplicate ids, which are resolved later by pack composition policy rather
/// than being silently dropped here.
///
/// @returns empty when structurally sound, otherwise one message per problem.
std::vector<std::string> validateSpec( const VerificationSpec &spec );

/// Content address: sha256 over canonicalJson(spec.toJson()).
///
/// Returns "" if the spec cannot even be canonicalized (for example a params
/// subtree carrying NaN). A caller must treat an empty digest as "not
/// addressable" rather than as a valid identity.
std::string specDigest( const VerificationSpec &spec );

} // namespace sicnu::verification
