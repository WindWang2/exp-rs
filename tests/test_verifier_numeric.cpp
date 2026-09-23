// tests/test_verifier_numeric.cpp
//
// Unified Scientific Verifier (ADR 0172) — Slice C: metric.range,
// relational.consistency, tolerance edges and the non-finite discipline.
//
// Light target: links sicnu_verifier + Catch2 + jsoncpp only.

#include <json/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "verify/verify_engine.h"
#include "verify/verify_error_codes.h"
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

VerificationContext contextFor( FakeMetricView &metrics )
{
    return VerificationContext{ nullptr, nullptr, nullptr, nullptr, &metrics };
}

Json::Value rangeParams( const std::string &name )
{
    Json::Value params( Json::objectValue );
    params["metric"] = name;
    return params;
}

Json::Value relationsParam( Json::Value relations )
{
    Json::Value params( Json::objectValue );
    params["relations"] = std::move( relations );
    return params;
}

Json::Value relation( const std::string &op, Json::Value left, Json::Value right )
{
    Json::Value r( Json::objectValue );
    r["op"] = op;
    r["left"] = std::move( left );
    r["right"] = std::move( right );
    return r;
}

} // namespace

// ---------------------------------------------------------------------------
// metric.range
// ---------------------------------------------------------------------------

TEST_CASE( "metric.range judges bounds inclusively", "[verify][numeric][C]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_mean"] = 0.5;

    const auto range = [ & ]( Json::Value params ) {
        VerificationCheckSpec check;
        check.checkId = "m";
        check.kind = "metric.range";
        check.params = std::move( params );
        return evaluateCheck( check, contextFor( metrics ) );
    };

    Json::Value params = rangeParams( "ndvi_mean" );
    params["min"] = 0.5;
    params["max"] = 1.0;
    REQUIRE( range( params ).status == VerificationStatus::Pass );
    params["max"] = 0.5;
    REQUIRE( range( params ).status == VerificationStatus::Pass );

    // A fresh, non-contradictory pin for the violation case (min > max is a
    // validation error the engine now refuses before evaluation).
    Json::Value aboveParams = rangeParams( "ndvi_mean" );
    aboveParams["max"] = 0.4;
    const VerificationCheckResult above = range( aboveParams );
    REQUIRE( above.status == VerificationStatus::Fail );
    REQUIRE( above.code == kCodeMetricOutOfRange );
}

TEST_CASE( "metric.range tolerance extends the band but never a fake pass",
           "[verify][numeric][C]" )
{
    FakeMetricView metrics;
    metrics.metrics["rmse"] = 0.12;

    const auto range = [ & ]( double max, double tolerance ) {
        VerificationCheckSpec check;
        check.checkId = "m";
        check.kind = "metric.range";
        Json::Value params = rangeParams( "rmse" );
        params["max"] = max;
        params["tolerance"] = tolerance;
        check.params = params;
        return evaluateCheck( check, contextFor( metrics ) );
    };

    REQUIRE( range( 0.1, 0.05 ).status == VerificationStatus::Pass );   // 0.12 <= 0.15
    REQUIRE( range( 0.1, 0.019 ).status == VerificationStatus::Fail );  // 0.12 > 0.119
    REQUIRE( range( 0.1, 0.0 ).status == VerificationStatus::Fail );
}

TEST_CASE( "metric failures are indeterminate with distinct typed causes",
           "[verify][numeric][C]" )
{
    FakeMetricView metrics;
    metrics.metrics["broken"] = std::nan( "" );
    metrics.metrics["inf"] = HUGE_VAL;

    const auto range = [ & ]( const std::string &name ) {
        VerificationCheckSpec check;
        check.checkId = "m";
        check.kind = "metric.range";
        check.params = rangeParams( name );
        check.params["max"] = 1.0;
        return evaluateCheck( check, contextFor( metrics ) );
    };

    SECTION( "missing metric" )
    {
        const VerificationCheckResult result = range( "ghost" );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricMissing );
    }
    SECTION( "NaN metric" )
    {
        const VerificationCheckResult result = range( "broken" );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricNotFinite );
    }
    SECTION( "infinite metric" )
    {
        const VerificationCheckResult result = range( "inf" );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricNotFinite );
    }
    SECTION( "no metric provider at all" )
    {
        VerificationCheckSpec check;
        check.checkId = "m";
        check.kind = "metric.range";
        check.params = rangeParams( "ndvi_mean" );
        check.params["max"] = 1.0;
        const VerificationCheckResult result = evaluateCheck( check, VerificationContext{} );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeProviderMissing );
    }
}

// ---------------------------------------------------------------------------
// relational.consistency
// ---------------------------------------------------------------------------

TEST_CASE( "relational.consistency judges metric algebra", "[verify][numeric][C]" )
{
    FakeMetricView metrics;
    metrics.metrics["ndvi_red"] = 0.25;
    metrics.metrics["ndvi_nir"] = 0.75;
    metrics.metrics["ndvi"] = 1.0;

    const auto relations = [ & ]( Json::Value list ) {
        VerificationCheckSpec check;
        check.checkId = "rel";
        check.kind = "relational.consistency";
        check.params = relationsParam( std::move( list ) );
        return evaluateCheck( check, contextFor( metrics ) );
    };

    SECTION( "sum_is over metrics and literals" )
    {
        Json::Value list( Json::arrayValue );
        Json::Value sum = relation( "sum_is", "ndvi", [ & ] {
            Json::Value operands( Json::arrayValue );
            operands.append( "ndvi_red" );
            operands.append( "ndvi_nir" );
            return operands;
        }() );
        list.append( sum );
        REQUIRE( relations( list ).status == VerificationStatus::Pass );

        sum["right"][0] = 0.3;
        list[0] = sum;
        const VerificationCheckResult result = relations( list );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeRelationViolated );
    }
    SECTION( "eq with a tolerance band" )
    {
        metrics.metrics["ndvi"] = 1.001;
        Json::Value list( Json::arrayValue );
        Json::Value eq = relation( "eq", "ndvi", 1.0 );
        eq["tolerance"] = 0.01;
        list.append( eq );
        REQUIRE( relations( list ).status == VerificationStatus::Pass );
        eq["tolerance"] = 0.0001;
        list[0] = eq;
        REQUIRE( relations( list ).status == VerificationStatus::Fail );
    }
    SECTION( "ordering relations" )
    {
        Json::Value list( Json::arrayValue );
        list.append( relation( "lt", "ndvi_red", "ndvi_nir" ) );
        list.append( relation( "ge", "ndvi_red", 0.2 ) );
        REQUIRE( relations( list ).status == VerificationStatus::Pass );
        list[1] = relation( "gt", 0.25, "ndvi_red" );
        REQUIRE( relations( list ).status == VerificationStatus::Fail );
    }
    SECTION( "ne" )
    {
        Json::Value list( Json::arrayValue );
        list.append( relation( "ne", "ndvi_red", "ndvi_nir" ) );
        REQUIRE( relations( list ).status == VerificationStatus::Pass );
        list[0] = relation( "ne", "ndvi_red", 0.25 );
        REQUIRE( relations( list ).status == VerificationStatus::Fail );
    }
    SECTION( "approx requires an explicit tolerance to mean something" )
    {
        Json::Value list( Json::arrayValue );
        Json::Value approx = relation( "approx", "ndvi", 1.0 );
        approx["tolerance"] = 0.05;
        list.append( approx );
        metrics.metrics["ndvi"] = 1.02;
        REQUIRE( relations( list ).status == VerificationStatus::Pass );
    }
}

TEST_CASE( "relational failures are fail-closed across operands", "[verify][numeric][C]" )
{
    FakeMetricView metrics;
    metrics.metrics["known"] = 1.0;
    metrics.metrics["nan"] = std::nan( "" );

    const auto relations = [ & ]( Json::Value list ) {
        VerificationCheckSpec check;
        check.checkId = "rel";
        check.kind = "relational.consistency";
        check.params = relationsParam( std::move( list ) );
        return evaluateCheck( check, contextFor( metrics ) );
    };

    SECTION( "a missing operand metric is indeterminate" )
    {
        Json::Value list( Json::arrayValue );
        list.append( relation( "eq", "known", "ghost" ) );
        const VerificationCheckResult result = relations( list );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricMissing );
    }
    SECTION( "a non-finite operand metric is indeterminate" )
    {
        Json::Value list( Json::arrayValue );
        list.append( relation( "eq", "known", "nan" ) );
        const VerificationCheckResult result = relations( list );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricNotFinite );
    }
    SECTION( "fail + indeterminate folds to Fail inside one check" )
    {
        Json::Value list( Json::arrayValue );
        list.append( relation( "eq", "known", "ghost" ) );      // indeterminate
        list.append( relation( "eq", "known", 2.0 ) );          // violated -> fail
        const VerificationCheckResult result = relations( list );
        REQUIRE( result.status == VerificationStatus::Fail );
        REQUIRE( result.code == kCodeRelationViolated );
    }
    SECTION( "sum_is over a missing operand is indeterminate" )
    {
        Json::Value list( Json::arrayValue );
        Json::Value sum = relation( "sum_is", "known", [ & ] {
            Json::Value operands( Json::arrayValue );
            operands.append( "ghost" );
            return operands;
        }() );
        list.append( sum );
        const VerificationCheckResult result = relations( list );
        REQUIRE( result.status == VerificationStatus::Indeterminate );
        REQUIRE( result.code == kCodeMetricMissing );
    }
}
