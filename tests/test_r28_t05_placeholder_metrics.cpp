// tests/test_r28_t05_placeholder_metrics.cpp — R28-T05 regressions for
// #1804 sub-items WE-18 (placeholder full-string id parse), WE-24 (MAPE
// undefined under all-zero truth) and WE-25 (sparse confusion matrix with a
// declared resource budget), plus #1772/WE-26 counterpart coverage for
// planner planning_context resolution validation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "experiment/evaluation.h"
#include "planner/planning_context.h"
#include "workflow/placeholder_grammar.h"

#include <json/json.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>
#include <memory>
#include <string>

using namespace sicnu::workflow;
using namespace sicnu::experiment;

// ---------------------------------------------------------------------------
// WE-18 (#1804): ${task.<id>} must consume the whole id segment.
// ---------------------------------------------------------------------------

TEST_CASE( "malformed numeric task refs never bind a partial id (WE-18)",
           "[r28_t05][workflow][placeholder]" )
{
    // ${task.12abc}: stol would happily return 12 — the ref must NOT become a
    // parent-task binding; it falls to the step-id form (which the #1780
    // launch gate then refuses as unresolvable).
    const auto malformed = parsePlaceholders( "${task.12abc}" );
    REQUIRE( malformed.size() == 1 );
    CHECK( malformed[0].parentTaskId == -1 );
    CHECK( malformed[0].stepId == "task.12abc" );
    CHECK( malformed[0].isValid() );
    // And because it parses as a step ref, the pre-launch detector sees it.
    CHECK( containsUnresolvedStepPlaceholder( "${task.12abc}" ) );

    // The same ref with a port keeps the same policy.
    const auto malformedPort = parsePlaceholders( "${task.12abc.masked}" );
    REQUIRE( malformedPort.size() == 1 );
    CHECK( malformedPort[0].parentTaskId == -1 );
    CHECK( malformedPort[0].stepId == "task.12abc.masked" );

    // Well-formed numeric ids keep binding.
    const auto numeric = parsePlaceholders( "${task.45}" );
    REQUIRE( numeric.size() == 1 );
    CHECK( numeric[0].parentTaskId == 45 );
    CHECK( numeric[0].portName == "output" );

    const auto numericPort = parsePlaceholders( "${task.45.masked}" );
    REQUIRE( numericPort.size() == 1 );
    CHECK( numericPort[0].parentTaskId == 45 );
    CHECK( numericPort[0].portName == "masked" );
}

// ---------------------------------------------------------------------------
// WE-24 (#1804): MAPE is explicitly undefined when every truth value is 0.
// ---------------------------------------------------------------------------

TEST_CASE( "all-zero truth reports MAPE as undefined, never 0 (WE-24)",
           "[r28_t05][experiment][metrics]" )
{
    const QVector<double> truth{ 0.0, 0.0, 0.0, 0.0 };
    const QVector<double> predicted{ 1.0, 2.0, 3.0, 4.0 };
    const auto metrics = regressionMetrics( truth, predicted );
    REQUIRE( metrics.has_value() );
    CHECK( metrics->count == 4 );
    CHECK( metrics->mapeSkippedZeros == 4 );
    CHECK_FALSE( metrics->mapeDefined );
    const QJsonObject json = metrics->toJson();
    CHECK( json.value( QStringLiteral( "mape" ) ).isNull() );
    CHECK( json.value( QStringLiteral( "mape_defined" ) ).toBool( true ) == false );

    // Mixed zero/non-zero truth: defined, and the zeros stay counted.
    const QVector<double> mixedTruth{ 0.0, 10.0, 0.0, 10.0 };
    const QVector<double> mixedPredicted{ 1.0, 12.0, 2.0, 8.0 };
    const auto mixed = regressionMetrics( mixedTruth, mixedPredicted );
    REQUIRE( mixed.has_value() );
    CHECK( mixed->mapeDefined );
    CHECK( mixed->mapeSkippedZeros == 2 );
    // |12-10|/10 = 0.2 ; |8-10|/10 = 0.2 → mean 20%.
    CHECK( mixed->mape == Catch::Approx( 20.0 ).epsilon( 1e-12 ) );
    const QJsonObject mixedJson = mixed->toJson();
    CHECK( mixedJson.value( QStringLiteral( "mape" ) ).isDouble() );
    CHECK( mixedJson.value( QStringLiteral( "mape_defined" ) ).toBool() );
}

// ---------------------------------------------------------------------------
// WE-25 (#1804): sparse confusion matrix — big taxonomies work, the budget
// is on stored cells, serialization round-trips both formats.
// ---------------------------------------------------------------------------

namespace
{

ConfusionMatrix diagonalMatrix( int labels, qint64 perClass )
{
    QStringList names;
    names.reserve( labels );
    for ( int i = 0; i < labels; ++i )
        names << QString::number( i );
    ConfusionMatrix matrix( names, labels );
    for ( int i = 0; i < labels; ++i )
        matrix.setCount( i, i, perClass );
    return matrix;
}

} // namespace

TEST_CASE( "confusion matrix above the old 8192 cap works sparsely (WE-25)",
           "[r28_t05][experiment][metrics]" )
{
    // 9000 labels with a pure diagonal used to demand a 9000^2 dense
    // allocation (648 MB) and was refused at the 8192 cap; sparse storage
    // must accept it and compute correct metrics.
    const int labels = 9000;
    const ConfusionMatrix matrix = diagonalMatrix( labels, 7 );
    CHECK( matrix.size() == labels );
    CHECK( matrix.total() == qint64( labels ) * 7 );
    CHECK( matrix.overallAccuracy() == Catch::Approx( 1.0 ) );
    CHECK( matrix.kappa() == Catch::Approx( 1.0 ) );
    const auto perClass = matrix.perClass( 4242 );
    CHECK( perClass.support == 7 );
    CHECK( perClass.precision == Catch::Approx( 1.0 ) );
    CHECK( perClass.recall == Catch::Approx( 1.0 ) );
    CHECK( matrix.macroF1() == Catch::Approx( 1.0 ) );
    CHECK( matrix.balancedAccuracy() == Catch::Approx( 1.0 ) );
}

TEST_CASE( "confusion matrix toJson/fromJson round-trips dense and sparse (WE-25)",
           "[r28_t05][experiment][metrics]" )
{
    // Small matrix: dense emission (backward-compatible schema).
    QStringList smallNames{ "water", "forest", "urban" };
    ConfusionMatrix small( smallNames, 3 );
    small.setCount( 0, 0, 5 );
    small.setCount( 0, 1, 2 );
    small.setCount( 1, 1, 7 );
    small.setCount( 2, 0, 1 );
    small.setCount( 2, 2, 4 );
    const QJsonObject denseJson = small.toJson();
    CHECK( denseJson.contains( QStringLiteral( "counts" ) ) );
    CHECK_FALSE( denseJson.contains( QStringLiteral( "counts_format" ) ) );
    const auto reparsed = ConfusionMatrix::fromJson( denseJson );
    REQUIRE( reparsed.has_value() );
    CHECK( reparsed.value() == small );

    // Large matrix: sparse emission, and fromJson must accept it.
    const int labels = 1200; // above the 1024 dense-emit threshold
    const ConfusionMatrix large = diagonalMatrix( labels, 3 );
    const QJsonObject sparseJson = large.toJson();
    CHECK( sparseJson.value( QStringLiteral( "counts_format" ) ).toString()
           == QStringLiteral( "sparse" ) );
    CHECK( sparseJson.contains( QStringLiteral( "nonzero_counts" ) ) );
    const auto reparsedLarge = ConfusionMatrix::fromJson( sparseJson );
    REQUIRE( reparsedLarge.has_value() );
    CHECK( reparsedLarge.value() == large );
    CHECK( reparsedLarge->overallAccuracy() == Catch::Approx( 1.0 ) );

    // A legacy dense document for a big matrix still parses (sparse store).
    QJsonObject legacy;
    QStringList names;
    for ( int i = 0; i < 1500; ++i )
        names << QString::number( i );
    legacy.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( names ) );
    QJsonArray rows;
    for ( int r = 0; r < 1500; ++r )
    {
        QJsonArray row;
        for ( int c = 0; c < 1500; ++c )
            row.append( r == c ? 2 : 0 );
        rows.append( row );
    }
    legacy.insert( QStringLiteral( "counts" ), rows );
    const auto legacyParsed = ConfusionMatrix::fromJson( legacy );
    REQUIRE( legacyParsed.has_value() );
    CHECK( legacyParsed->total() == 1500 * 2 );
    CHECK( legacyParsed->overallAccuracy() == Catch::Approx( 1.0 ) );
}

TEST_CASE( "confusion matrix refuses beyond the label sanity cap (WE-25)",
           "[r28_t05][experiment][metrics]" )
{
    QStringList tooMany;
    for ( int i = 0; i < 65537; ++i )
        tooMany << QString::number( i );
    QJsonObject json;
    json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( tooMany ) );
    QJsonArray oneRow;
    oneRow.append( 0 );
    QJsonArray rows;
    rows.append( oneRow );
    json.insert( QStringLiteral( "counts" ), rows );
    const auto refused = ConfusionMatrix::fromJson( json );
    CHECK_FALSE( refused.has_value() );
}

TEST_CASE( "sparse confusion documents refuse hostile cells (R1 P1-1/P2-4)",
           "[r28_t05][experiment][metrics]" )
{
    QStringList labels{ "a", "b" };
    const auto sparseDoc = [&]( const QJsonArray &cells ) {
        QJsonObject json;
        json.insert( QStringLiteral( "labels" ), QJsonArray::fromStringList( labels ) );
        json.insert( QStringLiteral( "counts_format" ), QStringLiteral( "sparse" ) );
        json.insert( QStringLiteral( "nonzero_counts" ), cells );
        return json;
    };
    const auto cell = []( qint64 r, qint64 c, double n ) {
        return QJsonObject{ { QStringLiteral( "r" ), r },
                            { QStringLiteral( "c" ), c },
                            { QStringLiteral( "n" ), n } };
    };

    // Negative counts corrupt every downstream total — refused (P1-1).
    QJsonArray negative;
    negative.append( cell( 0, 0, -5 ) );
    CHECK_FALSE( ConfusionMatrix::fromJson( sparseDoc( negative ) ).has_value() );

    // 2^63 as a double wraps to INT64_MIN in the cast — refused (P2-4).
    QJsonArray boundary;
    boundary.append( cell( 0, 0, 9223372036854775808.0 ) );
    CHECK_FALSE( ConfusionMatrix::fromJson( sparseDoc( boundary ) ).has_value() );

    // Cells outside the label grid — refused.
    QJsonArray outside;
    outside.append( cell( 0, 7, 1 ) );
    CHECK_FALSE( ConfusionMatrix::fromJson( sparseDoc( outside ) ).has_value() );

    // The same boundary applies to the legacy dense path (P2-4).
    QJsonObject denseBoundary;
    denseBoundary.insert( QStringLiteral( "labels" ),
                          QJsonArray::fromStringList( labels ) );
    QJsonArray row;
    row.append( 9223372036854775808.0 );
    row.append( 0 );
    QJsonArray denseRows;
    denseRows.append( row );
    denseRows.append( QJsonArray{ 0, 0 } );
    denseBoundary.insert( QStringLiteral( "counts" ), denseRows );
    CHECK_FALSE( ConfusionMatrix::fromJson( denseBoundary ).has_value() );
}

TEST_CASE( "sparse serialization is deterministic across instances (R1 P2-7)",
           "[r28_t05][experiment][metrics]" )
{
    // QHash iteration order is per-process randomized; the sparse emission
    // must sort by key or two equal matrices serialize to different bytes.
    const int labels = 1100;
    QStringList names;
    for ( int i = 0; i < labels; ++i )
        names << QString::number( i );
    ConfusionMatrix a( names, labels );
    ConfusionMatrix b( names, labels );
    for ( int i = 0; i < labels; i += 7 ) // scattered nonzero cells
    {
        a.setCount( i, ( i * 3 ) % labels, i + 1 );
        b.setCount( i, ( i * 3 ) % labels, i + 1 );
    }
    const QByteArray aBytes =
        QJsonDocument( a.toJson() ).toJson( QJsonDocument::Compact );
    const QByteArray bBytes =
        QJsonDocument( b.toJson() ).toJson( QJsonDocument::Compact );
    CHECK( aBytes == bBytes );
    // And the emission order is ascending by flat key (r-major).
    const QJsonArray cells = a.toJson().value( QStringLiteral( "nonzero_counts" ) ).toArray();
    qint64 previousKey = -1;
    const qint64 n = labels;
    for ( const QJsonValue &value : cells )
    {
        const QJsonObject c = value.toObject();
        const qint64 key = c.value( QStringLiteral( "r" ) ).toInteger() * n
                           + c.value( QStringLiteral( "c" ) ).toInteger();
        CHECK( key > previousKey );
        previousKey = key;
    }
}

// ---------------------------------------------------------------------------
// WE-26 (#1804): planning_context refuses non-positive / non-finite
// resolution_m instead of accepting them into planning.
// ---------------------------------------------------------------------------

TEST_CASE( "planning context refuses non-positive resolution_m (WE-26)",
           "[r28_t05][planner][context]" )
{
    const auto contextDoc = []( double resolutionM ) {
        Json::Value doc( Json::objectValue );
        doc["kind"] = "planning_context";
        doc["schema_version"] = "1.0";
        Json::Value assets( Json::arrayValue );
        Json::Value asset( Json::objectValue );
        asset["ref"] = "s2.tif";
        asset["asset_kind"] = "raster";
        asset["resolution_m"] = resolutionM;
        assets.append( asset );
        doc["assets"] = assets;
        return doc;
    };

    sicnu::planner::PlanningContext context;
    std::string error;

    CHECK_FALSE( sicnu::planner::planningContextFromJson( contextDoc( -10.0 ), context, error ) );
    CHECK( error.find( "resolution_m" ) != std::string::npos );

    error.clear();
    CHECK_FALSE( sicnu::planner::planningContextFromJson( contextDoc( 0.0 ), context, error ) );
    CHECK( error.find( "resolution_m" ) != std::string::npos );

    error.clear();
    CHECK( sicnu::planner::planningContextFromJson( contextDoc( 10.5 ), context, error ) );
    CHECK( error.empty() );
    REQUIRE( context.assets.size() == 1 );
    CHECK( context.assets.front().resolutionM == Catch::Approx( 10.5 ) );
}
