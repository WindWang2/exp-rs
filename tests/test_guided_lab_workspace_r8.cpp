/***************************************************************************
  tests/test_guided_lab_workspace_r8.cpp

  R8 (WP02, #1453-adjacent) behavioral regression for GuidedLabWorkspace,
  as a STANDALONE twin of the fail-closed affordances case in
  test_teaching_cockpit_smoke.cpp: the full smoke suite links the whole
  LabCockpitDock stack (qgis_core et al.), this twin compiles just the
  workspace TU over Sicnu::teaching + Qt Widgets, so the contract is
  verifiable on lanes that cannot link the heavy dock.

  The fail-closed branch used to disable run/submit/prev/next but leave
  "Validate & grade", "Export capsule" and the artifact edit armed — a
  stepless timeline (a lab whose doc failed to load) still offered
  grading affordances against a session that does not exist.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "app/teaching/guided_lab_workspace.h"
#include "teaching/lab_step_timeline.h"

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>

#include <json/json.h>

#include <memory>
#include <string>

namespace
{

QApplication *ensureApp()
{
  static QApplication *app = nullptr;
  if ( !app )
  {
    static int fake_argc = 1;
    static char fake_argv[] = "test_guided_lab_workspace_r8";
    static char *fake_argv_ptr[] = { fake_argv };
    app = new QApplication( fake_argc, fake_argv_ptr );
  }
  return app;
}

} // namespace

using sicnu::app::teaching::GuidedLabWorkspace;

TEST_CASE( "fail-closed timeline disables validate/export affordances",
           "[teaching][workspace][r8]" )
{
  ensureApp();
  GuidedLabWorkspace ws;

  auto *validateBtn =
    ws.findChild<QPushButton *>( QStringLiteral( "labWorkspaceValidateButton" ) );
  auto *exportBtn =
    ws.findChild<QPushButton *>( QStringLiteral( "labWorkspaceExportButton" ) );
  auto *artifactEdit =
    ws.findChild<QLineEdit *>( QStringLiteral( "labWorkspaceArtifactEdit" ) );
  REQUIRE( ( validateBtn && exportBtn && artifactEdit ) );

  // A valid timeline arms every affordance.
  Json::CharReaderBuilder rb;
  std::unique_ptr<Json::CharReader> reader( rb.newCharReader() );
  Json::Value spec;
  std::string errors;
  const std::string text = R"( { "id": "lab_r8_buttons", "title": "Buttons",
    "steps": [ { "title": "Look", "title_zh": "观察", "description_zh": "看。" } ] } )";
  REQUIRE( reader->parse( text.data(), text.data() + text.size(), &spec, &errors ) );
  ws.setTimeline( sicnu::teaching::LabStepTimeline::fromLabSpec( spec ) );
  CHECK( validateBtn->isEnabled() );
  CHECK( exportBtn->isEnabled() );
  CHECK( artifactEdit->isEnabled() );

  // A stepless/fail-closed timeline disarms the whole affordance set.
  ws.setTimeline( sicnu::teaching::LabStepTimeline{} );
  CHECK_FALSE( validateBtn->isEnabled() );
  CHECK_FALSE( exportBtn->isEnabled() );
  CHECK_FALSE( artifactEdit->isEnabled() );

  // Recovery: a later valid timeline re-arms them (no dead-end state).
  ws.setTimeline( sicnu::teaching::LabStepTimeline::fromLabSpec( spec ) );
  CHECK( validateBtn->isEnabled() );
  CHECK( exportBtn->isEnabled() );
  CHECK( artifactEdit->isEnabled() );
}
