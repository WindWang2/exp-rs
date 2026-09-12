// test_plugin_ui_placement.cpp — Workbench 9.0 M8: plugin command lifecycle
//
// The declarative rendering pipeline itself (ui.describe/ui.invoke, host
// widgets) is covered by the plugin platform's conformance kit; the SHELL
// contract this track owns is the command lifecycle: each rendered menu
// contribution becomes a registry command, the command triggers its action,
// and releasing the plugin disables then clears its commands so a reload can
// re-register cleanly.
#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>

#include "app/workbench/command_registry.h"
#include "app/workbench/plugin_command_defs.h"

using sicnu::app::CommandRegistry;

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test_plugin_ui_placement";
char *fake_argv[] = { fake_argv0, nullptr };

QCoreApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app && !QCoreApplication::instance() )
    app = new QApplication( fake_argc, fake_argv );
  return app;
}

} // namespace

TEST_CASE( "Plugin menu contributions register as actionable registry commands",
           "[m8][plugin_ui]" )
{
  ensureApp();
  CommandRegistry registry;

  QAction detect( QStringLiteral( "检测建筑" ), nullptr );
  QAction segment( QStringLiteral( "&语义分割" ), nullptr ); // ampersand stripped

  const int registered = sicnu::app::registerPluginMenuCommands(
    &registry, { &detect, &segment }, QStringLiteral( "seg_plugin" ),
    QStringLiteral( "插件" ) );

  REQUIRE( registered == 2 );
  REQUIRE( registry.commandIds().contains( QStringLiteral( "plugin.seg_plugin.0" ) ) );
  REQUIRE( registry.commandIds().contains( QStringLiteral( "plugin.seg_plugin.1" ) ) );

  // Titles come from the actions, ampersand-free for palette matching.
  CHECK( registry.definition( QStringLiteral( "plugin.seg_plugin.1" ) )->title
         == QStringLiteral( "语义分割" ) );

  // The command handler TRIGGERS the underlying action (the rendered widget
  // path), it does not re-implement it.
  {
    int triggers = 0;
    QObject::connect( &detect, &QAction::triggered, &detect,
                      [&] { ++triggers; }, Qt::DirectConnection );
    registry.action( QStringLiteral( "plugin.seg_plugin.0" ) )->trigger();
    CHECK( triggers == 1 );
  }
}

TEST_CASE( "Plugin commands follow the rendered action's lifetime, then clear",
           "[m8][plugin_ui]" )
{
  ensureApp();
  CommandRegistry registry;

  QAction *survivor = new QAction( QStringLiteral( "存活" ), nullptr );
  QAction *doomed = new QAction( QStringLiteral( "将死" ), nullptr );
  const int registered =
    sicnu::app::registerPluginMenuCommands( &registry, { survivor, doomed },
                                            QStringLiteral( "life" ), QStringLiteral( "插件" ) );
  REQUIRE( registered == 2 );

  // Deleting the rendered action (shell releaseUi) disables the command —
  // no crash on trigger, no dead entry left enabled.
  delete doomed;
  registry.refreshAll();
  CHECK_FALSE( registry.action( QStringLiteral( "plugin.life.1" ) )->isEnabled() );
  CHECK( registry.action( QStringLiteral( "plugin.life.0" ) )->isEnabled() );

  // Unload path: the release hook clears the whole plugin namespace.
  const int removed = registry.unregisterCommandsMatching( QStringLiteral( "plugin.life." ) );
  CHECK( removed == 2 );
  CHECK( registry.definition( QStringLiteral( "plugin.life.0" ) ) == nullptr );

  // Reload path: the same ids can register again (A3 — the old duplicate
  // rejection used to strand a plugin permanently after re-attach).
  QAction again( QStringLiteral( "再次注册" ), nullptr );
  REQUIRE( sicnu::app::registerPluginMenuCommands(
             &registry, { &again }, QStringLiteral( "life" ), QStringLiteral( "插件" ) ) == 1 );
  CHECK( registry.definition( QStringLiteral( "plugin.life.0" ) ) != nullptr );
}
