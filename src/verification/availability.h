/***************************************************************************
  availability.h — why a fact could not be obtained (RS14-10, Slice B)

  Every fact the verifier uses arrives through a provider, and every provider
  answers with one of these three values. The distinction matters because the
  three must NOT collapse into the same verdict:

    Found    — here is the fact; a decision may be made.
    Missing  — no such thing was found. Nothing is known about it either way.
    Refused  — the source declined to answer, and said why.

  `Missing` and `Refused` both make the consuming check Indeterminate rather
  than Fail. That is the central repair this track exists for: today a CRS
  sidecar that could not be read because of an IO hiccup is reported as
  CRS_MISMATCH — i.e. absence of evidence is reported as wrong science, and the
  pipeline redoes five minutes of work for nothing.

  They stay separate from each other because they demand different reactions.
  `Missing` may be legitimately expected (a step that by contract produces no
  provenance); `Refused` always carries a reason and is always an operational
  problem to fix, so an operator needs to tell them apart in a report.
 ***************************************************************************/
#pragma once

#include <string>

namespace sicnu::verification
{

enum class Availability
{
    Found,
    Missing,
    Refused,
};

/// Wire spelling: "found" | "missing" | "refused". Never nullptr.
const char *availabilityToWire( Availability availability );

/// Strict inverse; returns false for anything outside the closed vocabulary.
bool availabilityFromWire( const std::string &wire, Availability &out );

/// True when a check reading this answer has no facts to reason about and must
/// therefore become Indeterminate rather than reaching a verdict.
bool unavailable( Availability availability );

} // namespace sicnu::verification
