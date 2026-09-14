/***************************************************************************
 * scientific_contract.h — Scientific Contract Registry (Platform 10.0)
 *
 * Machine-readable SCIENTIFIC semantic contract for every first-party
 * `rs:` operator. One authority per dimension:
 *
 *   - Dimensions with NO existing authority anywhere (numeric domain,
 *     scale/offset policy, NoData semantics, categorical encoding, class-id
 *     range, time alignment, wavelength requirements, seed policy,
 *     cancellation granularity, atomic publication, provenance expectation)
 *     are DECLARED here — this file is their first and only truth.
 *   - Dimensions that ALREADY have an authority (modality, band roles,
 *     determinism grade, memory policy, io parameters: the capability
 *     sidecars `data/processing/algorithm_meta/capability/` and the live
 *     operator schema) are NOT duplicated here. The drift gates
 *     (tests/test_drift_projection_10.cpp) cross-check those existing
 *     sources against each other instead.
 *
 * Completeness is enforced mechanically against the live registry: every
 * registered `rs:` id must have a record here and every record must name a
 * registered operator (tests/test_scientific_contract_10.cpp).
 *
 * Plain C++20 + jsoncpp, no Qt — same discipline as the rest of
 * src/contracts. Canonical serialization schema: "exp.scientific_contract.v1".
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::contracts {

struct ScientificContract
{
    std::string operatorId;

    /// Numeric semantic domain of the raster the operator CONSUMES and
    /// PRODUCES (vocabulary: kNumericDomains).
    std::string inputDomain;
    std::string outputDomain;

    /// How scale/offset enter the numeric mapping (kScaleOffsetPolicies).
    std::string scaleOffset;

    /// NoData / unreadable-sample semantics (kNoDataPolicies).
    std::string noDataPolicy;

    /// Categorical output encoding, "none" for continuous operators
    /// (kCategoricalEncodings).
    std::string categoricalEncoding;

    /// Enclosed class-id range for categorical outputs, "" when none
    /// (pattern "[0-9]+\\.\\.[0-9]+").
    std::string classIdRange;

    /// Time-axis contract (kTimeAlignments).
    std::string timeAlignment;

    /// Spectral requirement beyond band indices (kWavelengthPolicies).
    std::string wavelengthPolicy;

    /// Stochasticity control (kSeedPolicies).
    std::string seedPolicy;

    /// Finest granularity at which cancellation is honoured
    /// (kCancellationGranularities).
    std::string cancellationGranularity;

    /// Output publication contract (kAtomicPublications).
    std::string atomicPublication;

    /// Provenance the caller can rely on (kProvenanceExpectations).
    std::string provenance;

    /// Canonical failure/refusal codes this operator emits (subset of the
    /// harness error taxonomy; drift-checked against the live error census).
    std::vector<std::string> refusalCodes;

    /// What anchors this record: "test:<suite>", "review:<finding/track>",
    /// "family:<family> + schema read" — a declaration without an anchor is
    /// a review finding.
    std::string evidence;

    /// Optional one-line human note for surprising values (kept out of the
    /// mechanical vocabulary so honesty never bends a closed list).
    std::string note;
};

/// Closed vocabularies. Values are wire-stable (frozen JSON strings);
/// extending them is a conscious contract update.
extern const std::vector<std::string> kNumericDomains;
extern const std::vector<std::string> kScaleOffsetPolicies;
extern const std::vector<std::string> kNoDataPolicies;
extern const std::vector<std::string> kCategoricalEncodings;
extern const std::vector<std::string> kTimeAlignments;
extern const std::vector<std::string> kWavelengthPolicies;
extern const std::vector<std::string> kSeedPolicies;
extern const std::vector<std::string> kCancellationGranularities;
extern const std::vector<std::string> kAtomicPublications;
extern const std::vector<std::string> kProvenanceExpectations;

/// The registry: first-party `rs:` operator id → scientific contract.
/// Sorted by id; the map is built once on first use.
const std::map<std::string, ScientificContract> &scientificContracts();

/// @returns the record for @p operatorId, or nullptr when absent.
const ScientificContract *findScientificContract( const std::string &operatorId );

/// Structural validation against the closed vocabularies.
/// @returns an empty vector when the record is well-formed, else one
/// human-readable reason per violation (deterministic order).
std::vector<std::string> validateScientificContract( const ScientificContract &contract );

/// Canonical JSON projection (schema "exp.scientific_contract.v1").
Json::Value scientificContractToJson( const ScientificContract &contract );

/// Inverse of scientificContractToJson; false + @p error on schema mismatch.
bool scientificContractFromJson( const Json::Value &json, ScientificContract &out,
                                 std::string &error );

/// Deterministic JSON document of the WHOLE registry (array, id-sorted) for
/// diffing and downstream tooling.
Json::Value scientificContractsToJson();

} // namespace sicnu::contracts
