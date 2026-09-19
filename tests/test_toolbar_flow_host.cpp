// test_toolbar_flow_host.cpp — #1056: rebuilding the product toolbar chips
// while a drag/resize gesture is in flight must drop the interaction state.
// m_dragChip / m_resizeChip point INTO m_chips (a QList), so a rebuild
// without clearing them leaves dangling pointers that the next mouse move
// dereferences.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QMouseEvent>
#include <QToolBar>
#include <QWidget>

#include "app/widgets/rs_toolbar_flow_host.h"

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_toolbar_flow_host";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

void sendMouse( QWidget *target, QEvent::Type type, const QPointF &globalPos,
                Qt::MouseButtons buttons = Qt::LeftButton )
{
  QMouseEvent event( type, QPointF( 1, 1 ), globalPos, Qt::LeftButton, buttons, Qt::NoModifier );
  QApplication::sendEvent( target, &event );
}

} // namespace

TEST_CASE( "RsToolbarFlowHost: chip rebuild clears an in-flight drag (#1056)",
           "[app][toolbar][1056]" )
{
  ensureApp();
  RsToolbarFlowHost host;
  QToolBar barA( QStringLiteral( "flowA" ), &host );
  QToolBar barB( QStringLiteral( "flowB" ), &host );

  host.setProductToolbars( { &barA, &barB } );
  host.resize( 800, 64 );
  REQUIRE( host.hasProductToolbars() );
  REQUIRE_FALSE( host.isInteractionActiveForTest() );

  auto *grip = host.findChild<QWidget *>( QStringLiteral( "rsToolbarDragGrip" ) );
  REQUIRE( grip != nullptr );

  // Start a drag through the host's event filter.
  sendMouse( grip, QEvent::MouseButtonPress, QPointF( 5, 5 ) );
  CHECK( host.isInteractionActiveForTest() );

  // Rebuild the chip list while the gesture is active: the old chip pointers
  // are about to be invalidated, so the interaction state must be dropped.
  host.setProductToolbars( { &barA, &barB } );
  CHECK_FALSE( host.isInteractionActiveForTest() );

  // A move on a NEW grip must not dereference a stale chip. (Pre-fix this
  // entered the drag branch with a dangling m_dragChip.) The old frames are
  // deleteLater()d, so flush deferred deletes before looking the new grip up.
  QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
  auto *newGrip = host.findChild<QWidget *>( QStringLiteral( "rsToolbarDragGrip" ) );
  REQUIRE( newGrip != nullptr );
  REQUIRE( newGrip != grip );
  sendMouse( newGrip, QEvent::MouseMove, QPointF( 30, 10 ) );
  CHECK_FALSE( host.isInteractionActiveForTest() );

  // And a fresh gesture still works end-to-end.
  sendMouse( newGrip, QEvent::MouseButtonPress, QPointF( 6, 6 ) );
  CHECK( host.isInteractionActiveForTest() );
  sendMouse( newGrip, QEvent::MouseButtonRelease, QPointF( 6, 6 ), Qt::NoButton );
  CHECK_FALSE( host.isInteractionActiveForTest() );
}

TEST_CASE( "RsToolbarFlowHost: chip rebuild clears an in-flight resize (#1056)",
           "[app][toolbar][1056]" )
{
  ensureApp();
  RsToolbarFlowHost host;
  QToolBar bar( QStringLiteral( "flowC" ), &host );
  host.setProductToolbars( { &bar } );
  host.resize( 800, 64 );

  auto *resize = host.findChild<QWidget *>( QStringLiteral( "rsToolbarResizeGrip" ) );
  REQUIRE( resize != nullptr );
  sendMouse( resize, QEvent::MouseButtonPress, QPointF( 5, 5 ) );
  CHECK( host.isInteractionActiveForTest() );

  host.setProductToolbars( { &bar } );
  CHECK_FALSE( host.isInteractionActiveForTest() );
}
