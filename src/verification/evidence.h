/***************************************************************************
  evidence.h — the machine-readable carrier of "why do you say that"
  (RS14-10, Slice B)

  A verdict alone is unusable: "this raster is not trustworthy" tells nobody
  what to fix, and three consumers (a student, an Agent, a benchmark) would each
  have to re-derive their own explanation from the pipeline. So every check
  result carries structured evidence, and the same evidence bundle feeds all
  three surfaces.

  The part that is easy to get wrong is COVERAGE. A statistic computed over a
  1000-pixel sample is an estimate of the whole raster, not a fact about it.
  Reporting it as such converts "we looked at 0.1% of the image" into
  "the image has 1.2% NoData", which is indistinguishable from having measured
  every pixel. Hence:

    EvidenceCoverage::Sampled MUST carry both sample_size and population in
    `details`. An evidence record missing either one is INCOMPLETE and any check
    reading it degrades to Indeterminate — see `evidenceCompleteness()`.
    An estimate is never silently promoted to a fact.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sicnu::verification
{

enum class EvidenceCoverage
{
    Full,         ///< every unit was examined; this is a fact
    Sampled,      ///< a subset was examined; this is an estimate, see details
    Unavailable,  ///< nothing was observed at all
};

/// Wire spelling: "full" | "sampled" | "unavailable". Never nullptr.
const char *coverageToWire( EvidenceCoverage coverage );

/// Strict inverse; false outside the closed vocabulary.
bool coverageFromWire( const std::string &wire, EvidenceCoverage &out );

/// One check's complete observation record. This is the unit of evidence that
/// teaching views, agent views and graders all read.
struct VerificationEvidence
{
    /// Check kind as its wire spelling, kept as text so an unsupported kind can
    /// still describe itself rather than being dropped.
    std::string kind;

    EvidenceCoverage coverage = EvidenceCoverage::Unavailable;

    Json::Value observed{ Json::objectValue };  ///< what was actually seen
    Json::Value expected{ Json::objectValue };  ///< empty object = no pin declared
    Json::Value details{ Json::objectValue };   ///< delta / reason / sample_size / ...

    /// Which provider instance supplied this. Answerable after the fact:
    /// "who said this?" is a first-class question for any result used in a
    /// report someone else will rely on.
    std::string sourceId;

    Json::Value toJson() const;
    static bool fromJson( const Json::Value &json, VerificationEvidence &out, std::string &error );
};

/// Reasons a sampled or absent record cannot be trusted as a measurement.
/// Empty means the record is internally sufficient.
std::vector<std::string> evidenceCompleteness( const VerificationEvidence &evidence );

/// Approximate serialized size, used against Budget::maxEvidenceBytes. It is an
/// approximation on purpose: the point is to bound a hostile provider's input,
/// not to bill anyone for bytes.
std::size_t evidenceBytes( const VerificationEvidence &evidence );

} // namespace sicnu::verification
