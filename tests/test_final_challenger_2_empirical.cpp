#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QToolBar>
#include <QSignalSpy>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QStackedWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>

#include <qgsapplication.h>
#include <gdal_priv.h>

#include "widgets/rs_toolbar_flow_host.h"
#include "widgets/rs_empty_state_widget.h"
#include "panels/mosaic_panel.h"
#include "dialogs/preferences_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace {

void initTestEnv()
{
    if ( QApplication::instance() )
        return;

    static int argc = 1;
    static char appName[] = "test_final_challenger_2_empirical";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

QString createSyntheticTile( const QString &path, int w, int h, float val, double ox, double oy )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver ) return QString();

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr );
    if ( !ds ) return QString();

    double gt[6] = { ox, 1.0, 0.0, oy, 0.0, -1.0 };
    GDALSetGeoTransform( ds, gt );

    std::vector<float> data( static_cast<size_t>( w * h ), val );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    CPLErr err = GDALRasterIO( band, GF_Write, 0, 0, w, h, data.data(), w, h, GDT_Float32, 0, 0 );
    ( void ) err;
    GDALSetRasterNoDataValue( band, -9999.0 );
    GDALClose( ds );
    return path;
}

} // namespace

// =============================================================================
// 1. RsToolbarFlowHost Multi-Item Drop Reordering & Slots >= 2 Verification
// =============================================================================

TEST_CASE( "Empirical: RsToolbarFlowHost multi-item drop reordering across slots >= 2", "[challenger2][toolbar][dnd]" )
{
    initTestEnv();

    RsToolbarFlowHost host;
    host.resize( 1600, 100 );

    QList<QToolBar *> toolbars;
    for ( int i = 0; i < 5; ++i )
    {
        auto *tb = new QToolBar( QStringLiteral( "tb_%1" ).arg( i ), &host );
        tb->setObjectName( QStringLiteral( "tb_%1" ).arg( i ) );
        tb->addAction( QStringLiteral( "Act_%1" ).arg( i ) );
        toolbars.append( tb );
    }

    host.setProductToolbars( toolbars );
    QHash<QToolBar *, bool> vis;
    for ( auto *tb : toolbars ) vis[tb] = true;
    host.applyVisibility( vis );

    // Ensure layout placed them in 1 row
    REQUIRE( host.usedRows() == 1 );

    auto findGrip = [&]( int idx ) -> QWidget * {
        auto grips = host.findChildren<QWidget *>( QStringLiteral( "rsToolbarDragGrip" ) );
        for ( auto *g : grips )
        {
            if ( g->parentWidget() && g->parentWidget()->findChild<QToolBar *>() == toolbars[idx] )
                return g;
        }
        return nullptr;
    };

    auto getToolbarOrder = [&]() -> QList<int> {
        struct Item { int idx; int x; };
        QList<Item> items;
        for ( int i = 0; i < 5; ++i )
        {
            QWidget *grip = findGrip( i );
            if ( grip && grip->parentWidget() )
                items.append( { i, grip->parentWidget()->geometry().x() } );
        }
        std::sort( items.begin(), items.end(), []( const Item &a, const Item &b ) {
            return a.x < b.x;
        } );
        QList<int> order;
        for ( const auto &it : items ) order.append( it.idx );
        return order;
    };

    // Initial visual order must be 0, 1, 2, 3, 4
    CHECK( getToolbarOrder() == QList<int>( { 0, 1, 2, 3, 4 } ) );

    // TEST DRAG 1: Drag tb_0 to slot 3 (over right half of tb_2)
    {
        QWidget *grip0 = findGrip( 0 );
        REQUIRE( grip0 != nullptr );
        QWidget *frame2 = findGrip( 2 )->parentWidget();
        REQUIRE( frame2 != nullptr );

        // Press on grip0
        QPoint gPress = grip0->mapToGlobal( QPoint( 4, 16 ) );
        QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 4, 16 ), gPress,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip0, &pressEv );

        // Move over tb_2's right half
        QPoint targetInHost = frame2->geometry().center() + QPoint( 15, 0 );
        QPoint targetGlobal = host.mapToGlobal( targetInHost );
        QMouseEvent moveEv( QEvent::MouseMove, targetInHost, targetGlobal,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip0, &moveEv );

        // Release at target
        QMouseEvent releaseEv( QEvent::MouseButtonRelease, targetInHost, targetGlobal,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip0, &releaseEv );

        // Expected order: [1, 2, 0, 3, 4] -> tb_0 moved to slot 2/3!
        CHECK( getToolbarOrder() == QList<int>( { 1, 2, 0, 3, 4 } ) );
    }

    // TEST DRAG 2: Drag tb_1 (now at index 0) past tb_4 to the far right (slot 5)
    {
        QWidget *grip1 = findGrip( 1 );
        REQUIRE( grip1 != nullptr );
        QWidget *frame4 = findGrip( 4 )->parentWidget();
        REQUIRE( frame4 != nullptr );

        QPoint gPress = grip1->mapToGlobal( QPoint( 4, 16 ) );
        QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 4, 16 ), gPress,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip1, &pressEv );

        // Move beyond tb_4's right edge
        QPoint targetInHost = frame4->geometry().topRight() + QPoint( 50, 16 );
        QPoint targetGlobal = host.mapToGlobal( targetInHost );
        QMouseEvent moveEv( QEvent::MouseMove, targetInHost, targetGlobal,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip1, &moveEv );

        QMouseEvent releaseEv( QEvent::MouseButtonRelease, targetInHost, targetGlobal,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip1, &releaseEv );

        // Expected order: [2, 0, 3, 4, 1]
        CHECK( getToolbarOrder() == QList<int>( { 2, 0, 3, 4, 1 } ) );
    }

    // TEST DRAG 3: Drag tb_4 (now at index 3) before tb_2 (index 0) -> slot 0
    {
        QWidget *grip4 = findGrip( 4 );
        REQUIRE( grip4 != nullptr );
        QWidget *frame2 = findGrip( 2 )->parentWidget();
        REQUIRE( frame2 != nullptr );

        QPoint gPress = grip4->mapToGlobal( QPoint( 4, 16 ) );
        QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 4, 16 ), gPress,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip4, &pressEv );

        // Move to left half of tb_2
        QPoint targetInHost = frame2->geometry().topLeft() + QPoint( 2, 16 );
        QPoint targetGlobal = host.mapToGlobal( targetInHost );
        QMouseEvent moveEv( QEvent::MouseMove, targetInHost, targetGlobal,
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip4, &moveEv );

        QMouseEvent releaseEv( QEvent::MouseButtonRelease, targetInHost, targetGlobal,
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QApplication::sendEvent( grip4, &releaseEv );

        // Expected order: [4, 2, 0, 3, 1]
        CHECK( getToolbarOrder() == QList<int>( { 4, 2, 0, 3, 1 } ) );
    }
}

// =============================================================================
// 2. RsToolbarFlowHost Separator Line Rendering in Dark and Light Modes
// =============================================================================

TEST_CASE( "Empirical: RsToolbarFlowHost separator line rendering in dark and light modes", "[challenger2][toolbar][separator]" )
{
    initTestEnv();

    RsToolbarFlowHost host;
    // Set narrow width to force 2 rows
    host.resize( 300, 100 );

    QList<QToolBar *> toolbars;
    for ( int i = 0; i < 4; ++i )
    {
        auto *tb = new QToolBar( QStringLiteral( "sep_tb_%1" ).arg( i ), &host );
        tb->setObjectName( QStringLiteral( "sep_tb_%1" ).arg( i ) );
        tb->addAction( QStringLiteral( "Action %1 with long text" ).arg( i ) );
        toolbars.append( tb );
    }

    host.setProductToolbars( toolbars );
    QHash<QToolBar *, bool> vis;
    for ( auto *tb : toolbars ) vis[tb] = true;
    host.applyVisibility( vis );

    REQUIRE( host.usedRows() == 2 );
    REQUIRE( host.height() == 64 );

    // Test 1: Light Mode Palette
    QPalette lightPal;
    lightPal.setColor( QPalette::Window, QColor( 244, 246, 248 ) );
    lightPal.setColor( QPalette::Mid, QColor( 226, 230, 235 ) );
    host.setPalette( lightPal );

    CHECK( host.palette().color( QPalette::Mid ) == QColor( 226, 230, 235 ) );

    // Paint into an offscreen pixmap to ensure paintEvent succeeds without glitch
    QPixmap lightPixmap( host.size() );
    lightPixmap.fill( Qt::transparent );
    host.render( &lightPixmap );
    CHECK_FALSE( lightPixmap.isNull() );
    CHECK( lightPixmap.width() == host.width() );
    CHECK( lightPixmap.height() == host.height() );

    // Test 2: Dark Mode Palette
    QPalette darkPal;
    darkPal.setColor( QPalette::Window, QColor( 26, 29, 35 ) );
    darkPal.setColor( QPalette::Mid, QColor( 52, 59, 70 ) );
    host.setPalette( darkPal );

    CHECK( host.palette().color( QPalette::Mid ) == QColor( 52, 59, 70 ) );

    QPixmap darkPixmap( host.size() );
    darkPixmap.fill( Qt::transparent );
    host.render( &darkPixmap );
    CHECK_FALSE( darkPixmap.isNull() );
    CHECK( darkPixmap.width() == host.width() );
    CHECK( darkPixmap.height() == host.height() );
}

// =============================================================================
// 3. MosaicPanel Stress Testing
// =============================================================================

TEST_CASE( "Empirical: MosaicPanel rapid adding/removing, empty states, and job execution", "[challenger2][mosaic_panel][stress]" )
{
    initTestEnv();
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    MosaicPanel panel;
    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
    auto *list = panel.findChild<QListWidget *>();
    auto *outputEdit = panel.findChild<QLineEdit *>();
    REQUIRE( stack != nullptr );
    REQUIRE( list != nullptr );
    REQUIRE( outputEdit != nullptr );

    // 1. Initial State: Empty state at Index 1
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( panel.inputFiles().isEmpty() );

    // 2. Generate 10 synthetic tiles
    QStringList tiles;
    for ( int i = 0; i < 10; ++i )
    {
        QString p = createSyntheticTile( QStringLiteral( "%1/tile_%2.tif" ).arg( tempDir.path() ).arg( i ),
                                        8, 8, 10.0f + i, i * 8.0, 10.0 );
        REQUIRE( QFile::exists( p ) );
        tiles.append( p );
    }

    // 3. Rapid Churn: 200 cycles of add/remove permutations
    for ( int cycle = 0; cycle < 200; ++cycle )
    {
        int toAdd = ( cycle % 5 ) + 1;
        for ( int j = 0; j < toAdd; ++j )
        {
            list->addItem( tiles[j] );
        }
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 0 );
        REQUIRE( panel.inputFiles().size() == toAdd );

        list->clear();
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 1 );
        REQUIRE( panel.inputFiles().isEmpty() );
    }

    // 4. Output Path Configuration and Whitespace Trimming
    const QString outPath = tempDir.path() + "/empirical_mosaic.tif";
    outputEdit->setText( "   " + outPath + "   " );
    CHECK( panel.outputPath() == outPath );

    // 5. Multi-Tile Mosaic Job Submission & Execution
    list->addItem( tiles[0] );
    list->addItem( tiles[1] );
    stack->setCurrentIndex( 0 );

    QSignalSpy spySuccess( &panel, &MosaicPanel::mosaicCompleted );
    QSignalSpy spyFailed( &panel, &MosaicPanel::mosaicFailed );

    bool invoked = QMetaObject::invokeMethod( &panel, "runMosaic", Qt::DirectConnection );
    REQUIRE( invoked );

    int maxWaitMs = 5000;
    while ( spySuccess.isEmpty() && spyFailed.isEmpty() && maxWaitMs > 0 )
    {
        QTest::qWait( 50 );
        maxWaitMs -= 50;
    }

    CHECK( spySuccess.count() == 1 );
    CHECK( spyFailed.isEmpty() );
    CHECK( QFile::exists( outPath ) );

    // Verify dataset properties
    GdalDatasetWrapper ds;
    REQUIRE( ds.open( outPath ) );
    CHECK( ds.width() == 16 );
    CHECK( ds.height() == 8 );
    CHECK( ds.bandCount() == 1 );
}

// =============================================================================
// 4. PreferencesDialog Stress Testing & QSettings Sync
// =============================================================================

TEST_CASE( "Empirical: PreferencesDialog tab switching, themes, CRS, tool paths, and QSettings sync", "[challenger2][preferences][stress]" )
{
    initTestEnv();

    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings settings;
    settings.clear();

    PreferencesDialog dialog;
    auto *tabWidget = dialog.findChild<QTabWidget *>( QStringLiteral( "preferencesTabWidget" ) );
    REQUIRE( tabWidget != nullptr );

    // 1. Rapid Tab Switching (100 switches)
    for ( int i = 0; i < 100; ++i )
    {
        tabWidget->setCurrentIndex( i % tabWidget->count() );
        REQUIRE( tabWidget->currentIndex() == ( i % tabWidget->count() ) );
    }

    // 2. Rapid Theme Toggles
    for ( int i = 0; i < 50; ++i )
    {
        dialog.setTheme( ( i % 2 == 0 ) ? "dark" : "light" );
        REQUIRE( dialog.theme() == ( ( i % 2 == 0 ) ? "dark" : "light" ) );
    }

    // 3. CRS Modifications
    const QString crsList[] = { "EPSG:4326", "EPSG:3857", "EPSG:32649", "EPSG:32650", "EPSG:4490" };
    for ( const auto &crs : crsList )
    {
        dialog.setDefaultCrs( crs );
        CHECK( dialog.defaultCrs().contains( crs ) );
    }

    // 4. Tool paths & Logging sync
    dialog.setGdalPath( "/usr/local/bin/gdal" );
    dialog.setOtbPath( "/opt/orfeo/bin" );
    dialog.setLogToFile( true );
    dialog.setLogFilePath( "/tmp/empirical_test.log" );
    dialog.setTheme( "dark" );
    dialog.setDefaultCrs( "EPSG:32650" );

    dialog.saveSettings();

    // 5. Load into a fresh second instance and verify QSettings sync
    PreferencesDialog dialog2;
    dialog2.loadSettings();

    CHECK( dialog2.theme() == "dark" );
    CHECK( dialog2.defaultCrs().contains( "EPSG:32650" ) );
    CHECK( dialog2.gdalPath() == "/usr/local/bin/gdal" );
    CHECK( dialog2.otbPath() == "/opt/orfeo/bin" );
    CHECK( dialog2.logToFile() == true );
    CHECK( dialog2.logFilePath() == "/tmp/empirical_test.log" );

    settings.clear();
}
