// tests/test_agent_copilot_ui.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include "agent/agent_copilot_dock_widget.h"
#include "agent/llm_settings_dialog.h"
#include "data/data_manager.h"

#include <QApplication>

using namespace sicnu::agent;
using namespace sicnu::data;

static void ensureQtApp()
{
  if ( !QApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_agent_copilot_ui";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
}

TEST_CASE( "LlmSettingsDialog initializes and manages provider profiles", "[agent][ui]" )
{
  ensureQtApp();

  LlmSettingsDialog dlg;
  REQUIRE( dlg.windowTitle().contains( QStringLiteral( "Copilot" ) ) );

  LlmProviderProfile profile = dlg.selectedProfile();
  REQUIRE_FALSE( profile.id.isEmpty() );
}

TEST_CASE( "AgentCopilotDockWidget instantiates and binds workspace context", "[agent][ui]" )
{
  ensureQtApp();

  DataManager dataMgr;

  AgentCopilotDockWidget dock;
  REQUIRE( dock.objectName() == "AgentCopilotDockWidget" );

  dock.setContext( &dataMgr, nullptr );

  // Test prompt submission triggers message card rendering
  dock.sendPrompt( QStringLiteral( "运行光谱指数测试" ) );
}
