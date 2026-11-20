// test_r35_t04_promotion_admission.cpp — R35-T04 WP16: a wrong-TYPED
// promotion request field is not an omission. QJsonValue::toArray() returns
// an empty array for strings/objects/numbers, which used to erase the whole
// criteria rule set — evaluate() folds eligibility over all_of(empty) ==
// vacuously true, so eligibility was decided by NO rule. The parse gate
// refuses wrong-typed arrays (and non-string benchmark entries); omission
// keeps the documented empty default; thresholds stay numeric-only.
#include <catch2/catch_test_macros.hpp>

#include "experiment/promotion.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

using namespace sicnu::experiment;

namespace
{
QJsonObject legalRequestJson()
{
    QJsonObject json;
    json.insert( QStringLiteral( "run_id" ), QStringLiteral( "run-7" ) );
    json.insert( QStringLiteral( "model_id" ), QStringLiteral( "model-1" ) );
    json.insert( QStringLiteral( "model_digest" ), QStringLiteral( "deadbeef" ) );
    QJsonArray criteria;
    QJsonObject criterion;
    criterion.insert( QStringLiteral( "metric" ), QStringLiteral( "val.f1" ) );
    criterion.insert( QStringLiteral( "min_value" ), 0.8 );
    criteria.append( criterion );
    json.insert( QStringLiteral( "criteria" ), criteria );
    QJsonArray benchmarks;
    benchmarks.append( QStringLiteral( "ds-v3" ) );
    json.insert( QStringLiteral( "benchmark_dataset_versions" ), benchmarks );
    return json;
}

bool parses( const QJsonObject &json )
{
    return PromotionRequest::fromJson( json ).isOk();
}
} // namespace

TEST_CASE( "promotion request round-trips a legal request", "[promotion][r35t04]" )
{
    const auto parsed = PromotionRequest::fromJson( legalRequestJson() );
    REQUIRE( parsed.isOk() );
    const PromotionRequest &request = parsed.value();
    REQUIRE( request.runId == QStringLiteral( "run-7" ) );
    REQUIRE( request.criteria.size() == 1 );
    REQUIRE( request.criteria.front().metric == QStringLiteral( "val.f1" ) );
    REQUIRE( request.criteria.front().minValue == 0.8 );
    REQUIRE( request.benchmarkDatasetVersions.size() == 1 );
    REQUIRE( request.benchmarkDatasetVersions.front() == QStringLiteral( "ds-v3" ) );
}

TEST_CASE( "wrong-typed criteria can no longer erase the rule set",
           "[promotion][r35t04]" )
{
    QJsonObject json = legalRequestJson();

    SECTION( "criteria as string" )
    {
        json.insert( QStringLiteral( "criteria" ), QStringLiteral( "val.f1 >= 0.8" ) );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "criteria as object" )
    {
        QJsonObject single;
        single.insert( QStringLiteral( "metric" ), QStringLiteral( "val.f1" ) );
        json.insert( QStringLiteral( "criteria" ), single );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "criteria as number" )
    {
        json.insert( QStringLiteral( "criteria" ), 1 );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "criteria as null (null is not omission)" )
    {
        json.insert( QStringLiteral( "criteria" ), QJsonValue() );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "benchmark_dataset_versions as string" )
    {
        json.insert( QStringLiteral( "benchmark_dataset_versions" ),
                     QStringLiteral( "ds-v3" ) );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "benchmark entry as number" )
    {
        QJsonArray benchmarks;
        benchmarks.append( 3 );
        json.insert( QStringLiteral( "benchmark_dataset_versions" ), benchmarks );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "criterion threshold as string" )
    {
        QJsonArray criteria;
        QJsonObject criterion;
        criterion.insert( QStringLiteral( "metric" ), QStringLiteral( "val.f1" ) );
        criterion.insert( QStringLiteral( "min_value" ), QStringLiteral( "0.8" ) );
        criteria.append( criterion );
        json.insert( QStringLiteral( "criteria" ), criteria );
        REQUIRE_FALSE( parses( json ) );
    }
    SECTION( "missing run_id still refused" )
    {
        json.remove( QStringLiteral( "run_id" ) );
        REQUIRE_FALSE( parses( json ) );
    }
}

TEST_CASE( "omitted criteria keeps the documented empty default",
           "[promotion][r35t04]" )
{
    QJsonObject json = legalRequestJson();
    json.remove( QStringLiteral( "criteria" ) );
    json.remove( QStringLiteral( "benchmark_dataset_versions" ) );
    const auto parsed = PromotionRequest::fromJson( json );
    REQUIRE( parsed.isOk() );
    REQUIRE( parsed.value().criteria.isEmpty() );
    REQUIRE( parsed.value().benchmarkDatasetVersions.isEmpty() );
}
