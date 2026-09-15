// Workbench 10.0 — Visual Analytics platform (goal WP-C)
//
// Covers: typed payloads stay bounded and self-consistent (matrix totals,
// histogram bin invariants), the async source delivers ready/failed on the
// GUI thread with generation-based supersession (older results never land
// after a newer request), and the chart host renders states + exports the
// CURRENT payload (CSV/JSON) without inventing rows.
#include <catch2/catch_test_macros.hpp>

#include "app/visualanalytics/va_chart_widget.h"
#include "app/visualanalytics/va_selection_hub.h"
#include "app/visualanalytics/va_source.h"

#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>
#include <limits>

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
    qint64 total = 0;
    for ( qint64 c : histogram.counts )
        total += c;
    histogram.validCount = total;
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
    CHECK( readySpy.first().first().value<VaData>().histogram.validCount == 36 );
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

// ── Linked Visual Analytics 11.0 — selection hub + brushing contract ────

TEST_CASE( "VaSelectionHub stamps generations and drops echoes",
           "[visual_analytics][hub]" )
{
    ensureApp();
    VaSelectionHub hub;

    QSignalSpy publishedSpy( &hub, &VaSelectionHub::selectionPublished );

    VaSelectionSubject subject;
    subject.kind = VaSelectionKind::ChartRange;
    subject.chartId = QStringLiteral( "rsVaHistogram" );
    subject.x0 = 1.0;
    subject.x1 = 2.0;

    // A subscriber that echoes the event straight back with the same origin
    // must NOT loop the hub (Oracle 1: no infinite feedback).
    quint64 echoGeneration = 0;
    QObject echoGuard;
    QObject::connect( &hub, &VaSelectionHub::selectionPublished, &echoGuard,
                      [&]( const VaSelectionEvent &event ) {
                          echoGeneration = hub.publish( event.subject, event.origin );
                      } );

    const quint64 gen = hub.publish( subject, QStringLiteral( "va.panel" ) );
    CHECK( gen != 0 );
    CHECK( echoGeneration == 0 ); // echo suppressed, no new generation issued
    CHECK( hub.stats().suppressedEchoes == 1 );
    CHECK( publishedSpy.count() == 1 ); // one broadcast, not two

    // A different surface re-publishing is fresh intent, not an echo.
    hub.publish( subject, QStringLiteral( "map.main" ) );
    CHECK( publishedSpy.count() == 2 );
    CHECK( hub.stats().suppressedEchoes == 1 );

    // Generations are strictly monotonic.
    const quint64 first = hub.publish( subject, QStringLiteral( "va.panel" ) );
    const quint64 second = hub.publish( subject, QStringLiteral( "va.panel" ) );
    CHECK( second == first + 1 );
}

TEST_CASE( "VaSelectionHub rejects invalid subjects and clamps text",
           "[visual_analytics][hub]" )
{
    ensureApp();
    VaSelectionHub hub;

    VaSelectionSubject nan;
    nan.kind = VaSelectionKind::ViewPoint;
    nan.x0 = std::numeric_limits<double>::quiet_NaN();
    CHECK( hub.publish( nan, QStringLiteral( "map.main" ) ) == 0 );
    CHECK( hub.stats().rejected == 1 );

    VaSelectionSubject inverted;
    inverted.kind = VaSelectionKind::ChartRange;
    inverted.x0 = 5.0;
    inverted.x1 = 1.0; // inverted range
    CHECK( hub.publish( inverted, QStringLiteral( "va.panel" ) ) == 0 );

    VaSelectionSubject longText;
    longText.kind = VaSelectionKind::Layer;
    longText.assetId = QString( 4096, QLatin1Char( 'a' ) );
    const quint64 gen = hub.publish( longText, QStringLiteral( "va.panel" ) );
    CHECK( gen != 0 );
    CHECK( hub.history().first().subject.assetId.size()
           == VaSelectionHub::kMaxTextChars );
}

TEST_CASE( "VaSelectionHub history stays bounded", "[visual_analytics][hub]" )
{
    ensureApp();
    VaSelectionHub hub;

    VaSelectionSubject subject;
    subject.kind = VaSelectionKind::ChartPoint;
    subject.chartId = QStringLiteral( "rsVaScatter" );
    for ( int i = 0; i < 100; ++i )
        hub.publish( subject, QStringLiteral( "va.panel" ) );
    CHECK( hub.history().size() == VaSelectionHub::kHistoryCapacity );
    CHECK( hub.stats().published == 100 );
}

TEST_CASE( "VaSelectionHub absorbs 100k logical selection events without growth",
           "[visual_analytics][hub][scale]" )
{
    ensureApp();
    VaSelectionHub hub;

    qint64 received = 0;
    QObject consumer;
    QObject::connect( &hub, &VaSelectionHub::selectionPublished, &consumer,
                      [&]( const VaSelectionEvent & ) { ++received; } );

    VaSelectionSubject subject;
    subject.kind = VaSelectionKind::Pixel;
    subject.layerId = QStringLiteral( "layer-a" );
    subject.assetId = QStringLiteral( "asset-a" );
    constexpr int kLogicalEvents = 100000;
    for ( int i = 0; i < kLogicalEvents; ++i )
    {
        subject.row = i;
        subject.column = i % 251;
        REQUIRE( hub.publish( subject, QStringLiteral( "map.main" ) ) != 0 );
    }
    CHECK( received == kLogicalEvents );
    // Diagnostics history is a bounded ring — never 100k entries.
    CHECK( hub.history().size() == VaSelectionHub::kHistoryCapacity );
    CHECK( hub.stats().published == kLogicalEvents );
}

TEST_CASE( "Scatter brush filter keeps values and geometry consistent",
           "[visual_analytics][brush]" )
{
    VaData payload;
    payload.kind = VaChartKind::Scatter;
    // Independent truth: exactly three points fall inside [2.0, 4.0].
    const std::vector<double> xs = { 1.0, 2.0, 3.0, 4.0, 5.0 };
    const std::vector<double> ys = { 10.0, 20.0, 30.0, 40.0, 50.0 };
    for ( size_t i = 0; i < xs.size(); ++i )
    {
        payload.scatter.xs.append( xs[i] );
        payload.scatter.ys.append( ys[i] );
        payload.scatter.groups.append( -1 );
        payload.scatter.cols.append( static_cast<qint64>( i ) * 3 );
        payload.scatter.rows.append( static_cast<qint64>( i ) * 7 );
    }
    payload.scatter.hasGeometry = true;
    payload.scatter.geotransform = { 0, 1, 0, 0, 0, -1 };

    const VaData filtered = filterScatterByXRange( payload, 2.0, 4.0 );
    REQUIRE( filtered.scatter.xs.size() == 3 );
    CHECK( filtered.scatter.xs == QVector<double>{ 2.0, 3.0, 4.0 } );
    CHECK( filtered.scatter.ys == QVector<double>{ 20.0, 30.0, 40.0 } );
    // Geometry stays parallel to the kept points (chart→map picks stay true).
    REQUIRE( filtered.scatter.cols.size() == 3 );
    REQUIRE( filtered.scatter.rows.size() == 3 );
    CHECK( filtered.scatter.cols == QVector<qint64>{ 3, 6, 9 } );
    CHECK( filtered.scatter.rows == QVector<qint64>{ 7, 14, 21 } );

    // Out-of-range brushes keep everything; non-scatter payloads pass through.
    CHECK( filterScatterByXRange( payload, -100.0, 100.0 ).scatter.xs.size() == 5 );
    VaData histogramPayload;
    histogramPayload.kind = VaChartKind::Histogram;
    CHECK( filterScatterByXRange( histogramPayload, 0.0, 1.0 ).kind
           == VaChartKind::Histogram );
}
