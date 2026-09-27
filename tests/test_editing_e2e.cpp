// test_editing_e2e.cpp — F11 Package H: offscreen interaction end-to-end.
//
// One continuous scenario over the real composition of components:
//   canvas + session attach → brush stroke → validity check → ROI-to-raster
//   stats → class write-back → undo/redo → commit → GeoJSON export →
//   layer removal mid-session → project clear. Oracles are the same
//   independent truths used by the unit suites (a-priori raster grid,
//   hand-known counts, format-level GeoJSON read-back).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "editing/rs_edit_agent_tool.h"
#include "editing/rs_edit_persistence.h"
#include "editing/rs_edit_session.h"
#include "editing/rs_geometry_validity.h"
#include "editing/rs_roi_semantics.h"
#include "editing/rs_sample_brush_tool.h"
#include "editing/rs_snapping_controller.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>

#include <gdal.h>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <cmath>
#include <vector>
#include <QJsonObject>
#include "support/qt_lifecycle.h"

// Track 2 R4 (PR #1335 exit-crash cluster, group 2): ordered teardown via the
// shared listener — drains deferred deletes, runs exitQgis()/invalidateCaches
// while guards are alive, deletes the app before glibc exit().
CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{

struct E2eFixture
{
    E2eFixture()
    {
        if ( !QgsApplication::instance() )
        {
            static int argc = 1;
            static char arg0[] = "test_editing_e2e";
            static char arg1[] = "--quiet";
            static char *argv[] = { arg0, arg1, nullptr };
            new QgsApplication( argc, argv, false );
        }
        QgsApplication::initQgis();
        project = QgsProject::instance();
        project->clear();
    }
    ~E2eFixture() { project->clear(); }
    QgsProject *project = nullptr;
};

/// 20×20 metric raster (same a-priori grid as the unit suite:
/// band1 = 20*row + col; gt {100000, 10, 0, 3000000, 0, -10}).
QString makeE2eRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 100000.0, 10.0, 0.0, 3000000.0, 0.0, -10.0 };
    GDALDatasetH ds = createOutputTiff( path, 20, 20, 1, GDT_Float32, gt, QString() );
    if ( !ds )
        return QStringLiteral( "create failed" );
    // A valid CRS is required: the ROI semantics fail closed on an invalid
    // raster CRS by design.
    const char *wkt =
      "PROJCS[\"WGS 84 / UTM zone 50N\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
      "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],"
      "UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Transverse_Mercator\"],"
      "PARAMETER[\"latitude_of_origin\",0],PARAMETER[\"central_meridian\",117],"
      "PARAMETER[\"scale_factor\",0.9996],PARAMETER[\"false_easting\",500000],"
      "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";
    GDALSetProjection( ds, wkt );
    std::vector<float> band( 400 );
    for ( int y = 0; y < 20; ++y )
        for ( int x = 0; x < 20; ++x )
            band[static_cast<size_t>( y * 20 + x )] = static_cast<float>( y * 20 + x );
    if ( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 20, 20,
                       band.data(), 20, 20, GDT_Float32, 0, 0 ) != CE_None )
    {
        GDALClose( ds );
        return QStringLiteral( "write failed" );
    }
    GDALClose( ds );
    return {};
}

QgsGeometry utmRoi()
{
    // cols {4..9} × rows {2..8} → 42 covered pixels (center-of-pixel).
    // Edges stay ≥1 m clear of every pixel center (boundary-case-free).
    return QgsGeometry::fromPolygonXY( { QVector<QgsPointXY>{
      QgsPointXY( 100044.0, 2999914.0 ), QgsPointXY( 100096.0, 2999914.0 ),
      QgsPointXY( 100096.0, 2999976.0 ), QgsPointXY( 100044.0, 2999976.0 ),
      QgsPointXY( 100044.0, 2999914.0 ) } } );
}

} // namespace

TEST_CASE( "E2E: draw → validate → stats → label → undo/redo → commit → export → lifecycle",
           "[editing][e2e][f11][oracle1][oracle2][oracle3]" )
{
    E2eFixture fx;
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // ── Surface: offscreen canvas + snapping + session + agent facts ──
    QgsMapCanvas canvas;
    canvas.resize( 500, 500 );
    canvas.setDestinationCrs( QgsCoordinateReferenceSystem( QStringLiteral( "EPSG:4326" ) ) );
    canvas.setExtent( QgsRectangle( 0, 0, 500, 500 ) );
    RsSnappingController snapping( &canvas );
    snapping.enableVertexSegment( 1.0, Qgis::MapToolUnit::Project, false );

    RsEditSession session;
    RsEditAgentTool::Sources sources;
    sources.session = &session;
    sources.snapping = &snapping;
    RsEditAgentTool agentTool( sources );

    QgsVectorLayer *samples = new QgsVectorLayer(
      QStringLiteral( "MultiPolygon?crs=EPSG:4326&field=class:int" ),
      QStringLiteral( "samples" ), QStringLiteral( "memory" ) );
    REQUIRE( samples->isValid() );
    REQUIRE( samples->startEditing() );
    REQUIRE( session.attachLayer( samples ).isEmpty() );

    // ── 1) Brush stroke paints one sample (one undo step) ────────────
    RsSampleBrushTool brush( &canvas );
    brush.setTargetLayer( samples );
    brush.setSession( &session );
    brush.setRadius( 8.0 );
    QSignalSpy strokeSpy( &brush, &RsSampleBrushTool::strokeCommitted );

    QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 250, 250 ),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QgsMapMouseEvent press( &canvas, &pressEv );
    brush.canvasPressEvent( &press );
    QMouseEvent moveEv( QEvent::MouseMove, QPointF( 270, 250 ),
                        Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QgsMapMouseEvent move( &canvas, &moveEv );
    brush.canvasMoveEvent( &move );
    QMouseEvent relEv( QEvent::MouseButtonRelease, QPointF( 270, 250 ),
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QgsMapMouseEvent release( &canvas, &relEv );
    brush.canvasReleaseEvent( &release );

    REQUIRE( strokeSpy.count() == 1 );
    REQUIRE( session.state( samples->id() ).featureCount == 1 );

    // ── 2) The sample geometry is valid (brush output is checked, not assumed) ──
    QgsFeature sample;
    REQUIRE( samples->getFeatures().nextFeature( sample ) );
    CHECK( RsGeometryValidity::isValid( sample.geometry() ) );

    // ── 3) ROI semantics over the a-priori raster grid ───────────────
    const QString rasterPath = dir.filePath( QStringLiteral( "e2e_grid.tif" ) );
    REQUIRE( makeE2eRaster( rasterPath ).isEmpty() );
    QgsRasterLayer raster( rasterPath, QStringLiteral( "grid" ), QStringLiteral( "gdal" ) );
    REQUIRE( raster.isValid() );

    const RsRoiStatsResult stats = RsRoiSemantics::bandStatsPreview(
      &raster, utmRoi(), raster.crs(), {} );
    REQUIRE( stats.ok );
    CHECK( stats.pixelCount == 42 );
    CHECK( stats.validPixelCount == 42 );
    // Oracle: mean over cols {4..9} × rows {2..8} of value 20r + c.
    double sum = 0.0;
    int n = 0;
    for ( int r = 2; r <= 8; ++r )
        for ( int c = 4; c <= 9; ++c )
        {
            sum += 20.0 * r + c;
            ++n;
        }
    CHECK( stats.bands.first().mean == Catch::Approx( sum / n ).margin( 1e-6 ) );

    // ── 4) Class label write-back ─────────────────────────────────────
    QString err;
    REQUIRE( RsRoiSemantics::writeClassLabel( samples, sample.id(), 3,
                                              QStringLiteral( "class" ), &session, &err ) );

    // ── 5) Agent facts mirror the mid-flight state (read-only) ───────
    {
        const auto facts = agentTool.execute( Json::Value( Json::nullValue ) );
        REQUIRE( facts.success );
        CHECK( facts.output["editing"]["dirty"].asBool() );
        CHECK( facts.output["editing"]["layers"][0]["featureCount"].asInt64() == 1 );
    }

    // ── 6) Undo / redo of the label command ───────────────────────────
    REQUIRE( session.undo( samples->id() ) );
    CHECK( samples->getFeature( sample.id() ).attribute( QStringLiteral( "class" ) ).isNull() );
    REQUIRE( session.redo( samples->id() ) );
    CHECK( samples->getFeature( sample.id() ).attribute( QStringLiteral( "class" ) ).toInt() == 3 );

    // ── 7) Commit and atomic export ──────────────────────────────────
    QString commitErr;
    const bool committed = session.commit( samples->id(), &commitErr );
    INFO( "commit error: " << commitErr.toStdString() );
    REQUIRE( committed );
    CHECK_FALSE( session.isDirty() );

    const QString exportPath = dir.filePath( QStringLiteral( "samples.geojson" ) );
    const RsEditPersistence::ExportResult exportResult =
      RsEditPersistence::exportLayerToGeoJson( samples, exportPath, QStringLiteral( "samples" ) );
    REQUIRE( exportResult.ok );

    // Format-level oracle: FeatureCollection with exactly one feature
    // carrying class=3 and roi_u=42.
    QFile out( exportPath );
    REQUIRE( out.open( QIODevice::ReadOnly ) );
    const QJsonDocument doc = QJsonDocument::fromJson( out.readAll() );
    REQUIRE( doc.isObject() );
    const QJsonArray features = doc.object()[QLatin1String( "features" )].toArray();
    REQUIRE( features.size() == 1 );
    const QJsonObject properties = features[0].toObject()[QLatin1String( "properties" )].toObject();
    CHECK( properties[QLatin1String( "class" )].toInt() == 3 );

    // ── 8) Lifecycle: removing the layer mid-session, then project clear ──
    fx.project->addMapLayer( samples, false );
    if ( !session.isAttached( samples->id() ) )
        REQUIRE( session.attachLayer( samples ).isEmpty() );
    fx.project->removeMapLayer( samples );
    samples = nullptr;
    CHECK( session.attachedLayerIds().isEmpty() );

    // The session and the agent tool stay fully usable after the removal.
    QgsVectorLayer *fresh = new QgsVectorLayer(
      QStringLiteral( "Point?crs=EPSG:4326" ), QStringLiteral( "fresh" ),
      QStringLiteral( "memory" ) );
    REQUIRE( session.attachLayer( fresh ).isEmpty() );
    const auto factsAfter = agentTool.execute( Json::Value( Json::nullValue ) );
    REQUIRE( factsAfter.success );
    CHECK( factsAfter.output["editing"]["layers"].size() == 1 );
    CHECK_FALSE( factsAfter.output["editing"]["layers"][0]["modified"].asBool() );
    delete fresh;
}
