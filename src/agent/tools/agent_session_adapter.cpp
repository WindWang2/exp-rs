// src/agent/tools/agent_session_adapter.cpp
#include "agent_session_adapter.h"

#include "agent/harness/harness_verification.h"

namespace sicnu::agent {
namespace {

/// Plan output expectations for one artifact path: matched by the output
/// document's `name`/`from_step` path or, in the loop's minimal wire shape,
/// by an output entry that names the path directly. Everything undeclared
/// falls back to the structural defaults (fail-closed: an undeclared
/// artifact still goes through every default check).
sicnu::agent::harness::VerificationExpectations
expectationsFor( const sicnu::agent_loop::PlanDraft &plan, const std::string &path )
{
    using sicnu::agent::harness::VerificationExpectations;
    VerificationExpectations expectations;
    expectations.requireProvenance = false; // loop deliveries carry no sidecar contract yet
    expectations.requireNonEmpty = true;

    for ( const Json::Value &output : plan.outputs )
    {
        if ( !output.isObject() )
            continue;
        const std::string kind = output.get( "kind", "" ).asString();
        if ( kind == "raster" || kind == "vector" || kind == "map" )
            expectations.kind = kind;
        // A plan output may pin a CR authority string.
        const Json::Value crs = output.get( "crs", Json::Value( Json::nullValue ) );
        if ( crs.isString() && !crs.asString().empty() )
            expectations.crs = crs.asString();
        break; // minimal shape: the first declared output describes the artifact
    }
    ( void )path;
    return expectations;
}

} // namespace

sicnu::agent_loop::VerificationReport
HarnessVerifier::verify( const sicnu::agent_loop::PlanDraft &plan,
                         const sicnu::agent_loop::ExecutionOutcome &outcome )
{
    using namespace sicnu::agent_loop;
    using sicnu::agent::harness::aggregateVerdict;
    using sicnu::agent::harness::verifyArtifact;
    using sicnu::agent::harness::Verdict;

    VerificationReport report;
    for ( const std::string &path : outcome.artifacts )
    {
        const auto artifact = verifyArtifact( path, expectationsFor( plan, path ) );
        ArtifactVerificationReport entry;
        entry.path = artifact.path;
        switch ( artifact.verdict )
        {
            case Verdict::Pass:
                entry.verdict = "PASS";
                break;
            case Verdict::PassWithWarnings:
                entry.verdict = "PASS_WITH_WARNINGS";
                break;
            case Verdict::Fail:
            default:
                entry.verdict = "FAIL";
                break;
        }
        for ( const auto &check : artifact.checks )
        {
            if ( check.passed )
                continue;
            // Failed checks become the report's warnings/notes: an
            // error-class check keeps the verdict FAIL (never-success),
            // warning-class ones downgrade to PASS_WITH_WARNINGS upstream.
            const std::string line = check.code.empty() ? check.check
                                                        : ( check.code + ": " + check.check );
            entry.warnings.push_back( line );
        }
        report.artifacts.push_back( entry );
    }

    report.verdictValue = VerificationReport::aggregate( report.artifacts );
    return report;
}

} // namespace sicnu::agent
