/***************************************************************************
  pack.h — composable, versioned bundles of verification checks (RS14-10)

  Why packs exist at all: a caller should not have to restate the structural,
  provenance and reproducibility expectations of every result by hand. A pack
  is a named, versioned, serializable bundle of checks that composes into a
  VerificationSpec (spec.h).

  The failure mode this file is written against is SILENT MERGING. When two
  packs declare the same check id there are three possible answers, and only
  two of them are honest:

    - "this is a conflict, say so"                     -> PackConflictPolicy::Reject
    - "this is a conflict, I resolved it, here is
       exactly which declaration I dropped"            -> PackConflictPolicy::Override

  The third — last-write-wins with no record — is indistinguishable from a
  successful merge: the caller sees one check where two were declared, the
  dropped expectation is never evaluated and never reported, and the result
  reads as Pass. That is a fail-open, so composePacks never overwrites
  silently: under Reject it returns NOTHING rather than a half-merged set, and
  under Override it records every dropped declaration in `overrides`.

  The same rule covers the empty case. A composition that yields zero checks
  is absence of evidence, which the lattice (status_lattice.h) calls
  Indeterminate and never Pass. composePacks therefore refuses it with
  VERIFY.NO_CHECKS instead of handing back an empty vector that a downstream
  roll-up would read as "nothing to complain about".

  Determinism: packs are merged in lexicographic pack-id order and each pack
  keeps its own declaration order inside that. That is what makes a composed
  spec's digest reproducible regardless of the order the caller listed packs
  in (see specDigest, which additionally sorts by check identity).
 ***************************************************************************/
#pragma once

#include "verification/verification_types.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::verification
{

/// Schema identity of a serialized pack. Readers refuse foreign values rather
/// than guessing how to reinterpret them.
inline constexpr const char *kVerifierPackSchema = "exp.verification.pack.v1";

struct VerifierPack;

/// How a composition resolves two packs declaring the same check id.
enum class PackConflictPolicy
{
    /// Refuse the merge and name the conflicting ids. Nothing is produced.
    Reject,

    /// Keep the first declaration (pack-id order) and RECORD every drop, so
    /// the merge stays traceable after the fact.
    Override,
};

/// One resolved overlap. `keptFrom` / `droppedFrom` are pack ids, never check
/// ids, so a reader can reconstruct which bundle won.
struct PackOverride
{
    std::string checkId;
    std::string keptFrom;
    std::string droppedFrom;
};

/// Result of a composition. `ok == false` means the caller MUST NOT use
/// `checks`: a refused composition carries no checks at all, because a partial
/// merge is exactly the thing that looks like success downstream.
struct ComposeOutcome
{
    bool ok = true;
    std::vector<VerificationCheck> checks;  ///< deterministic order (see above)
    std::vector<std::string> duplicateIds;  ///< ids declared by more than one pack
    std::vector<PackOverride> overrides;    ///< non-empty only under Override
    std::string failureCode;                ///< empty when ok
    std::string reason;                     ///< never empty when !ok
};

/// A named, versioned bundle of checks.
struct VerifierPack
{
    std::string id;                                   ///< e.g. "structural"
    std::string version = "1";
    std::string schema = kVerifierPackSchema;
    std::string derivedFrom;                          ///< operator id when derived, "" when hand-written
    std::vector<VerificationCheck> checks;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, VerifierPack &out, std::string &error );
};

/// Merges @p packs into one check list.
///
/// Never throws, never partially succeeds, and never overwrites a check
/// without saying so.
ComposeOutcome composePacks( const std::vector<VerifierPack> &packs, PackConflictPolicy policy );

} // namespace sicnu::verification
