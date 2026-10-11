// status_lattice.cpp — see status_lattice.h for the rules this encodes.
//
// Each rule below is covered by tests/test_verifier_core_14.cpp, including one
// mutation per rule (see .planning/RS14-10-unified-verifier/progress.md) so the
// gates are proven to be able to fail.

#include "verification/status_lattice.h"

#include <algorithm>

namespace sicnu::verification
{

int statusSeverity( CheckStatus status )
{
    switch ( status )
    {
        case CheckStatus::Pass:
            return 0;
        case CheckStatus::Indeterminate:
            return 1;
        case CheckStatus::Fail:
            return 2;
    }
    // Unreachable, but MSVC /W4 must not see a missing return.
    return 2;
}

const char *statusToWire( CheckStatus status )
{
    switch ( status )
    {
        case CheckStatus::Pass:
            return "pass";
        case CheckStatus::Fail:
            return "fail";
        case CheckStatus::Indeterminate:
            return "indeterminate";
    }
    return "indeterminate";
}

bool statusFromWire( const std::string &wire, CheckStatus &out )
{
    if ( wire == "pass" )
    {
        out = CheckStatus::Pass;
        return true;
    }
    if ( wire == "fail" )
    {
        out = CheckStatus::Fail;
        return true;
    }
    if ( wire == "indeterminate" )
    {
        out = CheckStatus::Indeterminate;
        return true;
    }
    return false;
}

CheckStatus combineStatus( CheckStatus left, CheckStatus right )
{
    return statusSeverity( left ) >= statusSeverity( right ) ? left : right;
}

CheckStatus combineAll( const std::vector<CheckStatus> &statuses )
{
    // Empty is the important case: no evidence, therefore Indeterminate. The
    // accumulator starts from the FIRST element rather than from Pass, because
    // seeding with Indeterminate would swallow a lone Pass and break the
    // identity case combineAll({Pass}) == Pass.
    if ( statuses.empty() )
    {
        return CheckStatus::Indeterminate;
    }

    CheckStatus accumulated = statuses.front();
    for ( std::size_t i = 1; i < statuses.size(); ++i )
    {
        accumulated = combineStatus( accumulated, statuses[ i ] );
    }
    return accumulated;
}

RollupResult rollUp( const std::vector<std::pair<std::string, CheckStatus>> &labelled,
                     IndeterminatePolicy policy )
{
    RollupResult result;
    if ( labelled.empty() )
    {
        result.status = CheckStatus::Indeterminate;
        return result;
    }

    CheckStatus accumulated = labelled.front().second;
    for ( std::size_t i = 1; i < labelled.size(); ++i )
    {
        accumulated = combineStatus( accumulated, labelled[ i ].second );
    }

    if ( policy == IndeterminatePolicy::Fail && accumulated == CheckStatus::Indeterminate )
    {
        for ( const auto &entry : labelled )
        {
            if ( entry.second == CheckStatus::Indeterminate )
            {
                result.promoted.push_back( entry.first );
            }
        }
        std::sort( result.promoted.begin(), result.promoted.end() );
        // Only promote when there is actually someone to promote. An empty
        // input has NO indeterminate check to blame, so it stays
        // Indeterminate: inventing a failure here would be the same category
        // of dishonesty as inventing a pass.
        if ( !result.promoted.empty() )
        {
            result.status = CheckStatus::Fail;
            return result;
        }
    }

    result.status = accumulated;
    return result;
}

} // namespace sicnu::verification
