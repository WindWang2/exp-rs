#include <catch2/catch_test_macros.hpp>

#include "widgets/rs_empty_state_widget.h"

#include <QApplication>
#include <QPushButton>
#include <QLabel>
#include <QSignalSpy>
#include <QIcon>
#include <QPixmap>

static void ensureApp()
{
    if ( !qApp )
    {
        static int argc = 1;
        static char arg0[] = "test_rs_empty_state_widget";
        static char *argv[] = { arg0, nullptr };
        new QApplication( argc, argv );
    }
}

TEST_CASE( "RsEmptyStateWidget construction and property accessors", "[ui][widget]" )
{
    ensureApp();

    SECTION( "Constructor with action text" )
    {
        sicnu::RsEmptyStateWidget widget(
            QStringLiteral( "l_yer_st_ck" ),
            QStringLiteral( "暂无图层" ),
            QStringLiteral( "打开栅格或矢量数据，图层将在此展示与管理" ),
            QStringLiteral( "打开数据..." ) );

        REQUIRE( widget.title() == QStringLiteral( "暂无图层" ) );
        REQUIRE( widget.description() == QStringLiteral( "打开栅格或矢量数据，图层将在此展示与管理" ) );
        REQUIRE( widget.actionText() == QStringLiteral( "打开数据..." ) );
        REQUIRE( widget.isActionVisible() );
        REQUIRE( widget.objectName() == QStringLiteral( "RsEmptyStateWidget" ) );
    }

    SECTION( "Constructor without action text" )
    {
        sicnu::RsEmptyStateWidget widget(
            QStringLiteral( "log_viewer" ),
            QStringLiteral( "暂无运行日志" ),
            QStringLiteral( "系统事件与算法运行提示将在此实时输出" ) );

        REQUIRE( widget.title() == QStringLiteral( "暂无运行日志" ) );
        REQUIRE( widget.description() == QStringLiteral( "系统事件与算法运行提示将在此实时输出" ) );
        REQUIRE( widget.actionText().isEmpty() );
        REQUIRE_FALSE( widget.isActionVisible() );
    }
}

TEST_CASE( "RsEmptyStateWidget QIcon constructor", "[ui][widget]" )
{
    ensureApp();

    QPixmap pix( 32, 32 );
    pix.fill( Qt::red );
    QIcon customIcon( pix );

    sicnu::RsEmptyStateWidget widget(
        customIcon,
        QStringLiteral( "自定义状态" ),
        QStringLiteral( "这是测试说明" ),
        QStringLiteral( "重试" ) );

    REQUIRE( widget.title() == QStringLiteral( "自定义状态" ) );
    REQUIRE( widget.description() == QStringLiteral( "这是测试说明" ) );
    REQUIRE( widget.actionText() == QStringLiteral( "重试" ) );
    REQUIRE( widget.isActionVisible() );
}

TEST_CASE( "RsEmptyStateWidget dynamic mutators and action visibility", "[ui][widget]" )
{
    ensureApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "d_t_b_se" ),
        QStringLiteral( "初始标题" ),
        QStringLiteral( "初始描述" ) );

    REQUIRE_FALSE( widget.isActionVisible() );

    widget.setTitle( QStringLiteral( "更新后标题" ) );
    REQUIRE( widget.title() == QStringLiteral( "更新后标题" ) );

    widget.setDescription( QStringLiteral( "更新后描述" ) );
    REQUIRE( widget.description() == QStringLiteral( "更新后描述" ) );

    widget.setActionText( QStringLiteral( "开始导入" ) );
    REQUIRE( widget.actionText() == QStringLiteral( "开始导入" ) );
    REQUIRE( widget.isActionVisible() );

    widget.setActionVisible( false );
    REQUIRE_FALSE( widget.isActionVisible() );

    widget.setActionVisible( true );
    REQUIRE( widget.isActionVisible() );

    widget.setIconSize( QSize( 64, 64 ) );
}

TEST_CASE( "RsEmptyStateWidget actionClicked signal emission", "[ui][widget]" )
{
    ensureApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "mos_ic" ),
        QStringLiteral( "未添加影像" ),
        QStringLiteral( "请添加待处理影像" ),
        QStringLiteral( "添加影像" ) );

    QSignalSpy spy( &widget, &sicnu::RsEmptyStateWidget::actionClicked );

    // Find the action button
    auto *btn = widget.findChild<QPushButton *>( QStringLiteral( "rsEmptyStateBtn" ) );
    REQUIRE( btn != nullptr );
    REQUIRE( btn->property( "primary" ).toBool() == true );

    btn->click();

    REQUIRE( spy.count() == 1 );
}

TEST_CASE( "RsEmptyStateWidget layout and minimum size hint sanity", "[ui][widget]" )
{
    ensureApp();

    sicnu::RsEmptyStateWidget widget(
        QStringLiteral( "workflow" ),
        QStringLiteral( "暂无后台任务" ),
        QStringLiteral( "任务执行进度将在此实时显示" ) );

    REQUIRE( widget.layout() != nullptr );
    QSize hint = widget.sizeHint();
    REQUIRE( hint.isValid() );
    REQUIRE( hint.width() > 0 );
    REQUIRE( hint.height() > 0 );
}
