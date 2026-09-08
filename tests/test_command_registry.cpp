// Workbench 5.0 — CommandRegistry contract (Milestone C)
#include <catch2/catch_test_macros.hpp>

#include "app/workbench/command_registry.h"

#include <QAction>
#include <QApplication>

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_command_registry";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

using sicnu::app::CommandDefinition;
using sicnu::app::SelectionContextSnapshot;

CommandDefinition simple( const char *id, std::function<void()> handler = {} )
{
  CommandDefinition d;
  d.id = QString::fromUtf8( id );
  d.title = QStringLiteral( "命令 %1" ).arg( d.id );
  d.description = QStringLiteral( "测试命令" );
  d.category = QStringLiteral( "测试" );
  d.handler = handler ? std::move( handler ) : [] {};
  return d;
}

} // namespace

TEST_CASE( "CommandRegistry: rejects empty ids, missing handlers and duplicates",
           "[command_registry][contract]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;

  REQUIRE_FALSE( registry.registerCommand( simple( "" ) ) );

  CommandDefinition noHandler = simple( "x.noHandler" );
  noHandler.handler = nullptr;
  REQUIRE_FALSE( registry.registerCommand( noHandler ) );

  REQUIRE( registry.registerCommand( simple( "map.zoomIn" ) ) );
  REQUIRE_FALSE( registry.registerCommand( simple( "map.zoomIn" ) ) ); // duplicate id
  REQUIRE( registry.count() == 1 );
}

TEST_CASE( "CommandRegistry: shortcut uniqueness is enforced at registration",
           "[command_registry][contract]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;

  CommandDefinition a = simple( "view.zoom" );
  a.shortcut = QKeySequence( QStringLiteral( "Ctrl++" ) );
  CommandDefinition b = simple( "view.widen" );
  b.shortcut = QKeySequence( QStringLiteral( "Ctrl++" ) ); // same binding

  REQUIRE( registry.registerCommand( a ) );
  REQUIRE_FALSE( registry.registerCommand( b ) ); // duplicate shortcut rejected

  CommandDefinition c = simple( "view.narrow" );
  c.shortcut = QKeySequence( QStringLiteral( "Ctrl+-" ) );
  REQUIRE( registry.registerCommand( c ) );
  REQUIRE( registry.count() == 2 );
}

TEST_CASE( "CommandRegistry: projections follow availability and carry reasons",
           "[command_registry][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;

  CommandDefinition d = simple( "layer.remove" );
  d.availability = []( const SelectionContextSnapshot &s ) { return s.hasVector; };
  d.explain = []( const SelectionContextSnapshot & ) {
    return QStringLiteral( "需要选中矢量图层" );
  };
  REQUIRE( registry.registerCommand( d ) );

  QAction *action = registry.action( "layer.remove" );
  REQUIRE( action );

  // No snapshot provider → default snapshot (no vector) → disabled + reason.
  registry.refreshAll();
  REQUIRE_FALSE( action->isEnabled() );
  REQUIRE( registry.unavailabilityReason( "layer.remove" ) == QStringLiteral( "需要选中矢量图层" ) );

  registry.setSnapshotProvider( [] {
    SelectionContextSnapshot s;
    s.hasVector = true;
    return s;
  } );
  REQUIRE( action->isEnabled() );
  REQUIRE( registry.unavailabilityReason( "layer.remove" ).isEmpty() );
}

TEST_CASE( "CommandRegistry: action triggers the single handler with availability guard",
           "[command_registry][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  int runs = 0;

  CommandDefinition d = simple( "rs.bandMath", [&runs] { ++runs; } );
  d.availability = []( const SelectionContextSnapshot &s ) { return s.hasRaster; };
  REQUIRE( registry.registerCommand( d ) );

  registry.setSnapshotProvider( [] { return SelectionContextSnapshot{}; } );
  REQUIRE_FALSE( registry.trigger( "rs.bandMath" ) ); // unavailable → no run
  REQUIRE( runs == 0 );

  registry.setSnapshotProvider( [] {
    SelectionContextSnapshot s;
    s.hasRaster = true;
    return s;
  } );
  REQUIRE( registry.trigger( "rs.bandMath" ) );
  REQUIRE( runs == 1 );
}

TEST_CASE( "CommandRegistry: stale projection clicks never run unavailable commands",
           "[command_registry][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  int runs = 0;

  CommandDefinition d = simple( "sar.speckle", [&runs] { ++runs; } );
  d.availability = []( const SelectionContextSnapshot &s ) { return s.hasSar; };
  REQUIRE( registry.registerCommand( d ) );
  registry.setSnapshotProvider( [] {
    SelectionContextSnapshot s;
    s.hasSar = true;
    return s;
  } );

  QAction *action = registry.action( "sar.speckle" );
  REQUIRE( action->isEnabled() );

  // Context flips to optical; refreshAll happens on selection change — a stale
  // click after refresh must be swallowed by the guard inside the projection.
  registry.setSnapshotProvider( [] { return SelectionContextSnapshot{}; } );
  registry.refreshAll();
  REQUIRE_FALSE( action->isEnabled() );
  action->trigger();
  REQUIRE( runs == 0 );
}

TEST_CASE( "CommandRegistry: checkable projections reflect checkedState",
           "[command_registry][behavior]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;

  CommandDefinition d = simple( "layer.toggleEditing" );
  d.checkable = true;
  d.checkedState = []( const SelectionContextSnapshot &s ) { return s.activeEditable; };
  REQUIRE( registry.registerCommand( d ) );

  QAction *action = registry.action( "layer.toggleEditing" );
  REQUIRE( action->isCheckable() );
  REQUIRE_FALSE( action->isChecked() );

  registry.setSnapshotProvider( [] {
    SelectionContextSnapshot s;
    s.activeEditable = true;
    return s;
  } );
  registry.refreshAll();
  REQUIRE( action->isChecked() );
}

TEST_CASE( "CommandRegistry: definitions are queryable and sorted", "[command_registry]" )
{
  ensureApp();
  sicnu::app::CommandRegistry registry;
  REQUIRE( registry.registerCommand( simple( "b.second" ) ) );
  REQUIRE( registry.registerCommand( simple( "a.first" ) ) );
  REQUIRE( registry.commandIds() == QStringList{ "a.first", "b.second" } );
  REQUIRE( registry.definition( "a.first" ) != nullptr );
  REQUIRE( registry.definition( "missing" ) == nullptr );
  REQUIRE( registry.definition( "a.first" )->title.startsWith( QStringLiteral( "命令" ) ) );
}
