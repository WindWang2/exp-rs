// tests/test_verifier_levels.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice L: the two verification
// levels. Level 1 judges one node's postcondition; level 2 folds node
// reports into the whole-task outcome under the no-swap rule (node
// Indeterminate is never swamped by a passing task body).
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
#include "verify/verify_levels.h"
#include "verify/verify_types.h"

using namespace sicnu::verify;

namespace
{

class FakeMetricView : public IMetricView
{
  public:
    std::map<std::string, double> metrics;

    std::optional<double> metric( const std::string &name ) override
    {
        const auto found = metrics.find( name );
        if ( found == metrics.end() )
            return std::nullopt;
        return found->second;
    }
};

VerificationContext contextForMetrics( FakeMetricView &metrics )
{
    return VerificationContext{ nullptr, nullptr, nullptr, nullptr, &metrics };
}

VerificationSpec metricSpec( const std::string &id, const std::string &scope, const std::string &metric,
                             double max )
{
    VerificationSpec spec;
    spec.specId = id;
    spec.scope = scope;
    Json::Value params( Json::objectValue );
    params["metric"] = metric;
    params["max"] = max;
    VerificationCheckSpec check;
    check.checkId = "check." + id;
    check.kind = "metric.range";
    check.params = params;
    spec.checks.push_back( check );
    return spec;
}

} // namespace

TEST_CASE( "level 1 refuses a task-scope spec as a node postcondition",
           "[verify][levels][L]" )
{
    const VerificationSpec taskSpec = metricSpec( "spec.task", "task", "m", 1.0 );
    const VerificationContext context;
    const VerificationReport report = verifyPlanNodePostcondition( taskSpec, context );

    REQUIRE( report.checks.size() == 1 );
    REQUIRE( report.checks[0].checkId == "spec.valid" );
    REQUIRE( report.checks[0].status == VerificationStatus::Fail );
    REQUIRE( report.checks[0].code == kCodeInvalidSpec );
    REQUIRE( report.overall == VerificationStatus::Fail );
}

TEST_CASE( "level 1 judges a node-scope spec through the engine", "[verify][levels][L]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 0.4;
    const VerificationSpec nodeSpec = metricSpec( "spec.node", "node", "ndvi_mean", 1.0 );

    const VerificationReport pass = verifyPlanNodePostcondition( nodeSpec, contextForMetrics( metrics ) );
    REQUIRE( pass.overall == VerificationStatus::Pass );

    metrics.metrics["ndvi_mean"] = 2.0;
    const VerificationReport fail = verifyPlanNodePostcondition( nodeSpec, contextForMetrics( metrics ) );
    REQUIRE( fail.overall == VerificationStatus::Fail );
    REQUIRE( fail.checks[0].code == kCodeMetricOutOfRange );
}

TEST_CASE( "level 2 folds node reports under the no-swap rule", "[verify][levels][L]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 0.4;
    metrics.metrics["task_metric"] = 0.1;
    const VerificationContext context = contextForMetrics( metrics );

    const VerificationSpec taskSpec = metricSpec( "spec.task", "task", "task_metric", 1.0 );
    const VerificationSpec nodeOk = metricSpec( "spec.node.ok", "node", "ndvi_mean", 1.0 );

    const VerificationReport nodePass = verifyPlanNodePostcondition( nodeOk, context );
    const TaskOutcome outcome = verifyWholeTask( taskSpec, context, { nodePass } );
    REQUIRE( outcome.overall == VerificationStatus::Pass );
    REQUIRE( outcome.nodes.size() == 1 );
    REQUIRE( outcome.nodes[0].overall == VerificationStatus::Pass );
    REQUIRE_FALSE( outcome.nodes[0].reportDigest.empty() );
    REQUIRE( outcome.taskChecks.size() == 1 );
}

TEST_CASE( "level 2: a passing task body never swaps away a node Indeterminate",
           "[verify][levels][L]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 0.4;
    metrics.metrics["task_metric"] = 0.1;
    const VerificationContext context = contextForMetrics( metrics );

    const VerificationSpec taskSpec = metricSpec( "spec.task", "task", "task_metric", 1.0 );
    const VerificationSpec nodeUnknown = metricSpec( "spec.node.unknown", "node", "unrecorded", 1.0 );
    const VerificationReport nodeReport = verifyPlanNodePostcondition( nodeUnknown, context );
    REQUIRE( nodeReport.overall == VerificationStatus::Indeterminate );

    const TaskOutcome outcome = verifyWholeTask( taskSpec, context, { nodeReport } );
    REQUIRE( outcome.overall == VerificationStatus::Indeterminate );
    REQUIRE( outcome.taskChecks[0].status == VerificationStatus::Pass );
}

TEST_CASE( "level 2: a node Fail fails the task even with a passing task body",
           "[verify][levels][L]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 2.0; // violates the node spec
    metrics.metrics["task_metric"] = 0.1;
    const VerificationContext context = contextForMetrics( metrics );

    const VerificationSpec taskSpec = metricSpec( "spec.task", "task", "task_metric", 1.0 );
    const VerificationSpec nodeBad = metricSpec( "spec.node.bad", "node", "ndvi_mean", 1.0 );
    const TaskOutcome outcome = verifyWholeTask( taskSpec, context,
                                                 { verifyPlanNodePostcondition( nodeBad, context ) } );
    REQUIRE( outcome.overall == VerificationStatus::Fail );
}

TEST_CASE( "level 2 refuses a node-scope spec as the whole-task judge",
           "[verify][levels][L]" )
{
    const VerificationSpec nodeSpec = metricSpec( "spec.node", "node", "m", 1.0 );
    const VerificationContext context;
    const TaskOutcome outcome = verifyWholeTask( nodeSpec, context, {} );
    REQUIRE( outcome.overall == VerificationStatus::Fail );
    REQUIRE( outcome.taskChecks.size() == 1 );
    REQUIRE( outcome.taskChecks[0].checkId == "spec.valid" );
    REQUIRE( outcome.taskChecks[0].code == kCodeInvalidSpec );
}

TEST_CASE( "level 2 outcome digest is deterministic and tamper-evident",
           "[verify][levels][L]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 0.4;
    metrics.metrics["task_metric"] = 0.1;
    const VerificationContext context = contextForMetrics( metrics );

    const VerificationSpec taskSpec = metricSpec( "spec.task", "task", "task_metric", 1.0 );
    const VerificationSpec nodeOk = metricSpec( "spec.node.ok", "node", "ndvi_mean", 1.0 );
    const VerificationReport nodeReport = verifyPlanNodePostcondition( nodeOk, context );

    const TaskOutcome first = verifyWholeTask( taskSpec, context, { nodeReport } );
    const TaskOutcome second = verifyWholeTask( taskSpec, context, { nodeReport } );
    REQUIRE( first.digest() == second.digest() );
    REQUIRE_FALSE( first.digest().empty() );

    // Re-evaluating the node with a diverging fact changes the outcome digest.
    metrics.metrics["ndvi_mean"] = 2.0;
    const VerificationReport diverged = verifyPlanNodePostcondition( nodeOk, context );
    const TaskOutcome changed = verifyWholeTask( taskSpec, context, { diverged } );
    REQUIRE( changed.digest() != first.digest() );
}
