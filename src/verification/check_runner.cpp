// check_runner.cpp — see check_runner.h for why the four refusals matter.

#include "verification/check_runner.h"

#include "verification/checks_artifact.h"
#include "verification/checks_cross_output.h"
#include "verification/checks_numeric.h"
#include "verification/checks_provenance.h"
#include "verification/checks_relational.h"
#include "verification/checks_reproducibility.h"
#include "verification/checks_state.h"
#include "verification/evidence.h"
#include "verification/failure_codes.h"

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::verification
{

namespace
{

/// Depth of a Json::Value, iterative so a hostile document cannot exhaust the
/// stack. Returns 0 for scalars.
int jsonDepth( const Json::Value &value )
{
    int depth = 0;
    std::vector<const Json::Value *> frontier{ &value };
    while ( !frontier.empty() )
    {
        std::vector<const Json::Value *> next;
        for ( const Json::Value *current : frontier )
        {
            if ( current->isObject() )
            {
                for ( const std::string &key : current->getMemberNames() )
                {
                    next.push_back( &( *current )[ key ] );
                }
            }
            else if ( current->isArray() )
            {
                for ( Json::ArrayIndex i = 0; i < current->size(); ++i )
                {
                    next.push_back( &( *current )[ i ] );
                }
            }
        }
        if ( next.empty() )
        {
            break;
        }
        ++depth;
        frontier = std::move( next );
    }
    return depth;
}

CheckResult indeterminateResult( const VerificationCheck &check, const char *code,
                                 const std::string &message, const std::string &sourceId )
{
    CheckResult result;
    result.checkId = check.id;
    result.kind = check.kind;
    result.title = check.title;
    result.status = CheckStatus::Indeterminate;
    result.failureCode = code;
    result.message = message;
    result.hints = check.hints;
    result.evidence.kind = check.kind;
    result.evidence.coverage = EvidenceCoverage::Unavailable;
    result.evidence.sourceId = sourceId;
    return result;
}

CheckResult dispatch( const VerificationCheck &check, const VerificationInputs &inputs )
{
    CheckKind kind = CheckKind::ArtifactShape;
    if ( !checkKindFromWire( check.kind, kind ) )
    {
        // Unknown kinds are NOT skipped. A skipped check is invisible to the
        // roll-up and therefore indistinguishable from a passing one.
        return indeterminateResult( check, failure_codes::kUnsupportedCheckKind,
                                    "check kind '" + check.kind +
                                        "' is not implemented by this verifier, so nothing "
                                        "could be established about it",
                                    inputs.sourceId );
    }

    switch ( kind )
    {
        case CheckKind::StateInvariant:
            return runStateInvariantCheck( check, inputs );
        case CheckKind::ArtifactShape:
            return runArtifactShapeCheck( check, inputs );
        case CheckKind::NumericRange:
            return runNumericRangeCheck( check, inputs );
        case CheckKind::RelationalConsistency:
            return runRelationalCheck( check, inputs );
        case CheckKind::ProvenanceCompleteness:
            return runProvenanceCompletenessCheck( check, inputs );
        case CheckKind::ReproducibilityDigest:
            return runReproducibilityDigestCheck( check, inputs );
        case CheckKind::CrossOutputConsistency:
            return runCrossOutputConsistencyCheck( check, inputs );
    }

    return indeterminateResult( check, failure_codes::kUnsupportedCheckKind,
                                "check kind '" + check.kind + "' has no registered evaluator",
                                inputs.sourceId );
}

/// Which provider a required-evidence name refers to. Unwired providers are the
/// only thing that makes a whole spec unevaluable, so this mapping is closed:
/// an unrecognised name is itself a spec defect rather than something to skip.
bool requiredProviderPresent( const std::string &name, const VerificationInputs &inputs )
{
    if ( name == "state" )
    {
        return inputs.state != nullptr;
    }
    if ( name == "artifact" )
    {
        return inputs.artifact != nullptr;
    }
    if ( name == "metric" )
    {
        return inputs.metric != nullptr;
    }
    if ( name == "provenance" )
    {
        return inputs.provenance != nullptr;
    }
    if ( name == "digest" )
    {
        return inputs.digest != nullptr;
    }
    return false;
}

void addUniqueSorted( std::vector<std::string> &target, const std::string &value )
{
    if ( value.empty() )
    {
        return;
    }
    target.push_back( value );
    std::sort( target.begin(), target.end() );
    target.erase( std::unique( target.begin(), target.end() ), target.end() );
}

} // namespace

VerificationReport runSpec( const VerificationSpec &spec, const VerificationInputs &inputs,
                            const NodeCheckMap &nodeChecks )
{
    BudgetUsage usage;
    std::vector<CheckResult> results;

    if ( nodeChecks.size() > spec.budget.maxNodes )
    {
        usage.exceeded = true;
        usage.reasons.emplace_back( "node budget exceeded" );
    }

    // A spec that declares evidence it cannot possibly obtain has no verdict to
    // give. Nothing below is evaluated, because evaluating part of it would
    // produce a partial answer that looks like a complete one.
    std::vector<std::string> missingEvidence;
    for ( const std::string &name : spec.requiredEvidence )
    {
        if ( !requiredProviderPresent( name, inputs ) )
        {
            missingEvidence.push_back( name );
        }
    }

    if ( !missingEvidence.empty() )
    {
        std::string listed;
        for ( const std::string &name : missingEvidence )
        {
            if ( !listed.empty() )
            {
                listed += ", ";
            }
            listed += name;
        }
        for ( const VerificationCheck &check : spec.checks )
        {
            results.push_back( indeterminateResult(
                check, failure_codes::kEvidenceUnavailable,
                "the spec requires evidence (" + listed +
                    ") that no provider can supply, so no check was evaluated",
                inputs.sourceId ) );
            ++usage.checksUnevaluated;
        }
        usage.reasons.push_back( "required evidence unavailable: " + listed );
        usage.exceeded = true;
        return buildReport( spec, results, nodeChecks, spec.indeterminatePolicy, usage );
    }

    for ( const VerificationCheck &check : spec.checks )
    {
        // Budget: stop evaluating past the cap, and say so loudly. The checks
        // not reached are Indeterminate — reporting them as absent would let a
        // caller read the run as "everything it looked at was fine".
        if ( usage.checksEvaluated >= spec.budget.maxChecks )
        {
            results.push_back( indeterminateResult(
                check, failure_codes::kBudgetExceeded,
                "not evaluated: the spec declares more checks than its budget allows",
                inputs.sourceId ) );
            ++usage.checksUnevaluated;
            usage.exceeded = true;
            usage.reasons.emplace_back( failure_codes::kBudgetExceeded );
            continue;
        }

        if ( check.title.size() > spec.budget.maxStringChars ||
             check.id.size() > spec.budget.maxStringChars )
        {
            results.push_back( indeterminateResult(
                check, failure_codes::kSpecInvalid,
                "check id or title exceeds the declared string budget", inputs.sourceId ) );
            ++usage.checksUnevaluated;
            usage.exceeded = true;
            usage.reasons.emplace_back( failure_codes::kSpecInvalid );
            continue;
        }

        const int depth = jsonDepth( check.params );
        usage.maxDepthSeen = std::max( usage.maxDepthSeen, depth );
        if ( depth > spec.budget.maxDepth )
        {
            // Refuse before any recursion into it gets expensive. The repo has
            // already been hit twice by jsoncpp depth bombs (#1154, #1155).
            results.push_back( indeterminateResult(
                check, failure_codes::kSpecInvalid,
                "check params nest deeper than the declared depth budget", inputs.sourceId ) );
            ++usage.checksUnevaluated;
            usage.exceeded = true;
            usage.reasons.emplace_back( "depth_exceeded" );
            continue;
        }

        CheckResult result = dispatch( check, inputs );
        result.hints = check.hints.isObject() ? check.hints : Json::Value{ Json::objectValue };
        if ( result.title.empty() )
        {
            result.title = check.title;
        }

        const std::size_t bytes = evidenceBytes( result.evidence );
        usage.maxEvidenceBytes = std::max( usage.maxEvidenceBytes, bytes );
        if ( bytes > spec.budget.maxEvidenceBytes )
        {
            // A provider that hands back an enormous record is not telling us
            // more; it is exhausting the budget. The check it fed becomes
            // Indeterminate rather than being trusted on oversized input.
            results.push_back( indeterminateResult(
                check, failure_codes::kBudgetExceeded,
                "the evidence produced for this check exceeds the byte budget", inputs.sourceId ) );
            ++usage.checksUnevaluated;
            usage.exceeded = true;
            usage.reasons.emplace_back( "evidence_bytes_exceeded" );
            continue;
        }

        ++usage.checksEvaluated;
        results.push_back( std::move( result ) );
    }

    VerificationReport report =
        buildReport( spec, results, nodeChecks, spec.indeterminatePolicy, usage );

    // Zero checks is the canonical fail-open trap. The lattice already makes an
    // empty roll-up Indeterminate, but the report must also SAY why, or a
    // consumer sees "indeterminate" with no reason and may treat it as noise.
    if ( spec.checks.empty() )
    {
        addUniqueSorted( report.outcome.failureCodes, failure_codes::kNoChecks );
    }
    if ( usage.exceeded )
    {
        // Only the budget code belongs at the outcome level. A spec-level
        // invalidity is already carried by the individual result that tripped
        // it; hoisting it here too would claim the WHOLE spec was invalid.
        addUniqueSorted( report.outcome.failureCodes, failure_codes::kBudgetExceeded );
    }

    return report;
}

VerificationReport runSpecFlat( const VerificationSpec &spec, const VerificationInputs &inputs )
{
    NodeCheckMap nodeChecks;
    std::vector<std::string> ids;
    ids.reserve( spec.checks.size() );
    for ( const VerificationCheck &check : spec.checks )
    {
        ids.push_back( check.id );
    }
    const std::string nodeId = spec.specId.empty() ? std::string( "task" ) : spec.specId;
    nodeChecks[ nodeId ] = ids;
    return runSpec( spec, inputs, nodeChecks );
}

} // namespace sicnu::verification
