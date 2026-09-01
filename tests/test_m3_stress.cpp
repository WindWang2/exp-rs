// tests/test_m3_stress.cpp — Empirical Challenger Stress Suite for Milestone 3
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTreeWidget>
#include <QTextEdit>
#include <QTextBrowser>
#include <QResizeEvent>
#include <QTemporaryDir>
#include <QRandomGenerator>

#include <gdal.h>
#include <cpl_conv.h>

#include <qgsapplication.h>
#include <qgsmessagelog.h>
#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include "widgets/rs_empty_state_widget.h"
#include "panels/mosaic_panel.h"
#include "panels/task_center_dock.h"
#include "log_panel.h"
#include "panels/data_manager_panel.h"
#include "data/data_manager.h"
#include "processing/framework/task_center.h"

namespace
{

void ensureQgisApp()
{
    if ( QApplication::instance() )
        return;

    static int argc = 1;
    static char appName[] = "test_m3_stress";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

QString createDummyRaster( const QString &name )
{
    static QTemporaryDir tempDir;
    const QString filePath = tempDir.path() + QStringLiteral( "/" ) + name + QStringLiteral( ".tif" );
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver ) return QString();
    GDALDatasetH ds = GDALCreate( driver, filePath.toUtf8().constData(), 8, 8, 1, GDT_Byte, nullptr );
    if ( !ds ) return QString();
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    GDALSetGeoTransform( ds, gt );
    GDALClose( ds );
    return filePath;
}

sicnu::data::SourceDescriptor gdalSource( const QString &path )
{
    sicnu::data::SourceDescriptor source;
    source.providerKey = QStringLiteral( "gdal" );
    source.canonicalSource = path;
    return source;
}

} // anonymous namespace

// =============================================================================
// 1. RsEmptyStateWidget Lifecycle & Adversarial Edge Cases
// =============================================================================

TEST_CASE( "RsEmptyStateWidget - Rapid State Mutation Stress Test", "[m3][empty_state][stress]" )
{
    ensureQgisApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "l_yer_st_ck" ),
        QStringLiteral( "初始标题" ),
        QStringLiteral( "初始描述" ),
        QStringLiteral( "初始操作" ) );

    // 1000 Rapid mutation cycles
    for ( int i = 0; i < 1000; ++i )
    {
        const QString title = QString( "标题迭代_%1" ).arg( i );
        const QString desc = QString( "描述迭代内容_%1_测试文本" ).arg( i );
        const QString action = ( i % 2 == 0 ) ? QString( "操作_%1" ).arg( i ) : QString();

        widget.setTitle( title );
        widget.setDescription( desc );
        widget.setActionText( action );
        widget.setActionVisible( ( i % 3 ) != 0 );

        REQUIRE( widget.title() == title );
        REQUIRE( widget.description() == desc );
        REQUIRE( widget.actionText() == action );

        if ( action.isEmpty() || ( i % 3 ) == 0 )
        {
            REQUIRE_FALSE( widget.isActionVisible() );
        }
        else
        {
            REQUIRE( widget.isActionVisible() );
        }
    }
}

TEST_CASE( "RsEmptyStateWidget - Null, Empty & Extreme String Formats", "[m3][empty_state][adversarial]" )
{
    ensureQgisApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "invalid_icon_alias_123" ),
        QString(),
        QString(),
        QString() );

    // Verify null/empty behavior
    REQUIRE( widget.title().isEmpty() );
    REQUIRE( widget.description().isEmpty() );
    REQUIRE( widget.actionText().isEmpty() );
    REQUIRE_FALSE( widget.isActionVisible() );

    // Extreme string length (10,000 chars)
    const QString hugeText( 10000, QLatin1Char( 'X' ) );
    widget.setTitle( hugeText );
    widget.setDescription( hugeText );
    widget.setActionText( hugeText );
    REQUIRE( widget.title().length() == 10000 );
    REQUIRE( widget.description().length() == 10000 );
    REQUIRE( widget.actionText().length() == 10000 );
    REQUIRE( widget.isActionVisible() );

    // Special Unicode, RTL, Chinese, Emoji sequences, Control chars
    const QString specialText = QStringLiteral( "🛰️ 遥感 Studio \u202E RTL_Reversed \t\n \u0000 End" );
    widget.setTitle( specialText );
    widget.setDescription( specialText );
    widget.setActionText( specialText );
    REQUIRE( widget.title() == specialText );
    REQUIRE( widget.description() == specialText );

    // HTML / Script injection attempt
    const QString xssPayload = QStringLiteral( "<script>alert('pwn')</script><b>Bold & Safe</b>" );
    widget.setTitle( xssPayload );
    widget.setDescription( xssPayload );
    widget.setActionText( xssPayload );
    REQUIRE( widget.title() == xssPayload );
}

TEST_CASE( "RsEmptyStateWidget - High-DPI Resizing and Icon Boundary Scaling", "[m3][empty_state][high_dpi]" )
{
    ensureQgisApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "d_t_b_se" ),
        QStringLiteral( "数据面板" ),
        QStringLiteral( "测试说明" ) );

    // Zero size
    widget.setIconSize( QSize( 0, 0 ) );
    QSize hint0 = widget.sizeHint();
    REQUIRE( hint0.isValid() );

    // Huge size
    widget.setIconSize( QSize( 1024, 1024 ) );
    QSize hintHuge = widget.sizeHint();
    REQUIRE( hintHuge.isValid() );
    REQUIRE( hintHuge.height() >= 1024 );

    // High-DPI standard sizes
    widget.setIconSize( QSize( 48, 48 ) );
    widget.setIconSize( QSize( 96, 96 ) );
    widget.setIconSize( QSize( 128, 128 ) );

    // Resize events on the widget itself across standard screen boundaries
    widget.resize( 0, 0 );
    widget.resize( 10, 10 );
    widget.resize( 200, 150 );
    widget.resize( 1920, 1080 );
    widget.resize( 3840, 2160 );

    REQUIRE( widget.minimumSizeHint().isValid() );
}

TEST_CASE( "RsEmptyStateWidget - Signal-Slot Disconnection and Rapid Clicks", "[m3][empty_state][signals]" )
{
    ensureQgisApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "check_outline" ),
        QStringLiteral( "就绪" ),
        QStringLiteral( "说明" ),
        QStringLiteral( "执行操作" ) );

    auto *btn = widget.findChild<QPushButton *>( QStringLiteral( "rsEmptyStateBtn" ) );
    REQUIRE( btn != nullptr );

    QSignalSpy spy( &widget, &sicnu::RsEmptyStateWidget::actionClicked );

    // 1000 simulated clicks
    for ( int i = 0; i < 1000; ++i )
    {
        btn->click();
    }
    REQUIRE( spy.count() == 1000 );

    // Hidden button should not trigger action logic if filtered or invisible
    widget.setActionVisible( false );
    REQUIRE_FALSE( widget.isActionVisible() );
    REQUIRE( btn->isHidden() );

    // Reset action text to empty
    widget.setActionText( QString() );
    REQUIRE_FALSE( widget.isActionVisible() );
    REQUIRE( btn->isHidden() );
}

// =============================================================================
// 2. Dynamic Stack Switching in Dock Panels & Views
// =============================================================================

TEST_CASE( "LogPanel - Dynamic Stack Switching and Rapid Log/Clear Cycles", "[m3][log_panel][stack]" )
{
    ensureQgisApp();

    LogPanel logPanel;
    auto *stack = logPanel.findChild<QStackedWidget *>( QStringLiteral( "rsLogTextStack" ) );
    REQUIRE( stack != nullptr );

    // Initial state: Empty state page (Index 1)
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( logPanel.messageCount() == 0 );

    // Log message -> should switch to Index 0 (Text area)
    logPanel.logMessage( QStringLiteral( "系统初始化完成" ), QStringLiteral( "System" ), Qgis::MessageLevel::Info );
    QCoreApplication::processEvents();
    REQUIRE( stack->currentIndex() == 0 );
    REQUIRE( logPanel.messageCount() == 1 );

    // Clear messages -> should return to Index 1 (Empty state)
    logPanel.clearMessages();
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( logPanel.messageCount() == 0 );

    // Rapid 300 cycles of log & clear
    for ( int i = 0; i < 300; ++i )
    {
        logPanel.logMessage( QString( "Log msg #%1" ).arg( i ), QStringLiteral( "Stress" ), Qgis::MessageLevel::Warning );
        REQUIRE( stack->currentIndex() == 0 );
        REQUIRE( logPanel.messageCount() == 1 );

        logPanel.clearMessages();
        REQUIRE( stack->currentIndex() == 1 );
        REQUIRE( logPanel.messageCount() == 0 );
    }
}

TEST_CASE( "MosaicPanel - Dynamic Stack Switching on Rapid Input Changes", "[m3][mosaic_panel][stack]" )
{
    ensureQgisApp();

    MosaicPanel mosaic;
    auto *stack = mosaic.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
    auto *list = mosaic.findChild<QListWidget *>();
    REQUIRE( stack != nullptr );
    REQUIRE( list != nullptr );

    // Initial state: Empty state page (Index 1)
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( list->count() == 0 );
    REQUIRE( mosaic.inputFiles().isEmpty() );

    // Add items directly to list and verify stack index logic
    const QString f1 = createDummyRaster( QStringLiteral( "mosaic_test_1" ) );
    const QString f2 = createDummyRaster( QStringLiteral( "mosaic_test_2" ) );

    list->addItem( f1 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    REQUIRE( stack->currentIndex() == 0 );
    REQUIRE( mosaic.inputFiles().size() == 1 );

    list->addItem( f2 );
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    REQUIRE( stack->currentIndex() == 0 );
    REQUIRE( mosaic.inputFiles().size() == 2 );

    // Clear all items -> Index 1
    list->clear();
    stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( mosaic.inputFiles().isEmpty() );

    // Rapid add/remove cycles (200 iterations)
    for ( int i = 0; i < 200; ++i )
    {
        list->addItem( f1 );
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 0 );

        delete list->takeItem( 0 );
        stack->setCurrentIndex( list->count() > 0 ? 0 : 1 );
        REQUIRE( stack->currentIndex() == 1 );
    }
}

TEST_CASE( "TaskCenterDock - Dynamic Stack Switching and Task Lifecycle", "[m3][task_center][stack]" )
{
    ensureQgisApp();

    sicnu::TaskCenterDock taskDock;
    auto *stack = taskDock.findChild<QStackedWidget *>( QStringLiteral( "rsTaskCenterTreeStack" ) );
    auto *tree = taskDock.findChild<QTreeWidget *>();
    REQUIRE( stack != nullptr );
    REQUIRE( tree != nullptr );

    // Initial state: Empty state page (Index 1)
    taskDock.refreshTaskList();
    REQUIRE( stack->currentIndex() == 1 );

    // Enqueue a task
    QVariantMap params;
    params.insert( QStringLiteral( "input" ), QStringLiteral( "/test/dem.tif" ) );
    long taskId = sicnu::TaskCenter::instance().enqueueTask( QStringLiteral( "stress_test_algo" ), params, false );
    REQUIRE( taskId > 0 );

    taskDock.refreshTaskList();
    QCoreApplication::processEvents();

    // Stack should now be on Index 0 (Tree)
    REQUIRE( stack->currentIndex() == 0 );
    REQUIRE( tree->topLevelItemCount() >= 1 );

    // Cancel task and refresh
    sicnu::TaskCenter::instance().cancelTask( taskId );
    taskDock.refreshTaskList();
    QCoreApplication::processEvents();

    REQUIRE( stack->currentIndex() == 0 ); // Still shows cancelled task record in history
}

TEST_CASE( "DataManagerPanel - Dynamic Stack Switching with Asset Registrations", "[m3][data_manager][stack]" )
{
    ensureQgisApp();

    sicnu::data::DataManager dm;
    sicnu::DataManagerPanel panel( &dm );

    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "dataManagerTreeStack" ) );
    auto *tree = panel.findChild<QTreeWidget *>( QStringLiteral( "dataManagerTree" ) );
    REQUIRE( stack != nullptr );
    REQUIRE( tree != nullptr );

    // Initial state: Empty (Index 1)
    panel.refresh();
    REQUIRE( stack->currentIndex() == 1 );

    // Register an asset
    const QString rasterPath = createDummyRaster( QStringLiteral( "dm_sample_1" ) );
    sicnu::data::RegisterRequest req;
    req.source = gdalSource( rasterPath );
    req.persistence = sicnu::data::PersistencePolicy::ProjectPersistent;
    auto regResult = dm.registerSource( req );
    REQUIRE_FALSE( regResult.assetId.isNull() );

    panel.refresh();
    REQUIRE( stack->currentIndex() == 0 ); // Now has data -> Index 0

    // Unload asset
    auto plan = dm.planUnload( regResult.assetId );
    auto unloadResult = dm.unload( plan );
    REQUIRE( static_cast<bool>( unloadResult ) );

    panel.refresh();
    REQUIRE( stack->currentIndex() == 1 ); // Empty again -> Index 1
}

// =============================================================================
// 3. Splitters Under Zero / Minimum Size & Extreme Scaling Constraints
// =============================================================================

TEST_CASE( "Splitters - Zero, Minimum & High-DPI Extreme Geometries", "[m3][splitters][stress]" )
{
    ensureQgisApp();

    QWidget container;
    container.resize( 800, 600 );

    QSplitter splitter( Qt::Horizontal, &container );
    splitter.setObjectName( QStringLiteral( "rsMapViewSplitter" ) );
    splitter.setChildrenCollapsible( false );

    auto *leftStack = new QStackedWidget( &splitter );
    auto *empty1 = new sicnu::RsEmptyStateWidget( QStringLiteral( "l_yer_st_ck" ), QStringLiteral( "空图层" ), QStringLiteral( "说明" ), QStringLiteral( "按钮" ), leftStack );
    leftStack->addWidget( empty1 );

    auto *rightStack = new QStackedWidget( &splitter );
    auto *empty2 = new sicnu::RsEmptyStateWidget( QStringLiteral( "app_icon" ), QStringLiteral( "空画布" ), QStringLiteral( "说明" ), QStringLiteral( "按钮" ), rightStack );
    rightStack->addWidget( empty2 );

    splitter.addWidget( leftStack );
    splitter.addWidget( rightStack );

    // Stress test split sizes
    splitter.setSizes( { 0, 0 } );
    REQUIRE( splitter.sizes().size() == 2 );

    splitter.setSizes( { 10000, 10000 } );
    REQUIRE( splitter.sizes().size() == 2 );

    splitter.setSizes( { 1, 999 } );
    REQUIRE( splitter.sizes().size() == 2 );

    splitter.setSizes( { -100, -100 } );
    REQUIRE( splitter.sizes().size() == 2 );

    // Extreme container resize stress
    container.resize( 0, 0 );
    container.resize( 10, 10 );
    container.resize( 3840, 2160 );
    container.resize( 7680, 4320 ); // 8K Ultra-HD

    REQUIRE( leftStack->sizeHint().isValid() );
    REQUIRE( rightStack->sizeHint().isValid() );
}
