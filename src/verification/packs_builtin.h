/***************************************************************************
  packs_builtin.h — the packs this module ships with (RS14-10)

  Three hand-written packs (structure, provenance, reproducibility) plus one
  PROJECTION: `scientificContractFor()` derives a pack from the scientific
  contract registry (src/contracts/scientific_contract.h).

  Two rules shape the derivation:

    1. It is a projection, never a second source of truth. The literals that
       land in a derived check's `params` are read out of
       `findScientificContract(operatorId)` at call time. Nothing here keeps
       its own copy of the contract table, so the registry remains the single
       authority and a contract edit moves the derived checks with it.

    2. An UNKNOWN operator is not an empty pack. Zero checks rolls up to
       VERIFY.NO_CHECKS, and "no checks" must never be read as Pass. So the
       unknown case returns `indeterminate == true` with the typed code
       VERIFY.UNSUPPORTED_CHECK_KIND AND a pack that still carries one visibly
       unevaluable check, so the absence is reported rather than disappearing.

  These functions return PACKS, never verdicts: they declare what should be
  established, they do not decide whether it holds.
 ***************************************************************************/
#pragma once

#include "verification/pack.h"

#include <string>

namespace sicnu::verification
{

/// Shape of the raster surface the operator must produce at all: present, of
/// the declared kind, on the declared grid.
VerifierPack structuralPack();

/// What the caller may rely on being recorded about how a result was made.
VerifierPack provenancePack();

/// Whether the same inputs produce the same output again.
VerifierPack reproducibilityPack();

/// A pack derived from a scientific contract, plus how the derivation went.
struct DerivedPack
{
    VerifierPack pack;
    bool indeterminate = false;  ///< true when the operator is unknown
    std::string failureCode;     ///< failure_codes::kUnsupportedCheckKind when indeterminate
    std::string reason;          ///< never empty when indeterminate
};

/// @returns the pack derived from the registry record for @p operatorId. For
///          an unregistered operator, an indeterminate DerivedPack that still
///          names the operator — never an empty, ok pack.
DerivedPack scientificContractFor( const std::string &operatorId );

} // namespace sicnu::verification
