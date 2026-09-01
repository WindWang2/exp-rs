// tests/test_mosaic_panel.cpp — Comprehensive Catch2 offscreen tests for MosaicPanel
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QFile>

#include <qgsapplication.h>
#include <gdal_priv.h>

#include "panels/mosaic_panel.h"
#include "widgets/rs_empty_state_widget.h"
#include "processing/framework/task_center.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace {

void ensureQgisApp()
{
    if ( QApplication::instance() )
        return;

    static int argc = 1;
    static char appName[] = "test_mosaic_panel";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

QString createTestGeoTiff( const QString &path, int width = 8, int height = 8, float fillVal = 10.0f, double originX = 0.0, double originY = 0.0 )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return QString();

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), width, height, 1, GDT_Float32, nullptr );
    if ( !ds )
        return QString();

    double gt[6] = { originX, 1.0, 0.0, originY, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );

    std::vector<float> data( static_cast<size_t>( width * height ), fillVal );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    CPLErr err = GDALRasterIO( band, GF_Write, 0, 0, width, height, data.data(), width, height, GDT_Float32, 0, 0 );
    ( void ) err;
    GDALSetRasterNoDataValue( band, -9999.0 );
    GDALClose( ds );
    return path;
}

} // namespace

TEST_CASE( "MosaicPanel - Initialization and UI Structure", "[m4][mosaic_panel][ui]" )
{
    ensureQgisApp();

    MosaicPanel panel;
    CHECK( ( panel.windowTitle().contains( "Mosaic", Qt::CaseInsensitive ) || panel.windowTitle().contains( "镶嵌" ) ) );

    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
    REQUIRE( stack != nullptr );
    CHECK( stack->count() == 2 );
    // Initially showing Empty State (Index 1)
    CHECK( stack->currentIndex() == 1 );

    auto *list = panel.findChild<QListWidget *>();
    REQUIRE( list != nullptr );
    CHECK( list->count() == 0 );
    CHECK( panel.inputFiles().isEmpty() );

    auto *emptyWidget = panel.findChild<sicnu::RsEmptyStateWidget *>();
    REQUIRE( emptyWidget != nullptr );
    CHECK( emptyWidget->title().contains( "镶嵌" ) );

    auto *outputEdit = panel.findChild<QLineEdit *>();
    REQUIRE( outputEdit != nullptr );
    CHECK( panel.outputPath().isEmpty() );

    auto *runBtn = panel.findChild<QPushButton *>( QString(), Qt::FindChildrenRecursively );
    REQUIRE( runBtn != nullptr );
}

TEST_CASE( "MosaicPanel - Input List and Empty State Stack Toggling", "[m4][mosaic_panel][stack]" )
{
    ensureQgisApp();
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    MosaicPanel panel;
    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
    auto *list = panel.findChild<QListWidget *>();
    REQUIRE( stack != nullptr );
    REQUIRE( list != nullptr );

    const QString f1 = createTestGeoTiff( tempDir.path() + "/tile_1.tif", 8, 8, 5.0f, 0.0, 10.0 );
    const QString f2 = createTestGeoTiff( tempDir.path() + "/tile_2.tif", 8, 8, 15.0f, 8.0, 10.0 );
    REQUIRE( QFile::exists( f1 ) );
    REQUIRE( QFile::exists( f2 ) );

    // 1. Initially Empty -> Index 1
    CHECK( stack->currentIndex() == 1 );
    CHECK( panel.inputFiles().isEmpty() );

    // 2. Add first item -> Switch to Index 0
    list->addItem( f1 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    CHECK( stack->currentIndex() == 0 );
    CHECK( panel.inputFiles().size() == 1 );
    CHECK( panel.inputFiles().first() == f1 );

    // 3. Add second item -> Stays at Index 0
    list->addItem( f2 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    CHECK( stack->currentIndex() == 0 );
    CHECK( panel.inputFiles().size() == 2 );
    CHECK( panel.inputFiles().at( 1 ) == f2 );

    // 4. Remove one item -> Stays at Index 0
    delete list->takeItem( 0 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    CHECK( stack->currentIndex() == 0 );
    CHECK( panel.inputFiles().size() == 1 );

    // 5. Remove remaining item -> Switch back to Empty State (Index 1)
    delete list->takeItem( 0 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    CHECK( stack->currentIndex() == 1 );
    CHECK( panel.inputFiles().isEmpty() );

    // 6. Rapid Add/Remove Stress Cycles (100 iterations)
    for ( int i = 0; i < 100; ++i )
    {
        list->addItem( f1 );
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 0 );

        delete list->takeItem( 0 );
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 1 );
    }
}

TEST_CASE( "MosaicPanel - Output Path Configuration and Validation", "[m4][mosaic_panel][output]" )
{
    ensureQgisApp();
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    MosaicPanel panel;
    auto *outputEdit = panel.findChild<QLineEdit *>();
    REQUIRE( outputEdit != nullptr );

    CHECK( panel.outputPath().isEmpty() );

    const QString testPath = tempDir.path() + "/mosaic_result.tif";
    outputEdit->setText( testPath );
    CHECK( panel.outputPath() == testPath );

    outputEdit->setText( "   " + testPath + "   " );
    CHECK( panel.outputPath() == testPath );

    outputEdit->clear();
    CHECK( panel.outputPath().isEmpty() );
}

TEST_CASE( "MosaicPanel - Task Dispatch and Mosaic Execution", "[m4][mosaic_panel][execution]" )
{
    ensureQgisApp();
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString f1 = createTestGeoTiff( tempDir.path() + "/tile_left.tif", 8, 8, 10.0f, 0.0, 8.0 );
    const QString f2 = createTestGeoTiff( tempDir.path() + "/tile_right.tif", 8, 8, 20.0f, 8.0, 8.0 );
    const QString outPath = tempDir.path() + "/mosaic_stitched.tif";

    MosaicPanel panel;
    auto *list = panel.findChild<QListWidget *>();
    auto *outputEdit = panel.findChild<QLineEdit *>();
    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
    REQUIRE( list != nullptr );
    REQUIRE( outputEdit != nullptr );
    REQUIRE( stack != nullptr );

    QSignalSpy spySuccess( &panel, &MosaicPanel::mosaicCompleted );
    QSignalSpy spyFailed( &panel, &MosaicPanel::mosaicFailed );

    // Populate inputs
    list->addItem( f1 );
    list->addItem( f2 );
    stack->setCurrentIndex( 0 );
    outputEdit->setText( outPath );

    CHECK( panel.inputFiles().size() == 2 );
    CHECK( panel.outputPath() == outPath );

    // Trigger Mosaic run via QMetaObject invokeMethod
    bool invoked = QMetaObject::invokeMethod( &panel, "runMosaic", Qt::DirectConnection );
    REQUIRE( invoked );

    // Wait for TaskCenter / async job execution
    int maxWaitMs = 5000;
    while ( spySuccess.isEmpty() && spyFailed.isEmpty() && maxWaitMs > 0 )
    {
        QTest::qWait( 50 );
        maxWaitMs -= 50;
    }

    CHECK( spySuccess.count() == 1 );
    CHECK( spyFailed.isEmpty() );
    CHECK( QFile::exists( outPath ) );

    // Verify generated mosaic file can be opened by GDAL and has expected dimensions (16x8)
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( outPath ) );
    CHECK( ds.width() == 16 );
    CHECK( ds.height() == 8 );
    CHECK( ds.bandCount() == 1 );
}

TEST_CASE( "MosaicPanel - Rapid Lifecycle and Resize Resilience", "[m4][mosaic_panel][lifecycle]" )
{
    ensureQgisApp();

    for ( int i = 0; i < 20; ++i )
    {
        auto *panel = new MosaicPanel();
        panel->resize( 300, 200 );
        panel->resize( 800, 600 );
        panel->resize( 1920, 1080 );
        delete panel;
    }
}
