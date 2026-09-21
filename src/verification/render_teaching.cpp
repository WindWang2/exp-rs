// render_teaching.cpp — see render_teaching.h for why the third verdict is
// the one this surface is built to defend.

#include "verification/render_teaching.h"

#include "verification/canonical_json.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

/// Every number that reaches a rendered surface goes through here. A count is a
/// number like any other: 0.1 + 0.2 and 0.3 must not render as different text.
std::string countText( std::size_t value )
{
    return roundSignificant( static_cast<double>( value ) );
}

bool carriesNothing( const Json::Value &value )
{
    if ( value.isNull() )
    {
        return true;
    }
    if ( value.isString() && value.asString().empty() )
    {
        return true;
    }
    if ( value.isObject() && value.empty() )
    {
        return true;
    }
    if ( value.isArray() && value.empty() )
    {
        return true;
    }
    return false;
}

/// Compact rendering of an evidence value. canonicalJson sorts members and
/// pins doubles to 12 significant digits, so the same evidence always renders
/// as the same sentence. A value it refuses (non-finite) renders as nothing,
/// and the caller's fallback then says so instead of printing "nan".
std::string renderValue( const Json::Value &value )
{
    if ( carriesNothing( value ) )
    {
        return std::string();
    }
    if ( value.isString() )
    {
        return value.asString();
    }
    std::string text;
    std::string error;
    if ( !canonicalJson( value, text, error ) )
    {
        return std::string();
    }
    return text;
}

std::string joinedCodes( const std::vector<std::string> &codes )
{
    std::string joined;
    for ( std::size_t index = 0; index < codes.size(); ++index )
    {
        if ( index != 0 )
        {
            joined += ", ";
        }
        joined += codes[ index ];
    }
    return joined;
}

/// Appends the code list and — when there is one — the closed table's own
/// explanation, so the sentence is actionable rather than merely labelled.
std::string explainCodes( const std::vector<std::string> &codes )
{
    if ( codes.empty() )
    {
        return ".";
    }
    std::string text = " (" + joinedCodes( codes ) + ").";
    const std::string hint = failureHintForCode( codes.front() );
    if ( !hint.empty() )
    {
        text += " " + hint;
    }
    return text;
}

std::string observedText( const CheckResult &result )
{
    const std::string rendered = renderValue( result.evidence.observed );
    switch ( result.evidence.coverage )
    {
        case EvidenceCoverage::Full:
            return rendered.empty() ? "no observed value was recorded for this check" : rendered;
        case EvidenceCoverage::Sampled:
            // An estimate is never promoted to a measurement: the sentence has
            // to say so, or 1000 sampled pixels read as the whole raster.
            return rendered.empty()
                       ? "part of the data was examined but no observed value was recorded"
                       : rendered + " — sampled: an estimate over part of the data, not a "
                                    "measurement of the whole";
        case EvidenceCoverage::Unavailable:
            return "nothing was observed: the evidence this check needs is unavailable";
    }
    return "nothing was observed: the evidence this check needs is unavailable";
}

std::string expectedText( const CheckResult &result )
{
    const std::string rendered = renderValue( result.evidence.expected );
    return rendered.empty() ? "no expected value was declared for this check" : rendered;
}

std::string whyItMattersText( const CheckResult &result )
{
    if ( !result.message.empty() )
    {
        return result.message;
    }
    if ( !result.failureCode.empty() )
    {
        return failureHintForCode( result.failureCode );
    }
    if ( result.status == CheckStatus::Pass )
    {
        return "this expectation was met, so it is not what needs your attention";
    }
    return "this expectation could not be judged, so it neither supports nor refutes the result";
}

/// Repair hint keys, most specific first. The hints object is authored by
/// whoever declared the check, so the search order is a convention, not a
/// guarantee — and every miss still has to land on a real sentence.
const char *const kFixHintKeys[] = { "how_to_fix", "fix", "repair", "hint", "suggestion" };

std::string howToFixText( const CheckResult &result )
{
    if ( result.hints.isObject() )
    {
        for ( const char *key : kFixHintKeys )
        {
            if ( !result.hints.isMember( key ) )
            {
                continue;
            }
            const Json::Value &candidate = result.hints[ key ];
            if ( candidate.isString() && !candidate.asString().empty() )
            {
                return candidate.asString();
            }
        }
    }

    if ( !result.failureCode.empty() )
    {
        const std::string hint = failureHintForCode( result.failureCode );
        switch ( replanClassForCode( result.failureCode ) )
        {
            case ReplanClass::Retry:
                return "Supply what is missing and re-run this step unchanged: " + hint;
            case ReplanClass::Replan:
                return "Repeating the run will not change this — the step or the plan has to: " +
                       hint;
            case ReplanClass::Abort:
                return "Stop and repair the verification specification before re-running: " + hint;
            case ReplanClass::None:
                return hint;
        }
    }

    if ( result.status == CheckStatus::Pass )
    {
        return "No repair is needed for this expectation.";
    }
    return "Supply the evidence this check needs and run the verification again.";
}

const char *verdictFor( CheckStatus status )
{
    switch ( status )
    {
        case CheckStatus::Pass:
            return kTeachingVerdictTrustworthy;
        case CheckStatus::Fail:
            return kTeachingVerdictNotTrustworthy;
        case CheckStatus::Indeterminate:
            return kTeachingVerdictUnverified;
    }
    return kTeachingVerdictUnverified;
}

std::string headlineFor( CheckStatus status, std::size_t violated, std::size_t undecided,
                         bool emptyReport )
{
    switch ( status )
    {
        case CheckStatus::Pass:
            return "This result can be trusted: every declared expectation was met.";
        case CheckStatus::Fail:
            return "This result is not trustworthy: " + countText( violated ) +
                   " declared expectation(s) were violated.";
        case CheckStatus::Indeterminate:
            // The third verdict exists precisely so this sentence never claims
            // success. "We could not obtain the evidence" must not read like
            // "the result is fine", because a student acting on that sentence
            // would trust a result nobody actually checked.
            return "This result cannot be judged yet: " + countText( undecided ) +
                   " of the declared expectations could not be concluded from the evidence "
                   "available.";
    }
    if ( emptyReport )
    {
        return "This result cannot be judged yet: no verification evidence was recorded.";
    }
    return "This result cannot be judged yet: " + countText( undecided ) +
           " of the declared expectations could not be concluded from the evidence available.";
}

} // namespace

CheckStatus derivedStatus( const VerificationReport &report )
{
    // The level-2 structure is authoritative whenever the report carries it,
    // and it is NOT cross-checked against the flat level: combining the two
    // would let a flat majority of fine checks mask a node that is unknown,
    // which is precisely the dilution this slice exists to catch. The flat
    // level is the fallback for a report with no node structure at all.
    if ( !report.nodes.empty() )
    {
        std::vector<CheckStatus> nodeStatuses;
        nodeStatuses.reserve( report.nodes.size() );
        for ( const NodeOutcome &node : report.nodes )
        {
            nodeStatuses.push_back( node.status );
        }
        return combineAll( nodeStatuses );
    }

    std::vector<CheckStatus> resultStatuses;
    resultStatuses.reserve( report.results.size() );
    for ( const CheckResult &result : report.results )
    {
        resultStatuses.push_back( result.status );
    }
    // Empty input is Indeterminate: zero evidence is never a pass.
    return combineAll( resultStatuses );
}

std::vector<std::string> derivedFailureCodes( const VerificationReport &report )
{
    std::vector<std::string> codes;
    for ( const CheckResult &result : report.results )
    {
        if ( result.status == CheckStatus::Pass || result.failureCode.empty() )
        {
            continue;
        }
        codes.push_back( result.failureCode );
    }
    std::sort( codes.begin(), codes.end() );
    codes.erase( std::unique( codes.begin(), codes.end() ), codes.end() );
    return codes;
}

std::vector<std::string> derivedBlockingNodes( const VerificationReport &report )
{
    std::vector<std::string> ids;
    for ( const NodeOutcome &node : report.nodes )
    {
        if ( node.status != CheckStatus::Pass )
        {
            ids.push_back( node.nodeId );
        }
    }
    return ids;
}

Json::Value TeachingCheckView::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["check_id"] = checkId;
    json["title"] = title;
    json["status"] = statusToWire( status );
    json["failure_code"] = failureCode;
    json["expected"] = expected;
    json["observed"] = observed;
    json["why_it_matters"] = whyItMatters;
    json["how_to_fix"] = howToFix;
    return json;
}

Json::Value TeachingView::toJson() const
{
    Json::Value json{ Json::objectValue };
    json["schema"] = schema;
    json["verdict"] = verdict;
    json["headline"] = headline;

    Json::Value reasonArray{ Json::arrayValue };
    for ( const std::string &reason : reasons )
    {
        reasonArray.append( reason );
    }
    json["reasons"] = reasonArray;

    Json::Value checkArray{ Json::arrayValue };
    for ( const TeachingCheckView &check : checks )
    {
        checkArray.append( check.toJson() );
    }
    json["checks"] = checkArray;

    return json;
}

TeachingView renderTeaching( const VerificationReport &report )
{
    const CheckStatus status = derivedStatus( report );
    const bool emptyReport = report.results.empty() && report.nodes.empty();

    std::size_t violated = 0;
    std::size_t undecided = 0;
    for ( const CheckResult &result : report.results )
    {
        if ( result.status == CheckStatus::Fail )
        {
            ++violated;
        }
        else if ( result.status == CheckStatus::Indeterminate )
        {
            ++undecided;
        }
    }

    TeachingView view;
    view.verdict = verdictFor( status );
    view.headline = headlineFor( status, violated, undecided, emptyReport );

    // Reasons come from the structure, not from the summary: the node (or, for
    // a flat report, the check) that actually stopped the roll-up is what the
    // student has to go and fix.
    for ( const NodeOutcome &node : report.nodes )
    {
        if ( node.status == CheckStatus::Pass )
        {
            continue;
        }
        if ( node.status == CheckStatus::Fail )
        {
            view.reasons.push_back( "Node '" + node.nodeId +
                                    "' did not meet the expectations declared for it" +
                                    explainCodes( node.failureCodes ) );
        }
        else
        {
            view.reasons.push_back( "Node '" + node.nodeId +
                                    "' could not be judged, so the result stays undecided" +
                                    explainCodes( node.failureCodes ) );
        }
    }
    if ( report.nodes.empty() )
    {
        for ( const CheckResult &result : report.results )
        {
            if ( result.status == CheckStatus::Pass )
            {
                continue;
            }
            const std::vector<std::string> codes =
                result.failureCode.empty() ? std::vector<std::string>{}
                                           : std::vector<std::string>{ result.failureCode };
            if ( result.status == CheckStatus::Fail )
            {
                view.reasons.push_back( "Check '" + result.checkId +
                                        "' did not meet what was declared for it" +
                                        explainCodes( codes ) );
            }
            else
            {
                view.reasons.push_back( "Check '" + result.checkId +
                                        "' could not be judged, so the result stays undecided" +
                                        explainCodes( codes ) );
            }
        }
    }

    if ( view.reasons.empty() )
    {
        if ( emptyReport )
        {
            view.reasons.push_back( "Nothing was verified: this report carries no check results and "
                                    "no node outcomes, so there is no evidence for or against the "
                                    "result." );
        }
        else
        {
            view.reasons.push_back( "Every declared expectation was met: " +
                                    countText( report.results.size() ) + " checks over " +
                                    countText( report.nodes.size() ) + " nodes." );
        }
    }

    for ( const CheckResult &result : report.results )
    {
        TeachingCheckView check;
        check.checkId = result.checkId;
        // The title is what the student reads; falling back to the id keeps the
        // row identifiable rather than blank.
        check.title = result.title.empty() ? result.checkId : result.title;
        check.status = result.status;
        check.failureCode = result.failureCode;
        check.expected = expectedText( result );
        check.observed = observedText( result );
        check.whyItMatters = whyItMattersText( result );
        check.howToFix = howToFixText( result );
        view.checks.push_back( std::move( check ) );
    }

    return view;
}

} // namespace sicnu::verification
