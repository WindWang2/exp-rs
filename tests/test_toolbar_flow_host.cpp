#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QToolBar>
#include <QUuid>

#include "widgets/rs_toolbar_flow_host.h"

namespace
{
void ensureApplication()
{
    if ( QApplication::instance() )
        return;
    static int argc = 1;
    static char name[] = "test_toolbar_flow_host";
    static char *argv[] = { name, nullptr };
    static QApplication application( argc, argv );
}
} // namespace

TEST_CASE( "Toolbar rows follow font metrics across wrapping and visibility changes",
           "[gui][toolbar][a11y]" )
{
    ensureApplication();
    RsToolbarFlowHost host;
    QList<QToolBar *> bars;
    QHash<QToolBar *, bool> visible;
    for ( int i = 0; i < 3; ++i )
    {
        auto *bar = new QToolBar( &host );
        // Avoid persisted user widths without changing the process settings.
        bar->setObjectName( QUuid::createUuid().toString() );
        bar->addAction( QStringLiteral( "Action %1" ).arg( i ) );
        bars.append( bar );
        visible.insert( bar, true );
    }
    host.setProductToolbars( bars );
    host.applyVisibility( visible );
    host.show();

    for ( int pixels : { 13, 26, 13 } )
    {
        QFont font = host.font();
        font.setPixelSize( pixels );
        host.setFont( font );
        for ( int width : { 640, 1000 } )
        {
            INFO( "font pixels " << pixels << ", host width " << width );
            host.resize( width, host.height() );
            QCoreApplication::processEvents();
            const int rowHeight = SicnuUi::Tokens::toolbarRowHeight( host.fontMetrics() );
            const int rows = width == 640 ? 2 : 1;
            CHECK( host.usedRows() == rows );
            CHECK( host.usedHeight() == rows * rowHeight );
            CHECK( host.height() == rows * rowHeight );
            for ( int i = 0; i < bars.size(); ++i )
            {
                const QToolBar *bar = bars.at( i );
                REQUIRE( bar->parentWidget() != nullptr );
                CHECK( bar->height() == rowHeight );
                CHECK( bar->parentWidget()->height() == rowHeight );
                CHECK( bar->parentWidget()->y() == ( rows == 2 && i == 2 ? rowHeight : 0 ) );
            }
        }
    }

    host.applyVisibility( {} );
    CHECK( host.usedRows() == 0 );
    CHECK( host.height() == 0 );
    host.applyVisibility( { { bars.first(), true } } );
    CHECK( host.usedRows() == 1 );
    CHECK( host.height() == SicnuUi::Tokens::toolbarRowHeight( host.fontMetrics() ) );
}
