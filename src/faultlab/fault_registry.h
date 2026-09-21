// fault_registry.h — the fault-family catalog: the closed vocabulary the
// scenario schema validates against, plus the metadata that makes families
// machine-usable (domain, default expected diagnosis, observables each
// family is expected to move, closed param vocabulary, sandbox class).
//
// This registry is DATA, not logic: applying a fault lives in
// fault_transforms.cpp. Keeping the catalog separate lets agents enumerate
// families (faultFamilyCatalog) before running anything, and lets the
// schema gate reject unknown families without any transform code.
//
// Diagnosis expectations reference the EXISTING lab diagnostic signatures
// (src/agent/harness/lab_diagnostics.cpp): all_negative_index, all_nodata,
// kappa_near_zero, blank_change_mask, crs_mismatch, scale_stripes. Families
// with no canonical signature declare `diagnostic.unmatched` — a typed
// "no existing signature covers this" value, never a silent fallback.
#pragma once

#include <string>
#include <vector>

namespace sicnu::faultlab
{

enum class FaultDomain
{
    Metadata,
    Geometry,
    Temporal,
    Ml,
    Artifact,
};

const char *faultDomainName( FaultDomain domain );

/// Signature id used when no canonical lab diagnostic covers the family.
inline constexpr const char *kDiagnosisUnmatched = "diagnostic.unmatched";

struct FaultFamilyInfo
{
    std::string id;       ///< stable family id ("band_role_swap")
    std::string titleEn;  ///< English title
    std::string titleZh;  ///< Chinese title
    FaultDomain domain = FaultDomain::Metadata;
    std::string severity;                        ///< "major" | "moderate"
    std::vector<std::string> defaultObjectiveIds; ///< suggested learning objectives
    std::string expectedDiagnosisSignature;       ///< default expected diagnosis (scenario may override)
    std::vector<std::string> expectedObservableIds; ///< observables this family must move
    std::vector<std::string> requiredParams;      ///< closed param vocabulary (required)
    std::vector<std::string> optionalParams;      ///< closed param vocabulary (optional)
    std::string sandboxClass = "temp_copy";       ///< sandbox policy for this family
    bool stochastic = false;                      ///< true when the transform consumes the seed

    bool hasParam( const std::string &name ) const;
};

/// Deterministic declaration order; stable across runs.
const std::vector<FaultFamilyInfo> &faultFamilyCatalog();

/// nullptr when the id is not registered (caller emits
/// `faultlab.fault_unknown_family`).
const FaultFamilyInfo *findFaultFamily( const std::string &id );

} // namespace sicnu::faultlab
