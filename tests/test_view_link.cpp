// Workbench 10.0 — N-view link coordination (goal WP-E)
//
// Covers: view registration through the display-manager authority, linked
// extent propagation between peers (same-CRS), per-view unlink stopping
// propagation, automatic detach on viewAboutToBeRemoved, and the reentrancy
// contract (propagation never echoes back).
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/shell/view_link_controller.h"
#include "app/visualanalytics/va_cursor_probe.h"
#include "app/visualanalytics/va_layer_link_controller.h"

#include "data/data_manager.h"

#include <QApplication>
#include <QEvent>
#include <QMetaObject>
#include <QTemporaryDir>
#include <QThread>

#include <qgsapplication.h>
#include <qgsmapcanvas.h>
#include <qgspointxy.h>
#include <qgsrectangle.h>
#include <qgslayertree.h>
#include <qgsmaplayerstore.h>
#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransform.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include "data/data_manager.h"
#include "geospatial/raster/raster_reader.h"

#include <gdal.h>

#include <cstdio>
#include <cstdlib>
#include <functional>

using sicnu::app::ViewLinkController;
using sicnu::app::VaLayerLinkController;
using sicnu::app::va::VaCursorProbe;
using sicnu::display::DisplayLayerId;
using sicnu::display::DisplayViewId;
using sicnu::display::DisplayViewSpec;
using sicnu::display::QgisDisplayManager;

namespace
{

sicnu::data::DataManager &sharedDataManager()
{
    static sicnu::data::DataManager manager;
    return manager;
}

struct DataManager
{
    // The manager needs a DataManager for asset leases; views created with
    // explicit canvases/trees/stores never touch it in these tests.
    QgisDisplayManager manager{ &sharedDataManager() };

    DisplayViewId createView( QgsMapCanvas &canvas, QgsLayerTree &tree,
                              QgsMapLayerStore &store )
    {
        DisplayViewSpec spec;
        spec.canvas = &canvas;
        spec.layerTree = &tree;
        spec.layerStore = &store;
        const auto created = manager.createView( spec );
        if ( !created )
            return DisplayViewId();
        return created.value();
    }
};

} // namespace

int main( int argc, char *argv[] )
{
    // Mirror test_qgis_display_manager: a heap-held QgsApplication with GUI
    // enabled (QT_QPA_PLATFORM=offscreen carries it); QgsMapCanvas aborts
    // without the QGIS singletons.
    // Intentionally leaked (never destroyed, no exitQgis): a function-static
    // QgsApplication is torn down during static destruction after QGIS
    // singletons are gone, which segfaulted Catch2 test discovery
    // (`--list-tests`) and aborted the whole CTest run.
    auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
    const int result = Catch::Session().run( argc, argv );
    // Canvas/project cases still crashed in glibc atexit cleanup (QGIS
    // thread-local PROJ context) once ctest ran them one case per process,
    // after every assertion had passed. Skip static destruction entirely,
    // like test_workbench_full_shell_lifecycle / test_twincanvas_sync.
    std::fflush( stdout );
    std::fflush( stderr );
    std::_Exit( result );
}

TEST_CASE( "view link propagates extents across linked views",
           "[view_link][workbench10]" )
{
    DataManager data;
    QgsMapCanvas a, b, c;
    QgsLayerTree treeA, treeB, treeC;
    QgsMapLayerStore storeA, storeB, storeC;

    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    const DisplayViewId viewC = data.createView( c, treeC, storeC );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );
    REQUIRE( !viewC.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.addView( viewC );

    // Link A and B; C stays independent.
    controller.setLinked( viewA, true );
    controller.setLinked( viewB, true );
    CHECK( controller.isLinked( viewA ) );
    CHECK( controller.isLinked( viewB ) );
    CHECK_FALSE( controller.isLinked( viewC ) );

    a.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    a.setExtent( QgsRectangle( 10, 10, 90, 90 ) );
    QThread::msleep( 80 ); // throttle window (16 ms) + propagation
    QApplication::processEvents();

    CHECK( b.extent() == a.extent() );
    CHECK_FALSE( c.extent() == a.extent() );
    CHECK( controller.stats().appliedSyncCount >= 1 );

    // Unlinking B stops its propagation; A still linked to nothing else.
    controller.setLinked( viewB, false );
    a.setExtent( QgsRectangle( 20, 20, 80, 80 ) );
    QThread::msleep( 80 );
    QApplication::processEvents();
    CHECK_FALSE( b.extent() == a.extent() );
}

TEST_CASE( "view link detaches removed views automatically",
           "[view_link][workbench10]" )
{
    DataManager data;
    QgsMapCanvas a, b;
    QgsLayerTree treeA, treeB;
    QgsMapLayerStore storeA, storeB;

    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.setLinked( viewA, true );
    controller.setLinked( viewB, true );

    REQUIRE( data.manager.removeView( viewB ).operator bool() );
    CHECK( !controller.views().contains( viewB ) );
    CHECK( !controller.isLinked( viewB ) );
}

TEST_CASE( "view link rejects unknown views", "[view_link][workbench10]" )
{
    DataManager data;
    ViewLinkController controller( &data.manager );
    controller.addView( DisplayViewId::generate() ); // unknown to the manager
    CHECK( controller.views().isEmpty() );
}


// ── Linked Visual Analytics 11.0 ─────────────────────────────────────────

namespace
{

/// Small GDAL GeoTIFF with a known-answer pixel value, written fresh per
/// fixture instance (value(col,row) = row*16 + col; north-up WGS84 grid with
/// origin (0,16), resolution 1°/px).
struct KnownAnswerRaster
{
    QString path;
    KnownAnswerRaster()
    {
        static QTemporaryDir dir;
        GDALAllRegister();
        GDALDriverH driver = GDALGetDriverByName( "GTiff" );
        REQUIRE( driver != nullptr );
        constexpr int width = 16;
        constexpr int height = 16;
        path = dir.filePath( QStringLiteral( "va_link_known_answer.tif" ) );
        GDALDatasetH dataset =
          GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_Float32,
                      nullptr );
        REQUIRE( dataset != nullptr );
        double geotransform[6] = { 0.0, 1.0, 0.0, static_cast<double>( height ), 0.0,
                                   -1.0 };
        GDALSetGeoTransform( dataset, geotransform );
        GDALSetProjection(
          dataset,
          "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,"
          "298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\","
          "0.0174532925199433]]" );
        GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
        for ( int row = 0; row < height; ++row )
        {
            std::vector<float> line( width );
            for ( int col = 0; col < width; ++col )
                line[col] = static_cast<float>( row * 16 + col );
            GDALRasterIO( band, GF_Write, 0, row, width, 1, line.data(), width, 1,
                          GDT_Float32, 0, 0 );
        }
        GDALClose( dataset );
    }
};

sicnu::data::AssetId registerRasterAsset( sicnu::data::DataManager &data,
                                          const QString &path )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    const auto registered = data.registerSource( sicnu::data::RegisterRequest{ source } );
    REQUIRE( !registered.assetId.isNull() );
    return registered.assetId;
}

/// Waits (bounded, event-loop friendly) until @p done turns true.
bool waitFor( const std::function<bool()> &done, int cycles = 500 )
{
    for ( int i = 0; i < cycles && !done(); ++i )
    {
        QApplication::processEvents();
        QThread::msleep( 10 );
    }
    return done();
}

} // namespace

TEST_CASE( "view link groups propagate independently", "[view_link][linked11]" )
{
    DataManager data;
    QgsMapCanvas a, b, c, d;
    QgsLayerTree treeA, treeB, treeC, treeD;
    QgsMapLayerStore storeA, storeB, storeC, storeD;

    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    const DisplayViewId viewC = data.createView( c, treeC, storeC );
    const DisplayViewId viewD = data.createView( d, treeD, storeD );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );
    REQUIRE( !viewC.isNull() );
    REQUIRE( !viewD.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.addView( viewC );
    controller.addView( viewD );

    controller.setLinkGroup( viewA, QStringLiteral( "terrain" ) );
    controller.setLinkGroup( viewB, QStringLiteral( "terrain" ) );
    controller.setLinkGroup( viewC, QStringLiteral( "spectral" ) );
    controller.setLinkGroup( viewD, QStringLiteral( "spectral" ) );
    CHECK( controller.groups().size() == 2 );
    CHECK( controller.viewsInGroup( QStringLiteral( "terrain" ) ).size() == 2 );
    CHECK( controller.linkGroup( viewA ) == QStringLiteral( "terrain" ) );

    a.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    QThread::msleep( 80 ); // throttle window (16 ms) + propagation
    QApplication::processEvents();

    // Group "terrain" followed A; group "spectral" is untouched.
    CHECK( b.extent() == a.extent() );
    CHECK_FALSE( c.extent() == a.extent() );
    CHECK_FALSE( d.extent() == a.extent() );

    c.setExtent( QgsRectangle( 500, 500, 700, 700 ) );
    QThread::msleep( 80 );
    QApplication::processEvents();
    CHECK( d.extent() == c.extent() );
    CHECK_FALSE( b.extent() == c.extent() );

    // A one-pass propagation terminates: no ping-pong after settling.
    const auto settled = controller.stats().appliedSyncCount;
    QApplication::processEvents();
    QThread::msleep( 40 );
    QApplication::processEvents();
    CHECK( controller.stats().appliedSyncCount == settled );
}

TEST_CASE( "viewport history restores previous extents step by step",
           "[view_link][linked11]" )
{
    DataManager data;
    QgsMapCanvas a;
    QgsLayerTree treeA;
    QgsMapLayerStore storeA;
    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    REQUIRE( !viewA.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );

    a.setExtent( QgsRectangle( 0, 0, 100, 100 ) );
    QApplication::processEvents();
    const QgsRectangle e0 = a.extent();
    a.setExtent( QgsRectangle( 10, 10, 90, 90 ) );
    QApplication::processEvents();
    const QgsRectangle e1 = a.extent();
    a.setExtent( QgsRectangle( 20, 20, 80, 80 ) );
    QApplication::processEvents();
    const QgsRectangle e2 = a.extent();

    CHECK( controller.historyCount( viewA ) >= 2 );
    CHECK( controller.restorePreviousViewport( viewA ) );
    CHECK( a.extent() == e1 );
    CHECK( controller.stats().restoredViewports == 1 );
    CHECK( controller.restorePreviousViewport( viewA ) );
    CHECK( a.extent() == e0 );
    // Walking further back can only leave e0 (or stop when history ends);
    // it must never jump FORWARD again (redo would be a different command).
    if ( controller.restorePreviousViewport( viewA ) )
        CHECK( a.extent() != e2 );
}

TEST_CASE( "cursor link projects cross-CRS with a known answer",
           "[view_link][linked11][crs]" )
{
    DataManager data;
    QgsMapCanvas source, peer;
    QgsLayerTree treeS, treeP;
    QgsMapLayerStore storeS, storeP;

    source.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    peer.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ) );

    const DisplayViewId viewS = data.createView( source, treeS, storeS );
    const DisplayViewId viewP = data.createView( peer, treeP, storeP );
    REQUIRE( !viewS.isNull() );
    REQUIRE( !viewP.isNull() );

    // Independent truth: what the peer crosshair should receive, computed
    // from the EPSG definitions directly (never via the controller).
    const QgsCoordinateTransform truth(
      QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ),
      QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:3857" ) ),
      QgsProject::instance() );
    const QgsPointXY geographic( 90.0, 45.0 );
    const QgsPointXY expected = truth.transform( geographic );
    CHECK( qgsDoubleNear( expected.x(), 10018754.171394622, 1e-3 ) );
    CHECK( qgsDoubleNear( expected.y(), 5621521.486192069, 1e-3 ) );

    ViewLinkController controller( &data.manager );
    controller.addView( viewS );
    controller.addView( viewP );
    controller.setLinkGroup( viewS, QStringLiteral( "g" ) );
    controller.setLinkGroup( viewP, QStringLiteral( "g" ) );
    controller.setCursorMarkersVisible( false ); // headless: geometry only

    Qt::ConnectionType direct =
      Qt::DirectConnection; // we are on the emitting (GUI) thread
    QgsPointXY receivedPoint;
    QString receivedWkt;
    int movedCount = 0;
    QObject::connect( &controller, &ViewLinkController::cursorMoved, &controller,
                      [&]( sicnu::display::DisplayViewId, const QgsPointXY &point,
                           const QString &wkt ) {
                          ++movedCount;
                          receivedPoint = point;
                          receivedWkt = wkt;
                      },
                      direct );

    // Emit the canvas signal the way the canvas itself would on mouse move.
    QMetaObject::invokeMethod( &source, "xyCoordinates",
                               Q_ARG( QgsPointXY, geographic ) );
    QApplication::processEvents();

    REQUIRE( movedCount == 1 );
    CHECK( receivedPoint == geographic );
    CHECK( QgsCoordinateReferenceSystem( receivedWkt ).isValid() );
    CHECK( controller.stats().cursorProjections == 1 );
    CHECK( controller.stats().transformFailures == 0 );
}

TEST_CASE( "cursor link suppresses crosshair state when the pointer leaves",
           "[view_link][linked11]" )
{
    DataManager data;
    QgsMapCanvas a, b;
    QgsLayerTree treeA, treeB;
    QgsMapLayerStore storeA, storeB;
    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );

    ViewLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );
    controller.setLinked( viewA, true );
    controller.setLinked( viewB, true );
    controller.setCursorMarkersVisible( false );

    int leftCount = 0;
    QObject::connect( &controller, &ViewLinkController::cursorLeft, &controller,
                      [&]( sicnu::display::DisplayViewId ) { ++leftCount; } );

    const QgsPointXY p( 5.0, 5.0 );
    QMetaObject::invokeMethod( &a, "xyCoordinates", Q_ARG( QgsPointXY, p ) );
    QApplication::processEvents();
    // Simulate the Leave event path through the controller's event filter.
    QEvent leave( QEvent::Leave );
    QApplication::sendEvent( &a, &leave );
    QApplication::processEvents();

    CHECK( leftCount == 1 );
    CHECK( controller.cursorSyncEnabled() );
}

TEST_CASE( "removing a view mid-flight does not crash the link controllers",
           "[view_link][linked11][lifecycle]" )
{
    DataManager data;
    auto *a = new QgsMapCanvas();
    auto *treeA = new QgsLayerTree();
    auto *storeA = new QgsMapLayerStore();
    const DisplayViewId viewA = data.createView( *a, *treeA, *storeA );
    REQUIRE( !viewA.isNull() );

    QgsMapCanvas b;
    QgsLayerTree treeB;
    QgsMapLayerStore storeB;
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    REQUIRE( !viewB.isNull() );

    ViewLinkController links( &data.manager );
    VaLayerLinkController layerLinks( &data.manager );
    links.addView( viewA );
    links.addView( viewB );
    layerLinks.addView( viewA );
    layerLinks.addView( viewB );
    links.setLinked( viewA, true );
    links.setLinked( viewB, true );
    links.setActiveView( viewA );

    a->setExtent( QgsRectangle( 0, 0, 10, 10 ) );
    // The real removal flow: the manager announces the view first, then the
    // canvas dies while a propagation may still be pending on the throttle.
    REQUIRE( data.manager.removeView( viewA ) );
    CHECK_FALSE( links.views().contains( viewA ) );
    CHECK_FALSE( layerLinks.views().contains( viewA ) );
    CHECK( links.activeView().isNull() );
    a->deleteLater();
    QApplication::processEvents();
    QThread::msleep( 40 );
    QApplication::processEvents();

    // The surviving view keeps working.
    b.setExtent( QgsRectangle( 1, 1, 9, 9 ) );
    QApplication::processEvents();
    CHECK( links.views().contains( viewB ) );
}

TEST_CASE( "layer visibility link syncs by asset across views",
           "[view_link][linked11][layers]" )
{
    KnownAnswerRaster raster;
    DataManager data;
    QgsMapCanvas a, b;
    QgsLayerTree treeA, treeB;
    QgsMapLayerStore storeA, storeB;
    const DisplayViewId viewA = data.createView( a, treeA, storeA );
    const DisplayViewId viewB = data.createView( b, treeB, storeB );
    REQUIRE( !viewA.isNull() );
    REQUIRE( !viewB.isNull() );

    VaLayerLinkController controller( &data.manager );
    controller.addView( viewA );
    controller.addView( viewB );

    // The SAME asset in both views (each view keeps its own layer record).
    const sicnu::data::AssetId assetId =
      registerRasterAsset( sharedDataManager(), raster.path );
    const auto layerA = data.manager.addLayer( viewA, assetId );
    REQUIRE( layerA );
    const auto layerB = data.manager.addLayer( viewB, assetId );
    REQUIRE( layerB );

    // Hide in A → B follows through the view-local tree authority.
    REQUIRE( data.manager.setLayerVisible( layerA.value(), false ) );
    QApplication::processEvents();

    QgsMapLayer *peerLayer = data.manager.mapLayer( layerB.value() );
    REQUIRE( peerLayer );
    QgsLayerTree *peerTree = data.manager.viewLayerTree( viewB );
    REQUIRE( peerTree );
    bool peerVisible = true;
    for ( QgsLayerTreeLayer *node : peerTree->findLayers() )
    {
        if ( node->layer() == peerLayer )
            peerVisible = node->itemVisibilityChecked();
    }
    CHECK_FALSE( peerVisible );
    CHECK( controller.stats().visibilitySyncs >= 1 );

    // Show again → both return to visible.
    REQUIRE( data.manager.setLayerVisible( layerA.value(), true ) );
    QApplication::processEvents();
    peerVisible = false;
    for ( QgsLayerTreeLayer *node : peerTree->findLayers() )
    {
        if ( node->layer() == peerLayer )
            peerVisible = node->itemVisibilityChecked();
    }
    CHECK( peerVisible );
}

TEST_CASE( "cursor probe samples the known-answer pixel and drops stale work",
           "[view_link][linked11][probe]" )
{
    KnownAnswerRaster raster;
    QgsRasterLayer layer( raster.path, QStringLiteral( "known" ),
                          QStringLiteral( "gdal" ) );
    REQUIRE( layer.isValid() );

    VaCursorProbe probe( [ &layer ]() -> QgsRasterLayer * { return &layer; } );

    struct SampleRecord
    {
        bool ok = false;
        double value = 0;
        int band = 0;
        bool noData = false;
    };
    QVector<SampleRecord> samples;
    QObject::connect( &probe, &VaCursorProbe::sampled, &probe,
                      [&]( bool ok, double value, int band, bool noData,
                           const QString & ) {
                          samples.append( SampleRecord{ ok, value, band, noData } );
                      } );

    // value(col,row) = row*16 + col; map (3,12) in the raster's own CRS is
    // pixel col 3, row 4 → value 67.
    probe.request( QgsPointXY( 3.0, 12.0 ), layer.crs().toWkt(), 1 );
    REQUIRE( waitFor( [&] { return !samples.isEmpty(); } ) );
    CHECK( samples.first().ok );
    CHECK( samples.first().value == 67.0 );
    CHECK( samples.first().band == 1 );
    CHECK( probe.stats().delivered == 1 );

    // A point outside the raster is an honest "outside", not a wrong value.
    probe.request( QgsPointXY( -500.0, 12.0 ), layer.crs().toWkt(), 1 );
    REQUIRE( waitFor( [&] { return samples.size() >= 2; } ) );
    CHECK_FALSE( samples.at( 1 ).ok );
    CHECK( samples.at( 1 ).noData ); // noData flag = outside
    CHECK( probe.stats().delivered == 2 );

    // Cancel: an in-flight generation never delivers.
    probe.request( QgsPointXY( 1.0, 1.0 ), layer.crs().toWkt(), 1 );
    probe.cancel();
    for ( int i = 0; i < 30; ++i )
    {
        QApplication::processEvents();
        QThread::msleep( 10 );
    }
    CHECK_FALSE( probe.isBusy() );
    CHECK( probe.stats().delivered == 2 ); // nothing new after the cancel
}
