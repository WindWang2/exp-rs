// test_obia_main_window.cpp — OBIA window thin-adapter tests (#663).
//
// The window is a thin client over the rs:obia_* operators: these tests pin
// the ADAPTER contract (dispatched operator ids, parameter mapping,
// schema-driven widget defaults) — the processing semantics themselves are
// pinned by the operator tests (test_obia_operators.cpp) and the Task Center
// seam tests (test_obia_task_center.cpp), not by driving kernels through
// GUI-owned lambdas (that path is deleted).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_main_window.h"
#include "app/obia/rs_obia_operator_adapter.h"

#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/task_center.h"

#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QToolBar>

#include <gdal.h>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_obia_main_window";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
    if ( !QApplication::instance() )
        return new QApplication( fake_argc, fake_argv );
    return static_cast<QApplication *>( QApplication::instance() );
}

bool g_gdalInit = ( GDALAllRegister(), true );

// 2048x2048: the segmentation operator needs seconds on this input, so the
// dispatch tests can assert the submitted contract and cancel BEFORE any
// completion handler (which shows modal message boxes) can run.
QString createLargeTestRaster( const QString &dir )
{
    const int w = 2048;
    const int h = 2048;
    const QString path = dir + QStringLiteral( "/seg_input.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return {};
    QVector<float> row( w );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            row[c] = static_cast<float>( ( r * w + c ) % 251 );
        GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, r, w, 1,
                      row.data(), w, 1, GDT_Float32, 0, 0 );
    }
    double gt[6] = { 100.0, 1.0, 0.0, 200.0, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );
    GDALClose( ds );
    return path;
}

// Small raster for loadRasterFile acceptance tests (no operator run).
QString createTestRaster( const QString &dir )
{
    const QString path = dir + QStringLiteral( "/tiny_input.tif" );
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 16, 16, 1,
                                  GDT_Float32, nullptr );
    if ( !ds )
        return {};
    QVector<float> row( 16 );
    for ( int r = 0; r < 16; ++r )
    {
        for ( int c = 0; c < 16; ++c )
            row[c] = ( c < 8 ) ? 50.0f : 150.0f;
        GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, r, 16, 1,
                      row.data(), 16, 1, GDT_Float32, 0, 0 );
    }
    GDALClose( ds );
    return path;
}

// Matching-size UInt32 labels at an explicit path so rs:obia_features cannot
// fail-fast on a missing file before the dispatch pin inspects the job.
bool createLabelRaster( const QString &path, int w, int h )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1,
                                  GDT_UInt32, nullptr );
    if ( !ds )
        return false;
    QVector<quint32> row( w );
    for ( int r = 0; r < h; ++r )
    {
        for ( int c = 0; c < w; ++c )
            row[c] = static_cast<quint32>( ( r * w + c ) % 17 + 1 );
        GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, r, w, 1,
                      row.data(), w, 1, GDT_UInt32, 0, 0 );
    }
    GDALClose( ds );
    return true;
}

} // namespace

TEST_CASE( "ObiaMainWindow: construction and window chrome", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    REQUIRE( window.windowTitle().contains( "OBIA" ) );
    REQUIRE( window.centralWidget() != nullptr );
    REQUIRE( window.statusBar() != nullptr );
}

TEST_CASE( "ObiaMainWindow: segmentation widget defaults equal the adapter/schema defaults", "[obia][ui][defaults]" )
{
    ensureApp();

    RsObiaMainWindow window;
    window.show();
    QApplication::processEvents();

    // #663 acceptance: dialog options map to operator schema parameters with
    // ONE source of truth — the widgets initialize FROM the rs:obia_segment
    // schema at runtime (rs_obia_main_window.cpp schemaIntDefault). This test
    // pins the widgets to the adapter's mirror of those defaults (the
    // value-level schema pin itself lives in test_obia_operators.cpp, in the
    // operators' link universe).
    const RsObiaMainWindow::SegmentOptions defaults;
    auto *kernelSpin = window.findChild<QSpinBox *>( "kernelSpin" );
    auto *binsSpin = window.findChild<QSpinBox *>( "binsSpin" );
    auto *rangeSpin = window.findChild<QDoubleSpinBox *>( "rangeSpin" );
    auto *minRegionSpin = window.findChild<QSpinBox *>( "minRegionSpin" );

    REQUIRE( kernelSpin != nullptr );
    REQUIRE( binsSpin != nullptr );
    REQUIRE( rangeSpin != nullptr );
    REQUIRE( minRegionSpin != nullptr );
    REQUIRE( kernelSpin->value() == defaults.smoothKernel );
    REQUIRE( binsSpin->value() == defaults.quantizeBins );
    REQUIRE( minRegionSpin->value() == defaults.minRegionSize );
    REQUIRE( rangeSpin->value() == Catch::Approx( defaults.rangeRadius ) );
}

TEST_CASE( "ObiaMainWindow: classifier combo lists the operator methods", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    auto *classifierCombo = window.findChild<QComboBox *>( "classifierCombo" );
    REQUIRE( classifierCombo != nullptr );
    // NormalBayes / SVM / RandomForest / KMeans / MLP — the rs:obia_classify
    // method enum minus the snake_case mapping (adapter-tested below).
    REQUIRE( classifierCombo->count() == 5 );
}

TEST_CASE( "ObiaMainWindow: dock panels exist", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;
    window.show();
    QApplication::processEvents();

    REQUIRE( window.findChild<QDockWidget *>( "obiaClassDock" ) != nullptr );
    REQUIRE( window.findChild<QDockWidget *>( "obiaSegmentDock" ) != nullptr );
}

TEST_CASE( "ObiaMainWindow: loadRasterFile accepts valid rasters", "[obia][ui]" )
{
    ensureApp();

    RsObiaMainWindow window;

    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString raster = createTestRaster( tmp.path() );
    REQUIRE( !raster.isEmpty() );

    // The invalid-path branch shows a modal warning (presentation, not
    // contract) and cannot run headless — only the acceptance path is pinned.
    REQUIRE( window.loadRasterFile( raster ) );
}

TEST_CASE( "ObiaMainWindow: segmentation dispatches the rs:obia_segment operator", "[obia][ui][dispatch]" )
{
    ensureApp();

    RsObiaMainWindow window;

    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString raster = createLargeTestRaster( tmp.path() );
    REQUIRE( !raster.isEmpty() );
    REQUIRE( window.loadRasterFile( raster ) );

    RsObiaMainWindow::SegmentOptions opts;
    opts.rasterPath = raster;
    opts.engine = QStringLiteral( "auto" );
    opts.smoothKernel = 5;
    opts.quantizeBins = 8;
    opts.minRegionSize = 10;
    opts.spatialRadius = 5;
    opts.rangeRadius = 12.5;

    const long taskId = window.startSegmentationTask( opts );
    REQUIRE( taskId > 0 );
    REQUIRE( window.isBusy() );
    REQUIRE( window.pendingOp() == RsObiaMainWindow::PendingOp::Segmentation );

    // THE architectural pin (#663): the GUI submits a real operator id with
    // the adapter's parameter mapping — no GUI-owned executor lambda.
    const auto info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    REQUIRE( info.algorithmId == QStringLiteral( "rs:obia_segment" ) );
    REQUIRE( info.autoLoadLayer == false );
    REQUIRE( info.jobRequest.params["input"].asString() == raster.toStdString() );
    REQUIRE( info.jobRequest.params["engine"].asString() == "auto" );
    REQUIRE( info.jobRequest.params["smoothKernel"].asInt() == 5 );
    REQUIRE( info.jobRequest.params["quantizeBins"].asInt() == 8 );
    REQUIRE( info.jobRequest.params["minRegionSize"].asInt() == 10 );
    REQUIRE( info.jobRequest.params["spatialRadius"].asInt() == 5 );
    REQUIRE( info.jobRequest.params["rangeRadius"].asDouble() == Catch::Approx( 12.5 ) );

    // Cancel restores the single-flight slot without executing kernels.
    window.cancelActiveTask();
    REQUIRE( !window.isBusy() );
    REQUIRE( window.pendingOp() == RsObiaMainWindow::PendingOp::None );
}

TEST_CASE( "ObiaMainWindow: busy gate rejects concurrent tasks", "[obia][ui][dispatch]" )
{
    ensureApp();

    RsObiaMainWindow window;

    QTemporaryDir tmp;
    const QString raster = createTestRaster( tmp.path() );
    REQUIRE( !raster.isEmpty() );
    REQUIRE( window.loadRasterFile( raster ) );

    RsObiaMainWindow::SegmentOptions opts;
    opts.rasterPath = raster;
    const long first = window.startSegmentationTask( opts );
    REQUIRE( first > 0 );

    RsObiaMainWindow::SegmentOptions second = opts;
    REQUIRE( window.startSegmentationTask( second ) == -1 );

    window.cancelActiveTask();
    REQUIRE( !window.isBusy() );
}

TEST_CASE( "ObiaMainWindow: hierarchy-chain features uses the fine labels raster", "[obia][ui][dispatch]" )
{
    ensureApp();

    RsObiaMainWindow window;

    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString raster = createLargeTestRaster( tmp.path() );
    REQUIRE( !raster.isEmpty() );
    REQUIRE( window.loadRasterFile( raster ) );

    // Stage hierarchy outputs the same way the Hierarchy button does, then
    // cancel before the operator finishes — mHasHierarchy is still false,
    // which is the chain-time state inside PendingOp::Hierarchy.
    const long hierId = window.startHierarchyTask( 5, 15.0, 10 );
    REQUIRE( hierId > 0 );
    REQUIRE( window.pendingOp() == RsObiaMainWindow::PendingOp::Hierarchy );

    const auto hierInfo = sicnu::TaskCenter::instance().getTaskInfo( hierId );
    REQUIRE( hierInfo.algorithmId == QStringLiteral( "rs:obia_hierarchy" ) );
    const QString finePath = QString::fromStdString(
        hierInfo.jobRequest.params["outputFine"].asString() );
    REQUIRE( QFileInfo( finePath ).fileName() == QStringLiteral( "hier_fine.tif" ) );

    window.cancelActiveTask();
    REQUIRE( !window.isBusy() );

    REQUIRE( createLabelRaster( finePath, 2048, 2048 ) );

    // The chained call PendingOp::Hierarchy makes after rehydrate:
    // startFeaturesTask( 0, /*afterHierarchyBuild=*/true ). Labels must be
    // the hierarchy fine raster, not empty / not the flat seg_labels.tif.
    const long featId = window.startFeaturesTask( 0, /*afterHierarchyBuild=*/true );
    REQUIRE( featId > 0 );
    REQUIRE( window.pendingOp() == RsObiaMainWindow::PendingOp::HierarchyFeatures );

    const auto featInfo = sicnu::TaskCenter::instance().getTaskInfo( featId );
    REQUIRE( featInfo.algorithmId == QStringLiteral( "rs:obia_features" ) );
    const QString labels = QString::fromStdString(
        featInfo.jobRequest.params["labels"].asString() );
    REQUIRE( labels == finePath );
    REQUIRE( QFileInfo( labels ).fileName() == QStringLiteral( "hier_fine.tif" ) );
    REQUIRE( !labels.contains( QStringLiteral( "seg_labels.tif" ) ) );

    window.cancelActiveTask();
    REQUIRE( !window.isBusy() );
}

TEST_CASE( "ObiaMainWindow: classifier label → operator method mapping", "[obia][adapter]" )
{
    using RsObiaOperatorAdapter::methodForClassifierLabel;
    REQUIRE( methodForClassifierLabel( QStringLiteral( "NormalBayes" ) ) == QStringLiteral( "normal_bayes" ) );
    REQUIRE( methodForClassifierLabel( QStringLiteral( "SVM" ) ) == QStringLiteral( "svm" ) );
    REQUIRE( methodForClassifierLabel( QStringLiteral( "RandomForest" ) ) == QStringLiteral( "random_forest" ) );
    REQUIRE( methodForClassifierLabel( QStringLiteral( "KMeans" ) ) == QStringLiteral( "kmeans" ) );
    REQUIRE( methodForClassifierLabel( QStringLiteral( "MLP" ) ) == QStringLiteral( "mlp" ) );
}

TEST_CASE( "ObiaAdapter: params builders carry the operator contract", "[obia][adapter]" )
{
    // Segment
    RsObiaMainWindow::SegmentOptions opts;
    opts.rasterPath = QStringLiteral( "/in.tif" );
    opts.outputLabelsPath = QStringLiteral( "/out.tif" );
    const Json::Value seg = RsObiaOperatorAdapter::buildSegmentParams( opts );
    REQUIRE( seg["engine"].asString() == "auto" );
    REQUIRE( seg["input"].asString() == "/in.tif" );
    REQUIRE( seg["threshold"].asDouble() == Catch::Approx( 0.1 ) );

    // Classify
    const Json::Value cls = RsObiaOperatorAdapter::buildFlatClassifyParams(
        QStringLiteral( "/in.tif" ), QStringLiteral( "/labels.tif" ),
        QStringLiteral( "/class.tif" ), { { 1, 1 }, { 2, 2 } },
        RsObiaOperatorAdapter::ClassifierOptions{}, RsFeatureSelection{},
        { { 1, QColor( 255, 0, 0 ) } }, QStringLiteral( "/u.csv" ) );
    REQUIRE( cls["labels"].asString() == "/labels.tif" );
    REQUIRE( cls["features"].asString() == "full" );
    REQUIRE( cls["segmentClasses"]["1"].asInt() == 1 );
    REQUIRE( cls["classColors"]["1"].asString() == "#ff0000" );
    REQUIRE( cls["outputUncertainty"].asString() == "/u.csv" );
    REQUIRE( cls["featureSelection"].isObject() );
    REQUIRE( cls["featureSelection"].size() == 14 );

    // Hierarchy classify round-trip pieces
    const Json::Value hier = RsObiaOperatorAdapter::buildHierarchyClassifyParams(
        QStringLiteral( "/in.tif" ), QStringLiteral( "/fine.tif" ),
        QStringLiteral( "/coarse.tif" ), QStringLiteral( "/parents.csv" ),
        QStringLiteral( "/class.tif" ), 1, { { 3, 2 } },
        RsObiaOperatorAdapter::ClassifierOptions{}, {}, QString() );
    REQUIRE( hier["labelsFine"].asString() == "/fine.tif" );
    REQUIRE( hier["labelsCoarse"].asString() == "/coarse.tif" );
    REQUIRE( hier["parents"].asString() == "/parents.csv" );
    REQUIRE( hier["classifyLevel"].asInt() == 1 );
    REQUIRE( hier.isMember( "outputUncertainty" ) == false );
}

#include "app/obia/rs_segment_select_tool.h"
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>

TEST_CASE( "OBIA/MapTools: RsSegmentSelectTool rubber band canvas destruction safety", "[obia][maptool]" )
{
    ensureApp();

    auto canvas = std::make_unique<QgsMapCanvas>();
    auto tool = std::make_unique<RsSegmentSelectTool>( canvas.get() );

    QVector<quint32> labels = { 1, 1, 1, 1 };
    RsSegmentMap segMap( labels, 2, 2 );
    tool->setSegmentMap( segMap );

    tool->clearSelection();

    // Clean teardown: destroy tool before canvas
    tool.reset();
    canvas.reset();
}

#endif // SICNU_HAS_OPENCV
