#include <catch2/catch_test_macros.hpp>

#include "widgets/rs_toolbar_flow_host.h"
#include "widgets/rs_empty_state_widget.h"
#include "panels/task_center_dock.h"
#include "panels/mosaic_panel.h"
#include "log_panel.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QSplitter>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTest>
#include <QPointer>
#include <cmath>

static void ensureApp()
{
    if ( !qApp )
    {
        static int argc = 1;
        static char arg0[] = "test_m3_adversarial_challenge";
        static char *argv[] = { arg0, nullptr };
        new QApplication( argc, argv );
    }
}

// Relative luminance according to WCAG 2.1
static double relativeLuminance( const QColor &c )
{
    auto channel = []( double v ) {
        v /= 255.0;
        return ( v <= 0.03928 ) ? ( v / 12.92 ) : std::pow( ( v + 0.055 ) / 1.055, 2.4 );
    };
    double r = channel( c.red() );
    double g = channel( c.green() );
    double b = channel( c.blue() );
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static double contrastRatio( const QColor &c1, const QColor &c2 )
{
    double l1 = relativeLuminance( c1 );
    double l2 = relativeLuminance( c2 );
    if ( l1 < l2 )
        std::swap( l1, l2 );
    return ( l1 + 0.05 ) / ( l2 + 0.05 );
}

// =============================================================================
// DOMAIN 1: RsToolbarFlowHost & Ribbon Dock Layout Stability & Stress Tests
// =============================================================================

TEST_CASE( "RsToolbarFlowHost dynamic resizing, row calculation, and boundary stress", "[m3][toolbar][stress]" )
{
    ensureApp();

    RsToolbarFlowHost host;
    host.resize( 800, 100 );

    SECTION( "Initial state with 0 toolbars" )
    {
        REQUIRE_FALSE( host.hasProductToolbars() );
        REQUIRE( host.usedRows() == 0 );
        REQUIRE( host.usedHeight() == 0 );
    }

    SECTION( "Single toolbar dynamic resizing and single-row collapse" )
    {
        auto *tb1 = new QToolBar( QStringLiteral( "tb1" ), &host );
        tb1->setObjectName( QStringLiteral( "tb1" ) );
        tb1->addAction( QStringLiteral( "Action 1" ) );
        tb1->addAction( QStringLiteral( "Action 2" ) );

        QSignalSpy spy( &host, &RsToolbarFlowHost::geometryChanged );

        host.setProductToolbars( { tb1 } );
        host.applyVisibility( { { tb1, true } } );

        REQUIRE( host.hasProductToolbars() );
        REQUIRE( host.usedRows() == 1 );
        REQUIRE( host.usedHeight() == RsToolbarFlowHost::kRowH );
        REQUIRE( spy.count() >= 1 );

        // Resize host to various widths: wide, medium, narrow, extreme
        const int widths[] = { 1920, 1200, 800, 400, 200, 100, 50, 1, 0, 4000 };
        for ( int w : widths )
        {
            host.resize( w, 100 );
            REQUIRE( host.usedRows() == 1 );
            REQUIRE( host.usedHeight() == RsToolbarFlowHost::kRowH );
            REQUIRE( host.height() == RsToolbarFlowHost::kRowH );
        }
    }

    SECTION( "Multiple toolbars flow wrapping and max 2 rows constraint" )
    {
        QList<QToolBar *> bars;
        for ( int i = 0; i < 6; ++i )
        {
            auto *tb = new QToolBar( QStringLiteral( "tb_%1" ).arg( i ), &host );
            tb->setObjectName( QStringLiteral( "tb_%1" ).arg( i ) );
            tb->addAction( QStringLiteral( "Act_%1" ).arg( i ) );
            bars.append( tb );
        }

        host.setProductToolbars( bars );
        QHash<QToolBar *, bool> vis;
        for ( auto *tb : bars )
            vis[tb] = true;
        host.applyVisibility( vis );

        // Wide host: all fit on 1 or 2 rows
        host.resize( 3000, 100 );
        REQUIRE( host.usedRows() >= 1 );
        REQUIRE( host.usedRows() <= RsToolbarFlowHost::kMaxRows );

        // Medium host: wraps to 2 rows
        host.resize( 600, 100 );
        REQUIRE( host.usedRows() == 2 );
        REQUIRE( host.usedHeight() == 2 * RsToolbarFlowHost::kRowH );
        REQUIRE( host.height() == 64 );

        // Very narrow host: clamped to max 2 rows
        host.resize( 150, 100 );
        REQUIRE( host.usedRows() == RsToolbarFlowHost::kMaxRows );
        REQUIRE( host.usedHeight() == 2 * RsToolbarFlowHost::kRowH );
    }

    SECTION( "Rapid visibility toggling and empty set reflow" )
    {
        QList<QToolBar *> bars;
        for ( int i = 0; i < 4; ++i )
        {
            auto *tb = new QToolBar( QStringLiteral( "rapid_tb_%1" ).arg( i ), &host );
            tb->setObjectName( QStringLiteral( "rapid_tb_%1" ).arg( i ) );
            bars.append( tb );
        }
        host.setProductToolbars( bars );

        // Rapid visibility cycles
        for ( int iter = 0; iter < 50; ++iter )
        {
            QHash<QToolBar *, bool> v;
            for ( int i = 0; i < bars.size(); ++i )
                v[bars[i]] = ( ( iter + i ) % 2 == 0 );
            host.applyVisibility( v );

            bool anyVisible = false;
            for ( bool b : v.values() )
                anyVisible |= b;

            if ( anyVisible )
            {
                REQUIRE( host.usedRows() >= 1 );
                REQUIRE( host.usedRows() <= 2 );
            }
            else
            {
                REQUIRE( host.usedRows() == 0 );
                REQUIRE( host.usedHeight() == 0 );
            }
        }

        // Hide all
        QHash<QToolBar *, bool> allHidden;
        for ( auto *tb : bars )
            allHidden[tb] = false;
        host.applyVisibility( allHidden );
        REQUIRE( host.usedRows() == 0 );
        REQUIRE( host.height() == 0 );
    }
}

TEST_CASE( "RsToolbarFlowHost resize grip interaction stress", "[m3][toolbar][resize]" )
{
    ensureApp();

    RsToolbarFlowHost host;
    host.resize( 1000, 100 );

    auto *tb1 = new QToolBar( QStringLiteral( "resize_tb1" ), &host );
    tb1->setObjectName( QStringLiteral( "resize_tb1" ) );
    host.setProductToolbars( { tb1 } );
    host.applyVisibility( { { tb1, true } } );

    auto *resizeGrip = host.findChild<QWidget *>( QStringLiteral( "rsToolbarResizeGrip" ) );
    REQUIRE( resizeGrip != nullptr );

    // 1. Mouse press on resize grip
    QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 4, 16 ), QPointF( 100, 16 ),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( resizeGrip, &pressEv );

    // 2. Mouse move to expand by +150px
    QMouseEvent moveEv( QEvent::MouseMove, QPointF( 154, 16 ), QPointF( 250, 16 ),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( resizeGrip, &moveEv );

    // 3. Mouse release
    QMouseEvent releaseEv( QEvent::MouseButtonRelease, QPointF( 154, 16 ), QPointF( 250, 16 ),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( resizeGrip, &releaseEv );

    // Verify host reflowed cleanly
    REQUIRE( host.usedRows() == 1 );
}

TEST_CASE( "RsToolbarFlowHost drag-and-drop reordering logic inspection", "[m3][toolbar][dnd]" )
{
    ensureApp();

    RsToolbarFlowHost host;
    host.resize( 1200, 100 );

    auto *tb0 = new QToolBar( QStringLiteral( "alpha_tb" ), &host );
    tb0->setObjectName( QStringLiteral( "alpha_tb" ) );
    auto *tb1 = new QToolBar( QStringLiteral( "beta_tb" ), &host );
    tb1->setObjectName( QStringLiteral( "beta_tb" ) );
    auto *tb2 = new QToolBar( QStringLiteral( "gamma_tb" ), &host );
    tb2->setObjectName( QStringLiteral( "gamma_tb" ) );

    host.setProductToolbars( { tb0, tb1, tb2 } );
    host.applyVisibility( { { tb0, true }, { tb1, true }, { tb2, true } } );

    auto grips = host.findChildren<QWidget *>( QStringLiteral( "rsToolbarDragGrip" ) );
    REQUIRE( grips.size() == 3 );

    // Simulate drag start on first toolbar grip
    QWidget *grip0 = grips.at( 0 );
    QMouseEvent pressEv( QEvent::MouseButtonPress, QPointF( 4, 16 ), grip0->mapToGlobal( QPoint( 4, 16 ) ),
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( grip0, &pressEv );

    // Move across screen to simulate drag over second and third toolbar
    QMouseEvent moveEv( QEvent::MouseMove, QPointF( 500, 16 ), host.mapToGlobal( QPoint( 500, 16 ) ),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( grip0, &moveEv );

    // Release over third toolbar position
    QMouseEvent releaseEv( QEvent::MouseButtonRelease, QPointF( 500, 16 ), host.mapToGlobal( QPoint( 500, 16 ) ),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QApplication::sendEvent( grip0, &releaseEv );

    // Host should remain healthy and reflowed
    REQUIRE( host.usedRows() >= 1 );
    REQUIRE( host.usedRows() <= 2 );
}

// =============================================================================
// DOMAIN 2: QSS Theme Switching & Visual Contrast Stress Tests
// =============================================================================

TEST_CASE( "QSS theme files validity and switching stress", "[m3][qss][theme]" )
{
    ensureApp();

#ifdef CMAKE_SOURCE_DIR
    const QString repoRoot = QString::fromUtf8( CMAKE_SOURCE_DIR );
#else
    const QString repoRoot = QDir::currentPath();
#endif
    QString lightPath = QDir( repoRoot ).filePath( QStringLiteral( "resources/styles.qss" ) );
    QString darkPath = QDir( repoRoot ).filePath( QStringLiteral( "resources/styles-dark.qss" ) );

    QFile fl( lightPath );
    REQUIRE( fl.open( QIODevice::ReadOnly | QIODevice::Text ) );
    QString lightQss = QString::fromUtf8( fl.readAll() );
    fl.close();
    REQUIRE_FALSE( lightQss.isEmpty() );

    QFile fd( darkPath );
    REQUIRE( fd.open( QIODevice::ReadOnly | QIODevice::Text ) );
    QString darkQss = QString::fromUtf8( fd.readAll() );
    fd.close();
    REQUIRE_FALSE( darkQss.isEmpty() );

    // Create a composite test window with all M3 components
    QWidget container;
    auto *lay = new QVBoxLayout( &container );

    auto *emptyWidget = new sicnu::RsEmptyStateWidget(
        QStringLiteral( "l_yer_st_ck" ),
        QStringLiteral( "测试标题" ),
        QStringLiteral( "测试描述说明文本" ),
        QStringLiteral( "操作按钮" ),
        &container );
    lay->addWidget( emptyWidget );

    auto *btnBox = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &container );
    lay->addWidget( btnBox );

    auto *grp = new QGroupBox( QStringLiteral( "分组框" ), &container );
    lay->addWidget( grp );

    container.resize( 600, 400 );
    container.show();

    // 100 rapid theme switches
    for ( int i = 0; i < 100; ++i )
    {
        if ( i % 2 == 0 )
            qApp->setStyleSheet( lightQss );
        else
            qApp->setStyleSheet( darkQss );

        qApp->processEvents();
    }

    // Reset to light
    qApp->setStyleSheet( lightQss );
    qApp->processEvents();

    REQUIRE( emptyWidget->title() == QStringLiteral( "测试标题" ) );
    REQUIRE( emptyWidget->isActionVisible() );
}

TEST_CASE( "QSS Empty State and Widget Contrast Analysis", "[m3][qss][contrast]" )
{
    // Light Theme Tokens
    const QColor lightBg( 0xFF, 0xFF, 0xFF );          // surface.panel #FFFFFF
    const QColor lightCanvasBg( 0xF4, 0xF6, 0xF8 );    // surface.canvas #F4F6F8
    const QColor lightTitle( 0x1C, 0x24, 0x30 );       // #1C2430
    const QColor lightDesc( 0x5A, 0x65, 0x73 );        // #5A6573
    const QColor lightAccent( 0x0B, 0x6E, 0x4F );      // #0B6E4F (button bg)
    const QColor lightBtnText( 0xFF, 0xFF, 0xFF );     // #FFFFFF

    // Dark Theme Tokens
    const QColor darkBg( 0x1E, 0x22, 0x29 );           // surface.panel #1E2229
    const QColor darkCanvasBg( 0x1A, 0x1D, 0x23 );     // surface.canvas #1A1D23
    const QColor darkTitle( 0xE8, 0xEC, 0xF1 );        // #E8ECF1
    const QColor darkDesc( 0xA8, 0xB0, 0xBC );         // #A8B0BC
    const QColor darkAccent( 0x2B, 0xB6, 0x73 );       // #2BB673 (primary btn bg)
    const QColor darkBtnText( 0x1A, 0x1D, 0x23 );      // #1A1D23 (primary btn text)

    SECTION( "Light Theme WCAG AA Contrast Ratios" )
    {
        double titleContrast = contrastRatio( lightTitle, lightBg );
        double descContrast = contrastRatio( lightDesc, lightBg );
        double btnContrast = contrastRatio( lightBtnText, lightAccent );

        REQUIRE( titleContrast >= 4.5 );
        REQUIRE( descContrast >= 4.5 );
        REQUIRE( btnContrast >= 4.5 );
    }

    SECTION( "Dark Theme WCAG AA Contrast Ratios" )
    {
        double titleContrast = contrastRatio( darkTitle, darkBg );
        double descContrast = contrastRatio( darkDesc, darkBg );
        double btnContrast = contrastRatio( darkBtnText, darkAccent );

        REQUIRE( titleContrast >= 4.5 );
        REQUIRE( descContrast >= 4.5 );
        REQUIRE( btnContrast >= 4.5 );
    }
}

// =============================================================================
// DOMAIN 3: Empty State CTA Button Interactions & Parent Lifecycle Safety
// =============================================================================

TEST_CASE( "RsEmptyStateWidget permutation and stress mutations", "[m3][emptystate][mutators]" )
{
    ensureApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "init_icon" ),
        QStringLiteral( "Init Title" ),
        QStringLiteral( "Init Desc" ),
        QStringLiteral( "Init Action" ) );

    for ( int i = 0; i < 500; ++i )
    {
        QString t = QStringLiteral( "Title %1" ).arg( i );
        QString d = QStringLiteral( "Desc %1" ).arg( i );
        QString a = ( i % 3 == 0 ) ? QString() : QStringLiteral( "Action %1" ).arg( i );

        widget.setTitle( t );
        widget.setDescription( d );
        widget.setActionText( a );
        widget.setActionVisible( ( i % 2 == 0 ) );

        REQUIRE( widget.title() == t );
        REQUIRE( widget.description() == d );
        REQUIRE( widget.actionText() == a );
        if ( a.isEmpty() || i % 2 != 0 )
        {
            REQUIRE_FALSE( widget.isActionVisible() );
        }
        else
        {
            REQUIRE( widget.isActionVisible() );
        }
    }
}

TEST_CASE( "RsEmptyStateWidget CTA click dispatch and signal connections", "[m3][emptystate][signals]" )
{
    ensureApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "mos_ic" ),
        QStringLiteral( "标题" ),
        QStringLiteral( "描述" ),
        QStringLiteral( "点击我" ) );

    QSignalSpy spy( &widget, &sicnu::RsEmptyStateWidget::actionClicked );

    auto *btn = widget.findChild<QPushButton *>( QStringLiteral( "rsEmptyStateBtn" ) );
    REQUIRE( btn != nullptr );

    for ( int i = 1; i <= 10; ++i )
    {
        btn->click();
        REQUIRE( spy.count() == i );
    }

    QTest::mouseClick( btn, Qt::LeftButton );
    REQUIRE( spy.count() == 11 );

    btn->setEnabled( false );
    QTest::mouseClick( btn, Qt::LeftButton );
    REQUIRE( spy.count() == 11 );
}

TEST_CASE( "Empty State lifecycle and parent container destruction safety", "[m3][emptystate][lifecycle]" )
{
    ensureApp();

    SECTION( "Destroying parent QStackedWidget automatically cleans child RsEmptyStateWidget" )
    {
        QPointer<sicnu::RsEmptyStateWidget> childPtr;
        {
            auto *stack = new QStackedWidget();
            auto *empty = new sicnu::RsEmptyStateWidget(
                QStringLiteral( "icon" ),
                QStringLiteral( "title" ),
                QStringLiteral( "desc" ),
                QStringLiteral( "act" ),
                stack );
            childPtr = empty;
            stack->addWidget( empty );
            REQUIRE( childPtr != nullptr );
            delete stack;
        }
        REQUIRE( childPtr.isNull() );
    }

    SECTION( "Deleting RsEmptyStateWidget before parent container does not corrupt parent" )
    {
        auto *stack = new QStackedWidget();
        auto *empty = new sicnu::RsEmptyStateWidget(
            QStringLiteral( "icon" ),
            QStringLiteral( "title" ),
            QStringLiteral( "desc" ),
            QStringLiteral( "act" ),
            stack );
        stack->addWidget( empty );
        stack->setCurrentWidget( empty );

        delete empty;

        REQUIRE( stack->count() == 0 );
        auto *label = new QLabel( QStringLiteral( "new widget" ), stack );
        stack->addWidget( label );
        REQUIRE( stack->count() == 1 );
        delete stack;
    }

    SECTION( "Standalone RsEmptyStateWidget without parent deletes cleanly" )
    {
        for ( int i = 0; i < 50; ++i )
        {
            auto *w = new sicnu::RsEmptyStateWidget(
                QStringLiteral( "standalone" ),
                QStringLiteral( "title" ),
                QStringLiteral( "desc" ) );
            delete w;
        }
    }
}

TEST_CASE( "Host panel empty state transitions and interactions", "[m3][panels][emptystate]" )
{
    ensureApp();

    SECTION( "TaskCenterDock empty state initialization" )
    {
        sicnu::TaskCenterDock taskDock;
        auto *stack = taskDock.findChild<QStackedWidget *>( QStringLiteral( "rsTaskCenterTreeStack" ) );
        REQUIRE( stack != nullptr );
        REQUIRE( stack->currentIndex() == 1 );

        auto *emptyWidget = taskDock.findChild<sicnu::RsEmptyStateWidget *>();
        REQUIRE( emptyWidget != nullptr );
        REQUIRE( emptyWidget->title() == QStringLiteral( "暂无任务" ) );
        REQUIRE_FALSE( emptyWidget->isActionVisible() );
    }

    SECTION( "LogPanel empty state initialization and message filter inspection" )
    {
        LogPanel logPanel;
        auto *stack = logPanel.findChild<QStackedWidget *>( QStringLiteral( "rsLogTextStack" ) );
        REQUIRE( stack != nullptr );
        // Initially empty state is visible (index 1)
        REQUIRE( stack->currentIndex() == 1 );

        auto *emptyWidget = logPanel.findChild<sicnu::RsEmptyStateWidget *>();
        REQUIRE( emptyWidget != nullptr );
        REQUIRE( emptyWidget->title() == QStringLiteral( "暂无系统日志" ) );
    }

    SECTION( "MosaicPanel empty state and CTA button integration" )
    {
        MosaicPanel mosaicPanel;
        auto *stack = mosaicPanel.findChild<QStackedWidget *>( QStringLiteral( "rsMosaicInputStack" ) );
        REQUIRE( stack != nullptr );
        REQUIRE( stack->currentIndex() == 1 );

        auto *emptyWidget = mosaicPanel.findChild<sicnu::RsEmptyStateWidget *>();
        REQUIRE( emptyWidget != nullptr );
        REQUIRE( emptyWidget->title() == QStringLiteral( "暂无镶嵌输入影像" ) );
        REQUIRE( emptyWidget->isActionVisible() );
        REQUIRE( emptyWidget->actionText() == QStringLiteral( "添加影像..." ) );
    }
}
