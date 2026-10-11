/***************************************************************************
  tests/test_ui_state_honesty_r8.cpp

  R8 WP02/WP06 source contracts, in the scan style of
  test_classification_window.cpp (heavy Qt/QGIS widget harnesses are not
  buildable per-assertion here; the source is the contract):

   * GuidedWorkflowWidget must gate the Next button on the running job —
     "Experiment complete!" used to be declarable while the final step's
     job was still in flight, and its eventual failure was then suppressed
     by the inactive-workflow guard (#1453 / WP02 finding 2).
   * The Help menu's teaching entries must be the CommandRegistry
     projection (addCmd), never parallel QActions re-claiming registered
     commands — the menus file's own convention comment (WP06 finding 2).
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readSource( const std::string &relativePath )
{
  std::ifstream in( std::string( CMAKE_SOURCE_DIR ) + "/" + relativePath, std::ios::binary );
  if ( !in )
    return {};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

} // namespace

TEST_CASE( "guided workflow gates Next on the running job", "[ui_honesty][r8]" )
{
  const std::string source = readSource( "src/app/widgets/guided_workflow_widget.cpp" );
  REQUIRE_FALSE( source.empty() );

  // showStep: Next must be state-driven exactly like Run — disabled while
  // this step's job is in flight, re-armed by restoreButton's
  // updateStepDisplay() when the job lands.
  CHECK( source.find( "m_nextButton->setEnabled( !m_jobHandle->isRunning() );" ) !=
         std::string::npos );

  // onNextStep: the completion branch (workflow complete) must never run
  // while a job is still in flight.
  CHECK( source.find( "if ( m_jobHandle->isRunning() )\n        return;" ) !=
         std::string::npos );
}

TEST_CASE( "the teaching menu entries are the command-registry projection",
           "[ui_honesty][r8]" )
{
  const std::string menus = readSource( "src/app/main_window_menus.cpp" );
  REQUIRE_FALSE( menus.empty() );

  // Both teaching commands are registered in workbench/command_defs.cpp;
  // the menus file's own convention says a registered command's menu item
  // MUST be the registry projection (single shortcut owner, help binding
  // via attachCommandRegistry) — the old parallel QActions left the rich
  // commands.json descriptors unreachable from every UI surface.
  CHECK( menus.find( "addCmd( helpMenu, \"teaching.labCockpit.show\" )" ) !=
         std::string::npos );
  CHECK( menus.find( "addCmd( helpMenu, \"teaching.adminConsole.show\" )" ) !=
         std::string::npos );

  // And the parallel-QAction shape for the same handlers is gone.
  CHECK( menus.find( "helpMenu->addAction( ic( \"workflow\" ), tr( \"Undergraduate Lab Teaching Workbench\" )" ) ==
         std::string::npos );
  CHECK( menus.find( "helpMenu->addAction( ic( \"workflow\" ), tr( \"Teacher Authoring & Assessment Console\" )" ) ==
         std::string::npos );
}
