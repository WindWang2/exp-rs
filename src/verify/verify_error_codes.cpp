// src/verify/verify_error_codes.cpp — closed code vocabulary membership
#include "verify_error_codes.h"

#include <array>

namespace sicnu::verify
{
namespace
{

constexpr std::array<const char *, 13> kFailureCodes = {
    kCodeInvalidSpec,
    kCodeDuplicateCheckId,
    kCodeStateViolated,
    kCodeArtifactMissing,
    kCodeArtifactTooSmall,
    kCodeTypeMismatch,
    kCodeGridMismatch,
    kCodeSchemaMismatch,
    kCodeMetricOutOfRange,
    kCodeRelationViolated,
    kCodeProvenanceIncomplete,
    kCodeDigestMismatch,
    kCodeCrossOutputInconsistent,
};

constexpr std::array<const char *, 7> kIndeterminateCodes = {
    kCodeProviderMissing,
    kCodeArtifactUnreadable,
    kCodeMetricMissing,
    kCodeMetricNotFinite,
    kCodeProvenanceMissing,
    kCodeDigestUnavailable,
    kCodeEmptyInput,
};

template <std::size_t N>
bool inList( const std::array<const char *, N> &codes, const std::string &code )
{
    for ( const char *known : codes )
        if ( code == known )
            return true;
    return false;
}

} // namespace

bool isVerifierCode( const std::string &code )
{
    return inList( kFailureCodes, code ) || inList( kIndeterminateCodes, code );
}

bool isIndeterminateCode( const std::string &code )
{
    return inList( kIndeterminateCodes, code );
}

} // namespace sicnu::verify
