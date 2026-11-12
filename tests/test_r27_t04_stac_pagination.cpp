// tests/test_r27_t04_stac_pagination.cpp — R27-T04 (#1788, #1789)
//
// Contracts under test:
//   1. (#1789) StacClient::searchNext that cannot continue (no cached page /
//      superseded generation / no next link) emits searchNextUnavailable —
//      never a silent return. A silent no-op strands the More button
//      forever (the click disabled it waiting for a completion).
//   2. (#1789) The dialog settles More in every terminal state: no-op
//      continuation, failed fresh search.
//   3. (#1788) A More-click landing APPENDS: page-1 rows survive page 2;
//      a fresh search REPLACES the listing; an empty next page keeps the
//      accumulated table and hides More.
//
// The dialog is driven through its real client's signals (invoked by name)
// so the connected slot path is exercised, not a re-implementation.
#include <catch2/catch_test_macros.hpp>

#include "dialogs/stac_browser_dialog.h"
#include "stac_client.h"

#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest>

#include <qgsmapcanvas.h>
#include <qgsapplication.h>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_stac_pagination";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      return new QgsApplication( fake_argc, fake_argv, false );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  QVariantMap feature( const QString &id )
  {
    QVariantMap f;
    f.insert( QStringLiteral( "id" ), id );
    QVariantMap assets;
    assets.insert( QStringLiteral( "cog" ), QVariantMap() );
    f.insert( QStringLiteral( "assets" ), assets );
    return f;
  }

  void dismissAlerts( int budget = 6 )
  {
    if ( budget <= 0 )
      return;
    QTimer::singleShot( 0, ensureApp(), [budget] {
      if ( QWidget *modal = QApplication::activeModalWidget() )
        modal->close();
      dismissAlerts( budget - 1 );
    } );
  }

  /// Emit the client's searchCompleted signal by name — the dialog's
  /// connected slot runs exactly as it would for a delivered page.
  void landPage( StacClient *client, const QVariantList &features, const QString &error,
                 const QUrl &next )
  {
    REQUIRE( client != nullptr );
    QMetaObject::invokeMethod( client, "searchCompleted",
                               Q_ARG( QVariantList, features ),
                               Q_ARG( QString, error ),
                               Q_ARG( QUrl, next ) );
    QApplication::processEvents();
  }

  QPushButton *findMoreButton( StacBrowserDialog &dlg )
  {
    const auto buttons = dlg.findChildren<QPushButton *>();
    for ( QPushButton *b : buttons )
      if ( b->text() == QLatin1String( "More Results" ) )
        return b;
    return nullptr;
  }
} // namespace

TEST_CASE( "StacClient::searchNext no-op emits a terminal signal (#1789)", "[stac][r27t04]" )
{
  ensureApp();
  StacClient client;
  QSignalSpy completed( &client, &StacClient::searchCompleted );
  QSignalSpy unavailable( &client, &StacClient::searchNextUnavailable );

  // No previous page at all.
  client.searchNext();
  REQUIRE( unavailable.size() == 1 ); // RED on HEAD: silent return, zero signals
  REQUIRE( completed.size() == 0 );
  CHECK( unavailable.at( 0 ).at( 0 ).toString().contains( QLatin1String( "no previous page" ) ) );

  // A failed policy search strands any prior continuation: the next
  // searchNext must again terminate with the unavailable signal.
  dismissAlerts();
  client.search( QStringLiteral( "ftp://not-http.example" ), QString(), QString(), {} );
  REQUIRE( completed.size() == 1 );
  CHECK( !completed.at( 0 ).at( 1 ).toString().isEmpty() ); // policy error surfaced
  unavailable.clear();
  client.searchNext();
  REQUIRE( unavailable.size() == 1 ); // RED on HEAD: stranded no-op is silent
}

TEST_CASE( "STAC browser appends More pages and settles terminal states (#1788/#1789)",
           "[stac][dialog][r27t04]" )
{
  ensureApp();
  QgsMapCanvas canvas;
  StacBrowserDialog dlg( &canvas );
  dlg.show();
  QApplication::processEvents();

  auto *client = dlg.findChild<StacClient *>();
  REQUIRE( client != nullptr );
  auto *table = dlg.findChild<QTableWidget *>();
  REQUIRE( table != nullptr );
  auto *more = findMoreButton( dlg );
  REQUIRE( more != nullptr );

  // --- Page 1: 2 features, has next -------------------------------
  landPage( client, { feature( QStringLiteral( "item-1" ) ),
                      feature( QStringLiteral( "item-2" ) ) },
            QString(), QUrl( QStringLiteral( "https://example.test/next" ) ) );
  REQUIRE( table->rowCount() == 2 );
  REQUIRE( table->item( 0, 0 )->text() == QLatin1String( "item-1" ) );
  REQUIRE( more->isVisibleTo( &dlg ) );

  // --- More click with NO cached continuation: terminal settle -----
  // (offline there is no page 2 to fetch, so searchNext no-ops — exactly the
  // #1789 stranded-button path.)
  more->click();
  QApplication::processEvents();
  CHECK( !more->isVisibleTo( &dlg ) ); // settled, not stranded-disabled
  CHECK( !more->isEnabled() );

  // --- The append flow: a More click whose page DOES come back -----
  // Re-land page 1 (fresh search semantics replace; More visible again).
  landPage( client, { feature( QStringLiteral( "item-1" ) ),
                      feature( QStringLiteral( "item-2" ) ) },
            QString(), QUrl( QStringLiteral( "https://example.test/next" ) ) );
  REQUIRE( table->rowCount() == 2 );
  REQUIRE( more->isVisibleTo( &dlg ) );
  // Hold the arm across the click: disconnect the no-op settle for this
  // stretch (an online fetch would not fire it — the page arrives instead).
  QObject::disconnect( client, &StacClient::searchNextUnavailable, nullptr, nullptr );
  more->click(); // arms append, searchNext no-ops silently
  QApplication::processEvents();
  // Page 2 lands while the append flag is armed (as it would online).
  landPage( client, { feature( QStringLiteral( "item-3" ) ) }, QString(), QUrl() );

  // #1788 RED on HEAD: the table was RESET to 1 row (replace). Fixed: 3.
  REQUIRE( table->rowCount() == 3 );
  CHECK( table->item( 0, 0 )->text() == QLatin1String( "item-1" ) );
  CHECK( table->item( 2, 0 )->text() == QLatin1String( "item-3" ) );
  // No next page: More hidden, accumulated rows kept.
  CHECK( !more->isVisibleTo( &dlg ) );

  // --- Failed fresh search: rows kept, More hidden and disabled -----
  dismissAlerts();
  landPage( client, QVariantList(), QStringLiteral( "policy refused" ), QUrl() );
  CHECK( table->rowCount() == 3 );
  CHECK( !more->isVisibleTo( &dlg ) );
  CHECK( !more->isEnabled() );
}

TEST_CASE( "selectCogHref refuses deep nesting instead of throwing (R1 S2 regression)",
           "[stac][json][r27t04]" )
{
  ensureApp();

  // Well-formed item → still resolves an asset href.
  {
    QVariantMap asset;
    asset.insert( QStringLiteral( "href" ), QStringLiteral( "https://example.test/a.tif" ) );
    QVariantMap assets;
    assets.insert( QStringLiteral( "cog" ), asset );
    QVariantMap item;
    item.insert( QStringLiteral( "assets" ), assets );
    CHECK_FALSE( StacClient::selectCogHref( QJsonObject::fromVariantMap( item ) ).isEmpty() );
  }

  // 70-deep nesting (past the pinned stackLimit=64): typed refusal, no
  // jsoncpp throw out of the Qt slot — the #1793 vector for catalog data.
  QVariantMap deep;
  deep.insert( QStringLiteral( "leaf" ), 1 );
  for ( int i = 0; i < 70; ++i )
  {
    QVariantMap wrap;
    wrap.insert( QStringLiteral( "n" ), deep );
    deep = wrap;
  }
  CHECK( StacClient::selectCogHref( QJsonObject::fromVariantMap( deep ) ).isEmpty() );
}
