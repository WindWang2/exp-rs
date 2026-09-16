/***************************************************************************
 * determinism_census.h — Determinism & Contract Census (Platform 11.0)
 *
 * Package A/B of the F09 track: a live, source-grounded census over every
 * first-party registered operator (rs:/gdal:/io:/otb:/opencv:/cartography:)
 * that projects — without duplicating — the three existing determinism
 * authorities plus the scientific-contract registry:
 *
 *   1. the published schema stamp (RSOperator::determinismGrade(), stamped
 *      into the operator schema by stampDeterminismGrade);
 *   2. the knowledge-layer claim (capability sidecar
 *      capability.determinism.{grade,stochastic});
 *   3. the source-level fact (does the operator class explicitly override
 *      determinismGrade()/determinism(), or does it silently fall back to
 *      the framework default?);
 *   4. contract membership (scientificContracts()) or a recorded exemption
 *      (data/contracts/contract_exemptions.json).
 *
 * Discipline (mirrors scientific_contract.h): this census is a PROJECTION
 * and a cross-check, never a second truth. The scan runs over `src/**`
 * registration sites; the runtime cross-check against the live
 * RSOperatorRegistry happens in the consuming test, which links the
 * operator library — sicnu_contracts itself stays operator-library-free
 * (same layering as graph_assembly).
 *
 * Canonical serialization schema: "exp.determinism_census.v1".
 * Plain C++20 + jsoncpp, no Qt.
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <map>
#include <string>
#include <vector>

namespace sicnu::contracts {

/// Source-level determinism facts for ONE operator id, recovered from the
/// registration site and the implementing class declaration.
struct DeterminismOverrideInfo
{
    std::string operatorId;
    std::string operatorClass;      /// implementing class name ("" = scan miss)
    std::string file;               /// file that registers the id (relative to src/)
    bool classFound = false;        /// class declaration located in the tree
    bool gradeOverride = false;     /// explicit determinismGrade() override
    std::string gradeLiteral;       /// the literal the override returns ("bit-exact"...)
    bool runtimeOverride = false;   /// explicit determinism() override
    std::string runtimeLiteral;     /// "RSOperatorDeterminism::BitExact"/"...Tolerance"
    int baseDepth = 0;              /// 0 = declared on the operator class itself,
                                    /// N = inherited from the N-th scanned ancestor
    std::string note;               /// bounded, honest scan caveats
};

/// One census row: every published/claimed/scanned determinism fact for one
/// operator id, plus its contract-coverage state.
struct DeterminismCensusEntry
{
    std::string operatorId;
    std::string prefix;             /// "rs:", "gdal:", "io:", "otb:", "opencv:", "cartography:"
    std::string schemaGrade;        /// literal recovered from the class source ("" = none)
    std::string runtimeGrade;       /// "bit_exact"/"tolerance" from scanned determinism()
                                    /// literal, or the framework default when not overridden
    std::string sidecarGrade;       /// capability.determinism.grade ("" = no sidecar/field)
    std::string sidecarStochastic;  /// "true"/"false" ("" = absent)
    bool hasScientificContract = false;
    std::string seedPolicy;         /// contract seedPolicy ("" = no contract)
    bool exempted = false;          /// contract exemption record present
    std::string exemptionReason;
    DeterminismOverrideInfo source;
};

struct DeterminismCensus
{
    /// id-sorted, one row per operator id found in the source registration
    /// scan (superset of the live registry: conditionally-compiled families
    /// such as otb:/opencv: may legitimately be absent from a given host).
    std::vector<DeterminismCensusEntry> entries;
    /// Bounded, honest assembly facts (scan misses, unreadable sidecars).
    std::vector<std::string> notes;
};

/// Scan src/** for operator registration sites (REGISTER_RS_OPERATOR macros
/// and `add("id", []{ return std::make_unique<Class>(); } )` lambdas) and
/// recover the per-class determinism override facts. Deterministic: sorted
/// by operator id; every step bounded.
std::map<std::string, DeterminismOverrideInfo> scanDeterminismOverrides(
    const std::string &sourceRoot );

/// Contract exemption records (data/contracts/contract_exemptions.json,
/// schema "exp.contract_exemptions.v1"): first-party registered operators
/// whose scientific-semantics record is deliberately an exemption instead of
/// a full contract (e.g. third-party numeric cores behind thin adapters).
struct ContractExemption
{
    std::string operatorId;
    std::string reason;
    std::string evidence;
};
/// @param error set when the file exists but is malformed (empty otherwise).
std::map<std::string, ContractExemption> loadContractExemptions(
    const std::string &sourceRoot, std::string &error );

/// Assemble the full census from source scans + the contract registry +
/// capability sidecars + exemptions. Never throws on individual row
/// problems — problems land in `notes` so a census is always producible;
/// gates decide what is disqualifying.
DeterminismCensus buildDeterminismCensus( const std::string &sourceRoot );

/// Canonical JSON projection (schema "exp.determinism_census.v1").
Json::Value determinismCensusToJson( const DeterminismCensus &census );

/// Grade-spelling bridge: "bit-exact" (schema stamp) ≡ "bit_exact"
/// (sidecar/runtime). Frozen wire formats — the census bridges, it does not
/// rewrite.
std::string normalizeDeterminismGrade( std::string grade );

} // namespace sicnu::contracts
