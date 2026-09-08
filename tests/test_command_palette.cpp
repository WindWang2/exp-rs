// Workbench 5.0 — Command palette contract (Milestone D)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/command_palette.h"
#include "app/workbench/command_registry.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QTest>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_command_palette";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  QCoreApplication::setOrganizationName( QStringLiteral( "sicnu-test" ) );
  QCoreApplication::setApplicationName( QStringLiteral( "sicnu-palette-test" ) );
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

using sicnu::app::CommandDefinition;
using sicnu::app::SelectionContextSnapshot;

CommandDefinition def( const char *id, const QString &title, const QStringList &keywords = {} )
{
  CommandDefinition d;
  d.id = QString::fromUtf8( id );
  d.title = title;
  d.description = QStringLiteral( "palette 测试命令" );
  d.category = QStringLiteral( "测试" );
  d.keywords = keywords;
  d.handler = [] {};
  return d;
}

} // namespace

TEST_CASE( "Palette: empty query lists recent-first entries (bounded)",
           "[command_palette][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  REQUIRE( registry.registerCommand( def( "aa.first", QStringLiteral( "打开工程" ) ) ) );
  REQUIRE( registry.registerCommand( def( "bb.second", QStringLiteral( "波段数学" ),
                                          QStringList{ QStringLiteral( "band math" ) } ) ) );

  sicnu::app::CommandPalette palette( &registry );
  palette.openPalette();

  auto *list = palette.findChild<QListWidget *>( QStringLiteral( "rsCommandPaletteList" ) );
  REQUIRE( list );
  REQUIRE( list->count() == 2 ); // both commands visible with empty query

  // Run one THROUGH the palette (runCurrent = the Enter path) - recency is a palette feature.
  for ( int i = 0; i < list->count(); ++i )
  {
    if ( list->item( i )->data( Qt::UserRole ).toString() == QLatin1String( "bb.second" ) )
      list->setCurrentRow( i );
  }
  palette.runCurrent();
  QTest::qWait( 1 );

  palette.openPalette();
  REQUIRE( list->count() == 2 );
  REQUIRE( list->item( 0 )->data( Qt::UserRole ).toString() == QLatin1String( "bb.second" ) );
}

TEST_CASE( "Palette: query filters by title prefix, keyword and id",
           "[command_palette][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  REQUIRE( registry.registerCommand( def( "project.open", QStringLiteral( "打开工程" ) ) ) );
  REQUIRE( registry.registerCommand( def( "rs.bandMath", QStringLiteral( "波段数学" ),
                                          QStringList{ QStringLiteral( "band math" ),
                                                       QStringLiteral( "bandmath" ) } ) ) );
  REQUIRE( registry.registerCommand( def( "map.zoomIn", QStringLiteral( "放大" ) ) ) );

  sicnu::app::CommandPalette palette( &registry );
  palette.openPalette();
  auto *list = palette.findChild<QListWidget *>( QStringLiteral( "rsCommandPaletteList" ) );
  auto *input = palette.findChild<QLineEdit *>( QStringLiteral( "rsCommandPaletteInput" ) );
  REQUIRE( list );
  REQUIRE( input );

  QTest::keyClicks( input, QStringLiteral( "band" ) );
  QTest::qWait( 1 );
  REQUIRE( list->count() == 1 );
  REQUIRE( list->item( 0 )->data( Qt::UserRole ).toString() == QLatin1String( "rs.bandMath" ) );

  input->clear();
  QTest::keyClicks( input, QStringLiteral( "project." ) );
  QTest::qWait( 1 );
  REQUIRE( list->count() == 1 );
  REQUIRE( list->item( 0 )->data( Qt::UserRole ).toString() == QLatin1String( "project.open" ) );
}

TEST_CASE( "Palette: unavailable commands stay visible with reasons and never run",
           "[command_palette][contract]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  int runs = 0;
  CommandDefinition d = def( "sar.speckle", QStringLiteral( "斑点滤波" ) );
  d.availability = []( const SelectionContextSnapshot &s ) { return s.hasSar; };
  d.explain = []( const SelectionContextSnapshot & ) {
    return QStringLiteral( "需要选中 SAR 数据" );
  };
  d.handler = [&runs] { ++runs; };
  REQUIRE( registry.registerCommand( d ) );
  registry.setSnapshotProvider( [] { return SelectionContextSnapshot{}; } );

  sicnu::app::CommandPalette palette( &registry );
  palette.openPalette();
  auto *list = palette.findChild<QListWidget *>( QStringLiteral( "rsCommandPaletteList" ) );
  REQUIRE( list );
  REQUIRE( list->count() == 1 );
  // The reason is user-visible on the row:
  REQUIRE( list->item( 0 )->text().contains( QStringLiteral( "需要选中 SAR 数据" ) ) );

  // Enter on the unavailable row must NOT execute and closes nothing:
  QTest::keyClick( list, Qt::Key_Return );
  REQUIRE( runs == 0 );
}

TEST_CASE( "Palette: Escape closes without running", "[command_palette][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  REQUIRE( registry.registerCommand( def( "a.cmd", QStringLiteral( "命令A" ) ) ) );

  sicnu::app::CommandPalette palette( &registry );
  palette.openPalette();
  REQUIRE( palette.isVisible() );
  QTest::keyClick( &palette, Qt::Key_Escape );
  QTest::qWait( 1 );
  REQUIRE_FALSE( palette.isVisible() );
}
