// src/verify/verify_pack.h
#pragma once

//
// Unified Scientific Verifier (ADR 0172) — composable spec packs.
//
// A pack is a named, versioned bundle of complete specs
// ("sicnu.verification.pack/1"). Packs are caller-supplied content, so the
// same discipline as specs applies: strict serde, structural validation,
// mechanical budgets (kMaxSpecsPerPack) and a digest over the canonical
// document. composePacks merges packs deterministically (specs ordered by
// specId) and REJECTS duplicate specIds instead of silently overriding —
// two packs pinning the same specId disagree about WHAT to verify, and a
// compositor must not pick a winner.
//

#include <string>
#include <vector>

#include "verify_types.h"

namespace sicnu::verify
{

struct VerifierPack
{
    int schemaVersion = 1;
    std::string packId; ///< stable identity, non-empty
    std::vector<VerificationSpec> specs;
};

/// Structural validation: pack id, budget (kMaxSpecsPerPack), per-spec
/// validateSpec, specId uniqueness. @returns an empty vector when the pack
/// is well-formed, else one human-readable reason per violation in a
/// deterministic order.
std::vector<std::string> validatePack( const VerifierPack &pack );

/// Canonical pack document (schema marker + the canonical spec documents).
Json::Value packToJson( const VerifierPack &pack );

/// Strict inverse of packToJson: unknown fields, wrong schema marker, wrong
/// types and duplicate specIds are errors. @returns false + @p error.
bool packFromJson( const Json::Value &json, VerifierPack &out, std::string &error );

/// Bounded text parse (explicit stackLimit — a pack nests full specs) +
/// packFromJson + validatePack. On schema rejection @returns false with
/// @p error; on structural rejection @returns false with @p validationErrors.
bool parsePack( const std::string &text, VerifierPack &out, std::string &error,
                std::vector<std::string> &validationErrors );

/// sha256 of the canonical pack document text.
std::string packDigest( const VerifierPack &pack );

/// Merge @p parts into one pack under @p packId. Duplicate specIds across
/// parts are a conflict: @returns false + @p error naming both sources.
/// The merged spec list is ordered by specId — byte-identical for the same
/// input set regardless of the part order.
bool composePacks( const std::string &packId, const std::vector<VerifierPack> &parts,
                   VerifierPack &out, std::string &error );

} // namespace sicnu::verify
