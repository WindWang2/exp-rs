// src/verify/verify_status.cpp — status lattice (fail-closed aggregation)
#include "verify_status.h"

namespace sicnu::verify
{

const char *statusToWire( VerificationStatus status )
{
    switch ( status )
    {
    case VerificationStatus::Pass:
        return "pass";
    case VerificationStatus::Fail:
        return "fail";
    case VerificationStatus::Indeterminate:
        return "indeterminate";
    }
    return "indeterminate";
}

const char *statusToString( VerificationStatus status )
{
    switch ( status )
    {
    case VerificationStatus::Pass:
        return "Pass";
    case VerificationStatus::Fail:
        return "Fail";
    case VerificationStatus::Indeterminate:
        return "Indeterminate";
    }
    return "Indeterminate";
}

VerificationStatus aggregateStatus( const std::vector<VerificationStatus> &statuses )
{
    // Fail dominates; otherwise Indeterminate dominates; an EMPTY input is
    // Indeterminate — "nothing verified" must never aggregate to pass.
    if ( statuses.empty() )
        return VerificationStatus::Indeterminate;
    bool anyIndeterminate = false;
    for ( const VerificationStatus status : statuses )
    {
        if ( status == VerificationStatus::Fail )
            return VerificationStatus::Fail;
        if ( status == VerificationStatus::Indeterminate )
            anyIndeterminate = true;
    }
    return anyIndeterminate ? VerificationStatus::Indeterminate : VerificationStatus::Pass;
}

} // namespace sicnu::verify
