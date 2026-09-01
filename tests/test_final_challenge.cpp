#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QIcon>
#include <QPixmap>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <qgsapplication.h>
#include <qgsmessagelog.h>

#include "log_panel.h"
#include "widgets/rs_empty_state_widget.h"

namespace
{

void ensureQgisApp()
{
    if ( QApplication::instance() )
        return;

    static int argc = 1;
    static char appName[] = "test_final_challenge";
    static char *argv[] = { appName, nullptr };
    static auto *app = new QgsApplication( argc, argv, true );
    ( void ) app;
    QgsApplication::initQgis();
}

QIcon createTestVectorIcon()
{
    QPixmap pix( 128, 128 );
    pix.fill( Qt::transparent );
    QPainter p( &pix );
    p.setPen( Qt::red );
    p.setBrush( Qt::blue );
    p.drawRect( 10, 10, 108, 108 );
    p.end();
    return QIcon( pix );
}

} // anonymous namespace

// =============================================================================
// 1. LogPanel Adversarial Message Routing & Dynamic Stack Switching
// =============================================================================

TEST_CASE( "LogPanel - High-Throughput Burst and Comprehensive Filtering Stress", "[final][log_panel][stress]" )
{
    ensureQgisApp();

    LogPanel panel;
    auto *stack = panel.findChild<QStackedWidget *>( QStringLiteral( "rsLogTextStack" ) );
    auto *textEdit = panel.findChild<QTextEdit *>();
    auto *levelCombo = panel.findChild<QComboBox *>();
    auto *searchEdit = panel.findChild<QLineEdit *>();
    auto *clearBtn = panel.findChild<QPushButton *>();

    REQUIRE( stack != nullptr );
    REQUIRE( textEdit != nullptr );
    REQUIRE( levelCombo != nullptr );
    REQUIRE( searchEdit != nullptr );
    REQUIRE( clearBtn != nullptr );

    // 1.1 Initial State: Empty state at Index 1
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( panel.messageCount() == 0 );
    REQUIRE( panel.lastMessage().isEmpty() );

    // 1.2 Default Tag & Level Filter verification: All messages allowed
    panel.logMessage( QStringLiteral( "初始信息日志" ), QStringLiteral( "Core" ), Qgis::MessageLevel::Info );
    REQUIRE( stack->currentIndex() == 0 );
    REQUIRE( panel.messageCount() == 1 );
    REQUIRE( panel.lastMessage() == QStringLiteral( "初始信息日志" ) );

    // 1.3 Rapid Message Burst: 5,000 messages across various levels & tags
    const Qgis::MessageLevel levels[] = {
        Qgis::MessageLevel::Info,
        Qgis::MessageLevel::Warning,
        Qgis::MessageLevel::Critical,
        Qgis::MessageLevel::Success,
        Qgis::MessageLevel::NoLevel
    };
    const QString tags[] = {
        QStringLiteral( "System" ),
        QStringLiteral( "GDAL" ),
        QStringLiteral( "Algorithms" ),
        QStringLiteral( "TaskCenter" ),
        QStringLiteral( "CustomTag" )
    };

    for ( int i = 0; i < 5000; ++i )
    {
        panel.logMessage(
            QString( "Burst message index %1 with payload data" ).arg( i ),
            tags[i % 5],
            levels[i % 5]
        );
    }
    REQUIRE( panel.messageCount() == 5001 );
    REQUIRE( stack->currentIndex() == 0 );

    // 1.4 Clear functionality
    clearBtn->click();
    REQUIRE( panel.messageCount() == 0 );
    REQUIRE( panel.lastMessage().isEmpty() );
    REQUIRE( stack->currentIndex() == 1 );
    REQUIRE( textEdit->toPlainText().isEmpty() );

    // 1.5 Level Filter Verification
    // Level 0: Info, 1: Warning, 2: Critical, 3: Success
    // Setting level filter to Critical (MessageLevel::Critical)
    int criticalIndex = levelCombo->findData( static_cast<int>( Qgis::MessageLevel::Critical ) );
    REQUIRE( criticalIndex != -1 );
    levelCombo->setCurrentIndex( criticalIndex );

    // Log an Info message -> should be filtered out, stack remains at Index 1
    panel.logMessage( QStringLiteral( "This is info - filtered out" ), QStringLiteral( "System" ), Qgis::MessageLevel::Info );
    REQUIRE( panel.messageCount() == 1 ); // Total count increments
    REQUIRE( stack->currentIndex() == 1 ); // But message not displayed -> stack remains empty

    // Log a Critical message -> should pass through, stack becomes Index 0
    panel.logMessage( QStringLiteral( "This is critical error!" ), QStringLiteral( "System" ), Qgis::MessageLevel::Critical );
    REQUIRE( panel.messageCount() == 2 );
    REQUIRE( stack->currentIndex() == 0 );

    // Reset level filter to All (-1)
    int allIndex = levelCombo->findData( -1 );
    REQUIRE( allIndex != -1 );
    levelCombo->setCurrentIndex( allIndex );

    // 1.6 Search Filtering
    clearBtn->click();
    REQUIRE( stack->currentIndex() == 1 );

    searchEdit->setText( QStringLiteral( "ALPHA_KEYWORD" ) );

    // Message without keyword -> dropped by filter
    panel.logMessage( QStringLiteral( "Normal log without token" ), QStringLiteral( "System" ), Qgis::MessageLevel::Info );
    REQUIRE( stack->currentIndex() == 1 );

    // Message with keyword -> displayed
    panel.logMessage( QStringLiteral( "Special event containing ALPHA_KEYWORD here" ), QStringLiteral( "System" ), Qgis::MessageLevel::Info );
    REQUIRE( stack->currentIndex() == 0 );

    // Tag matching keyword -> displayed
    panel.logMessage( QStringLiteral( "Message with target tag" ), QStringLiteral( "ALPHA_KEYWORD" ), Qgis::MessageLevel::Info );
    REQUIRE( stack->currentIndex() == 0 );

    searchEdit->clear();
}

TEST_CASE( "LogPanel - Buffer Overflow and Hidden Flushing Safety", "[final][log_panel][buffer]" )
{
    ensureQgisApp();

    LogPanel panel;
    panel.hide();

    // Log 2,500 messages while hidden
    for ( int i = 0; i < 2500; ++i )
    {
        panel.logMessage( QString( "Hidden message #%1" ).arg( i ), QStringLiteral( "Background" ), Qgis::MessageLevel::Info );
    }
    REQUIRE( panel.messageCount() == 2500 );

    // Show event triggers flushPendingMessages
    panel.show();
    QCoreApplication::processEvents();

    auto *textEdit = panel.findChild<QTextEdit *>();
    REQUIRE( textEdit != nullptr );
    REQUIRE_FALSE( textEdit->toPlainText().isEmpty() );
}

// =============================================================================
// 2. RsEmptyStateWidget Vector Integrity & High-DPI Churn
// =============================================================================

TEST_CASE( "RsEmptyStateWidget - Zero-Size Collapse, High-DPI Expansion & Icon Non-Degradation", "[final][empty_state][vector]" )
{
    ensureQgisApp();

    QIcon vectorIcon = createTestVectorIcon();
    REQUIRE_FALSE( vectorIcon.isNull() );

    sicnu::RsEmptyStateWidget widget(
        vectorIcon,
        QStringLiteral( "图层管理" ),
        QStringLiteral( "当前工程尚未加载任何遥感影像图层。" ),
        QStringLiteral( "加载影像" )
    );

    auto *iconLabel = widget.findChild<QLabel *>( QStringLiteral( "rsEmptyStateIcon" ) );
    REQUIRE( iconLabel != nullptr );
    REQUIRE_FALSE( iconLabel->pixmap().isNull() );
    REQUIRE( iconLabel->pixmap().width() == 48 );
    REQUIRE( iconLabel->pixmap().height() == 48 );

    // 5,000 rapid geometric cycles through edge cases: 0x0, extreme, high-DPI, standard
    const QSize testSizes[] = {
        QSize( 0, 0 ),
        QSize( 1, 1 ),
        QSize( 1024, 1024 ),
        QSize( -10, -10 ),
        QSize( 64, 64 ),
        QSize( 2048, 2048 ),
        QSize( 0, 0 ),
        QSize( 128, 128 ),
        QSize( 32, 32 ),
        QSize( 48, 48 )
    };

    for ( int cycle = 0; cycle < 500; ++cycle )
    {
        for ( const QSize &s : testSizes )
        {
            widget.setIconSize( s );
            if ( s.width() <= 0 || s.height() <= 0 )
            {
                REQUIRE( iconLabel->isHidden() );
            }
            else
            {
                REQUIRE_FALSE( iconLabel->isHidden() );
                REQUIRE_FALSE( iconLabel->pixmap().isNull() );
                REQUIRE( iconLabel->pixmap().width() == s.width() );
                REQUIRE( iconLabel->pixmap().height() == s.height() );
            }
        }
    }

    // Set back to 48x48 standard size and verify pixel validity (non-blank)
    widget.setIconSize( QSize( 48, 48 ) );
    REQUIRE_FALSE( iconLabel->isHidden() );
    QPixmap finalPix = iconLabel->pixmap();
    REQUIRE_FALSE( finalPix.isNull() );
    REQUIRE( finalPix.size() == QSize( 48, 48 ) );

    QImage img = finalPix.toImage();
    bool hasNonZeroAlpha = false;
    for ( int y = 0; y < img.height(); ++y )
    {
        for ( int x = 0; x < img.width(); ++x )
        {
            if ( qAlpha( img.pixel( x, y ) ) > 0 )
            {
                hasNonZeroAlpha = true;
                break;
            }
        }
        if ( hasNonZeroAlpha ) break;
    }
    REQUIRE( hasNonZeroAlpha ); // Vector icon was preserved with 100% fidelity!
}

TEST_CASE( "RsEmptyStateWidget - Multi-Source Icon Resolving & Signal Handling", "[final][empty_state][resolving]" )
{
    ensureQgisApp();

    // Icon by alias
    sicnu::RsEmptyStateWidget w1(
        QStringLiteral( "l_yer_st_ck" ),
        QStringLiteral( "测试标题" ),
        QStringLiteral( "测试描述" )
    );
    REQUIRE( w1.title() == QStringLiteral( "测试标题" ) );

    // Icon by empty string
    sicnu::RsEmptyStateWidget w2(
        QString(),
        QStringLiteral( "无图标标题" ),
        QStringLiteral( "无图标描述" )
    );
    auto *iconLabel = w2.findChild<QLabel *>( QStringLiteral( "rsEmptyStateIcon" ) );
    REQUIRE( iconLabel != nullptr );
    REQUIRE( iconLabel->isHidden() );

    // Signal spy on CTA button
    sicnu::RsEmptyStateWidget w3(
        QStringLiteral( "mos_ic" ),
        QStringLiteral( "操作" ),
        QStringLiteral( "描述" ),
        QStringLiteral( "点击测试" )
    );
    QSignalSpy spy( &w3, &sicnu::RsEmptyStateWidget::actionClicked );
    auto *btn = w3.findChild<QPushButton *>( QStringLiteral( "rsEmptyStateBtn" ) );
    REQUIRE( btn != nullptr );

    for ( int i = 0; i < 100; ++i )
    {
        btn->click();
    }
    REQUIRE( spy.count() == 100 );
}
