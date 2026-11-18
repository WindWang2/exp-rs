// test_r35_t04_study_report_parse.cpp — R35-T04 WP18: StudyReport::fromJson
// must not fabricate trusted zero points. QJsonValue::toDouble() silently
// returns 0.0 for missing/null/string fields, so a damaged curve point used
// to restore as a REAL point with mean=0 — the sensitivity projection passes
// the numbers through and the UI cannot distinguish a legal zero from a
// fabricated one. Strict admission: every numeric point/band field is a real
// JSON number (a legal 0.0 passes untouched); run_count narrows only through
// a whole, in-range double.
#include <catch2/catch_test_macros.hpp>

#include "study/study_export.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstring>
#include <functional>

using namespace sicnu::study;

namespace
{
StudyReport sampleReport()
{
    StudyReport report;
    report.studyId = QStringLiteral( "study_1" );
    report.experimentId = QStringLiteral( "exp_1" );
    report.algorithmId = QStringLiteral( "algo" );
    report.strategy = QStringLiteral( "grid" );
    report.specJson = QJsonObject{ { QStringLiteral( "k" ), 1 } };
    report.stoppedReason = QString();
    report.elapsedMs = 4242;
    report.generatedAtUtc = QDateTime( QDate( 2026, 11, 19 ), QTime( 12, 0, 0 ), Qt::UTC );

    SensitivityCurve curve;
    curve.parameterPath = QStringLiteral( "threshold" );
    curve.metricName = QStringLiteral( "f1" );
    curve.trend = QStringLiteral( "increasing" );
    CurvePoint legalZero;
    legalZero.dimensionValue = 0.5;
    legalZero.runCount = 3;
    legalZero.mean = 0.0; // legal zero: must survive the round-trip as 0.0
    legalZero.populationStdDev = 0.25;
    legalZero.min = -1.0;
    legalZero.max = 1.0;
    CurvePoint normal;
    normal.dimensionValue = 0.75;
    normal.runCount = 2;
    normal.mean = 0.8125;
    normal.populationStdDev = 0.125;
    normal.min = 0.5;
    normal.max = 1.0;
    curve.points = { legalZero, normal };
    report.curves = { curve };

    UncertaintyEnvelope envelope;
    envelope.metricName = QStringLiteral( "f1" );
    UncertaintyBand band;
    band.parameterAssignments = { { QStringLiteral( "threshold" ), QStringLiteral( "0.5" ) } };
    band.runCount = 3;
    band.mean = 0.0; // legal zero mean in a band
    band.populationStdDev = 0.25;
    band.min = -1.0;
    band.max = 1.0;
    envelope.bands = { band };
    report.envelopes = { envelope };
    return report;
}

QJsonObject mutatePoint( const QJsonObject &reportJson, std::function<void( QJsonObject & )> mutator )
{
    QJsonObject doc = reportJson;
    QJsonArray curves = doc.value( QStringLiteral( "curves" ) ).toArray();
    QJsonObject curve = curves.at( 0 ).toObject();
    QJsonArray points = curve.value( QStringLiteral( "points" ) ).toArray();
    QJsonObject point = points.at( 0 ).toObject();
    mutator( point );
    points.replace( 0, point );
    curve.insert( QStringLiteral( "points" ), points );
    curves.replace( 0, curve );
    doc.insert( QStringLiteral( "curves" ), curves );
    return doc;
}

bool failsWithInvalidPoint( const QJsonObject &doc )
{
    const auto parsed = StudyReport::fromJson( doc );
    if ( parsed.isOk() || parsed.diagnostics().isEmpty() )
        return false;
    return parsed.diagnostics().constFirst().code
           == QStringLiteral( "study.report_invalid_point" );
}
} // namespace

TEST_CASE( "StudyReport round-trips legal zeros exactly", "[study][r35t04]" )
{
    const StudyReport report = sampleReport();
    const QJsonObject json = report.toJson();
    const auto parsed = StudyReport::fromJson( json );
    REQUIRE( parsed.isOk() );
    const StudyReport &back = parsed.value();
    REQUIRE( back.curves.size() == 1 );
    REQUIRE( back.curves.front().points.size() == 2 );
    // The legal zero means survive — never nudged, never dropped.
    REQUIRE( back.curves.front().points.front().mean == 0.0 );
    REQUIRE( back.curves.front().points.at( 1 ).mean == 0.8125 );
    REQUIRE( back.curves.front().points.front().runCount == 3 );
    REQUIRE( back.curves.front().points.front().dimensionValue == 0.5 );
    REQUIRE( back.curves.front().points.front().populationStdDev == 0.25 );
    REQUIRE( back.curves.front().points.front().min == -1.0 );
    REQUIRE( back.curves.front().points.front().max == 1.0 );
    // Bands round-trip with their legal zero mean too.
    REQUIRE( back.envelopes.size() == 1 );
    REQUIRE( back.envelopes.front().bands.front().mean == 0.0 );
    REQUIRE( back.envelopes.front().bands.front().runCount == 3 );
    REQUIRE( back.elapsedMs == 4242 );
}

TEST_CASE( "StudyReport refuses missing, null and string-typed curve numbers",
           "[study][r35t04]" )
{
    const QJsonObject base = sampleReport().toJson();

    SECTION( "mean removed" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.remove( QStringLiteral( "mean" ) ); } ) ) );
    }
    SECTION( "mean as string" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.insert( QStringLiteral( "mean" ), QStringLiteral( "0.5" ) ); } ) ) );
    }
    SECTION( "mean as null" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.insert( QStringLiteral( "mean" ), QJsonValue() ); } ) ) );
    }
    SECTION( "mean as bool" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.insert( QStringLiteral( "mean" ), true ); } ) ) );
    }
    SECTION( "dimension_value removed" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.remove( QStringLiteral( "dimension_value" ) ); } ) ) );
    }
    SECTION( "min as string" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.insert( QStringLiteral( "min" ), QStringLiteral( "-1" ) ); } ) ) );
    }
    SECTION( "max removed" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.remove( QStringLiteral( "max" ) ); } ) ) );
    }
    SECTION( "population_std_dev as string" )
    {
        REQUIRE( failsWithInvalidPoint( mutatePoint(
            base, []( QJsonObject &p ) { p.insert( QStringLiteral( "population_std_dev" ), QStringLiteral( "0.25" ) ); } ) ) );
    }
}

TEST_CASE( "StudyReport run_count narrows only through whole in-range doubles",
           "[study][r35t04]" )
{
    const QJsonObject base = sampleReport().toJson();
    // Fractional count: not a count.
    REQUIRE( failsWithInvalidPoint( mutatePoint(
        base, []( QJsonObject &p ) { p.insert( QStringLiteral( "run_count" ), 2.5 ); } ) ) );
    // Negative count: not a count.
    REQUIRE( failsWithInvalidPoint( mutatePoint(
        base, []( QJsonObject &p ) { p.insert( QStringLiteral( "run_count" ), -1.0 ); } ) ) );
    // Missing count.
    REQUIRE( failsWithInvalidPoint( mutatePoint(
        base, []( QJsonObject &p ) { p.remove( QStringLiteral( "run_count" ) ); } ) ) );
    // String count.
    REQUIRE( failsWithInvalidPoint( mutatePoint(
        base, []( QJsonObject &p ) { p.insert( QStringLiteral( "run_count" ), QStringLiteral( "3" ) ); } ) ) );
    // Beyond 2^53: the double wire cannot carry the count exactly. (2^53+1
    // is NOT representable — the literal folds to exactly 2^53 — so the
    // probe uses 2^53+2, the first integer the double domain admits above
    // the bound.)
    REQUIRE( failsWithInvalidPoint(
        mutatePoint( base, []( QJsonObject &p ) { p.insert( QStringLiteral( "run_count" ), 9007199254740994.0 ); } ) ) );
    // Zero is a legal count (a sampled-but-unexecuted point).
    const QJsonObject zeroCount = mutatePoint(
        base, []( QJsonObject &p ) { p.insert( QStringLiteral( "run_count" ), 0.0 ); } );
    const auto parsed = StudyReport::fromJson( zeroCount );
    REQUIRE( parsed.isOk() );
    REQUIRE( parsed.value().curves.front().points.front().runCount == 0 );
}

TEST_CASE( "StudyReport envelope bands and elapsed_ms share the strict gate",
           "[study][r35t04]" )
{
    const QJsonObject base = sampleReport().toJson();

    // Band mean as string → typed refusal (uncertainty must not be invented).
    {
        QJsonObject doc = base;
        QJsonArray envelopes = doc.value( QStringLiteral( "uncertainty_envelopes" ) ).toArray();
        QJsonObject envelope = envelopes.at( 0 ).toObject();
        QJsonArray bands = envelope.value( QStringLiteral( "bands" ) ).toArray();
        QJsonObject band = bands.at( 0 ).toObject();
        band.insert( QStringLiteral( "mean" ), QStringLiteral( "0" ) );
        bands.replace( 0, band );
        envelope.insert( QStringLiteral( "bands" ), bands );
        envelopes.replace( 0, envelope );
        doc.insert( QStringLiteral( "uncertainty_envelopes" ), envelopes );
        REQUIRE( failsWithInvalidPoint( doc ) );
    }
    // Band run_count removed → typed refusal.
    {
        QJsonObject doc = base;
        QJsonArray envelopes = doc.value( QStringLiteral( "uncertainty_envelopes" ) ).toArray();
        QJsonObject envelope = envelopes.at( 0 ).toObject();
        QJsonArray bands = envelope.value( QStringLiteral( "bands" ) ).toArray();
        QJsonObject band = bands.at( 0 ).toObject();
        band.remove( QStringLiteral( "run_count" ) );
        bands.replace( 0, band );
        envelope.insert( QStringLiteral( "bands" ), bands );
        envelopes.replace( 0, envelope );
        doc.insert( QStringLiteral( "uncertainty_envelopes" ), envelopes );
        REQUIRE( failsWithInvalidPoint( doc ) );
    }
    // elapsed_ms missing → typed refusal (previously a silent 0 ms).
    {
        QJsonObject doc = base;
        doc.remove( QStringLiteral( "elapsed_ms" ) );
        REQUIRE( failsWithInvalidPoint( doc ) );
    }
    // elapsed_ms fractional → typed refusal.
    {
        QJsonObject doc = base;
        doc.insert( QStringLiteral( "elapsed_ms" ), 42.5 );
        REQUIRE( failsWithInvalidPoint( doc ) );
    }
}
