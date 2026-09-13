// Workbench 10.0 — Visual Analytics platform (goal WP-C)
//
// Covers: typed payloads stay bounded and self-consistent (matrix totals,
// histogram bin invariants), the async source delivers ready/failed on the
// GUI thread with generation-based supersession (older results never land
// after a newer request), and the chart host renders states + exports the
// CURRENT payload (CSV/JSON) without inventing rows.
#include <catch2/catch_test_macros.hpp>

#include "app/visualanalytics/va_chart_widget.h"
#include "app/visualanalytics/va_source.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace sicnu::app::va;

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_visual_analytics";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app && !QCoreApplication::instance() )
        app = new QApplication( fake_argc, fake_argv ); // offscreen via env
    return app;
}

VaHistogram makeHistogram( int bins )
{
    VaHistogram histogram;
    for ( int i = 0; i <= bins; ++i )
        histogram.binEdges.append( i * 1.0 );
    for ( int i = 0; i < bins; ++i )
        histogram.counts.append( i + 1 );
    histogram.validCount = bins;
    histogram.mean = 1.5;
    return histogram;
}

} // namespace

TEST_CASE( "VA payloads keep their invariants", "[visual_analytics][data]" )
{
    VaMatrix matrix;
    matrix.rowLabels = { QStringLiteral( "a" ), QStringLiteral( "b" ) };
    matrix.colLabels = { QStringLiteral( "a" ), QStringLiteral( "b" ) };
    matrix.cells = { 3, 1, 0, 7 };
    REQUIRE( matrix.cells.size() == matrix.rowLabels.size() * matrix.colLabels.size() );
    CHECK( matrix.total() == 11 );

    const VaHistogram histogram = makeHistogram( 64 );
    REQUIRE( histogram.binEdges.size() == histogram.counts.size() + 1 );
    qint64 binTotal = 0;
    for ( qint64 c : histogram.counts )
        binTotal += c;
    CHECK( binTotal == histogram.validCount );
}

TEST_CASE( "VaDataSource delivers ready payloads", "[visual_analytics][source]" )
{
    ensureApp();
    VaDataSource source;
    QSignalSpy readySpy( &source, &VaDataSource::ready );
    QSignalSpy failedSpy( &source, &VaDataSource::failed );

    VaData payload;
    payload.kind = VaChartKind::Histogram;
    payload.histogram = makeHistogram( 8 );
    source.request( [payload]( const std::function<bool()> & ) { return payload; } );
    REQUIRE( readySpy.wait( 5000 ) );
    CHECK( failedSpy.isEmpty() );
    CHECK( readySpy.first().first().value<VaData>().histogram.validCount == 8 );
    CHECK_FALSE( source.isBusy() );
}

TEST_CASE( "VaDataSource turns exceptions into typed failures",
           "[visual_analytics][source]" )
{
    ensureApp();
    VaDataSource source;
    QSignalSpy readySpy( &source, &VaDataSource::ready );
    QSignalSpy failedSpy( &source, &VaDataSource::failed );

    source.request( []( const std::function<bool()> & ) -> VaData {
        throw std::runtime_error( "no valid pixels" );
    } );
    REQUIRE( failedSpy.wait( 5000 ) );
    CHECK( failedSpy.first().first().toString().contains( QStringLiteral( "no valid pixels" ) ) );
    CHECK( readySpy.isEmpty() );
}

TEST_CASE( "VaDataSource supersession drops stale results",
           "[visual_analytics][source]" )
{
    ensureApp();
    VaDataSource source;
    QSignalSpy readySpy( &source, &VaDataSource::ready );

    std::atomic<bool> slowReleased { false };
    source.request( [ &slowReleased ]( const std::function<bool()> &stale ) {
        while ( !stale() && !slowReleased.load() )
        {
            // hold the first job until it is superseded or released
            QThread::msleep( 5 );
        }
        VaData data;
        data.kind = VaChartKind::Series;
        data.series.name = QStringLiteral( "slow" );
        return data;
    } );

    // The second request supersedes the first and wins the race.
    VaData fast;
    fast.kind = VaChartKind::Series;
    fast.series.name = QStringLiteral( "fast" );
    source.request( [fast]( const std::function<bool()> & ) { return fast; } );
    REQUIRE( readySpy.wait( 5000 ) );
    slowReleased.store( true );

    // Let any (dropped) stale delivery drain — the contract says it must not.
    QTest::qWait( 200 );
    for ( const auto &emission : readySpy )
    {
        const VaData data = emission.first().value<VaData>();
        CHECK( data.series.name == QStringLiteral( "fast" ) );
    }
}

TEST_CASE( "VaChartWidget states and exports", "[visual_analytics][chart]" )
{
    ensureApp();
    VaChartWidget chart;
    chart.resize( 320, 240 );

    SECTION( "empty by default, no CSV invented" )
    {
        CHECK( chart.toCsv().isEmpty() );
        CHECK( chart.toJson().isEmpty() );
    }

    SECTION( "histogram payload renders and exports bounded rows" )
    {
        VaData data;
        data.kind = VaChartKind::Histogram;
        data.histogram = makeHistogram( 16 );
        chart.setData( data );
        CHECK( chart.kind() == VaChartKind::Histogram );

        const QString csv = chart.toCsv();
        const int rows = csv.count( QLatin1Char( '\n' ) );
        CHECK( rows == 17 ); // header + 16 bins
        CHECK( csv.contains( QStringLiteral( "bin_start,bin_end,count" ) ) );

        bool parsed = false;
        const QJsonDocument doc =
            QJsonDocument::fromJson( chart.toJson().toUtf8(), nullptr );
        parsed = doc.isObject();
        CHECK( parsed );
        CHECK( doc.object()[QStringLiteral( "bins" )].toInt() == 16 );
    }

    SECTION( "error state clears exports" )
    {
        chart.setError( QStringLiteral( "boom" ) );
        CHECK( chart.toCsv().isEmpty() );
    }
}
