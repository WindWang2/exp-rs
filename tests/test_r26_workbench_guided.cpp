// test_r26_workbench_guided.cpp — R26-02 workbench: guided workflow honesty.
//   WP9a: an output-directory failure must abort the submission (no double
//         error from a task that was doomed anyway).
//   WP9b: a failed operator step must block "Experiment complete!" —
//         workflowCompleted may only fire when no step ended in failure.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>

#include "app/widgets/guided_workflow_widget.h"

#include <QDir>
#include <QFile>
#include <QListWidget>

#include <memory>

namespace
{

void ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    static int argc = 1;
    static char appName[] = "test_r26_workbench_guided";
    static char *argv[] = { appName, nullptr };
    new QApplication( argc, argv );
  }
}

/// The guided widget resolves outputs/ params through resolveRuntimeDataPath,
/// whose "exists" guard cannot adopt a not-yet-created SICNU_DATA_DIR root and
/// then walks up from the CWD. A test must therefore run from a scratch
/// directory so a run never creates output/labs/<lab> inside the source tree
/// (output/ is gitignored, but the test should not depend on that).
void pinRuntimeDataRoot( const QTemporaryDir &dir )
{
  const QString root = dir.filePath( QStringLiteral( "r26-data-root" ) );
  QDir().mkpath( root + QStringLiteral( "/data" ) );
  QDir().mkpath( root + QStringLiteral( "/data/labs" ) );
  qputenv( "SICNU_DATA_DIR", root.toUtf8() );
  QDir::setCurrent( root );
}

QString writeLab( const QTemporaryDir &dir, const QString &json )
{
  const QString path = dir.filePath( QStringLiteral( "lab98_r26_probe.lab.json" ) );
  QFile file( path );
  if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
    return QString();
  file.write( json.toUtf8() );
  file.close();
  return path;
}

void pumpMs( int ms )
{
  QElapsedTimer t;
  t.start();
  while ( t.elapsed() < ms )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 25 );
    QThread::msleep( 10 );
  }
}

GuidedWorkflowWidget *makeWidgetWithLab( const QString &labJson, QTemporaryDir &dir )
{
  REQUIRE( writeLab( dir, labJson ).isEmpty() == false );
  auto *widget = new GuidedWorkflowWidget( nullptr );
  widget->loadWorkflowsFromDir( dir.path() );
  REQUIRE( widget->workflows().size() == 1 );
  // Selecting the lab row is the user action that arms Start (no
  // auto-selection happens when loading produced no errors).
  auto *list = widget->findChild<QListWidget *>();
  REQUIRE( list != nullptr );
  list->setCurrentRow( 0 );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  return widget;
}

}  // namespace

TEST_CASE( "GuidedWorkflow: a cancelled step also blocks workflowCompleted",
           "[app][widgets][guided][r26-workbench]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // A missing input makes the operator fail; cancelling the in-flight job
  // must mark the step unfinished so "Experiment complete!" stays blocked.
  const QString lab = QStringLiteral( R"( {
    "spec_version": 1,
    "id": "lab98_r26_probe",
    "title": "R26 Probe",
    "title_zh": "探针",
    "objective": "Cancel one step.",
    "steps": [
      { "title": "Compute", "title_zh": "计算", "description_zh": "跑一步。",
        "operator_id": "rs:spectral_index",
        "params": { "input": "data/samples/r26_missing_input_xyz.tif",
                    "output": "outputs/ndvi_r26.tif", "index": "NDVI" } },
      { "title": "Wrap", "title_zh": "收尾", "description_zh": "收尾。" }
    ]
  } )" );

  pinRuntimeDataRoot( dir );
  std::unique_ptr<GuidedWorkflowWidget> widget( makeWidgetWithLab( lab, dir ) );
  QSignalSpy completedSpy( widget.get(), &GuidedWorkflowWidget::workflowCompleted );
  auto *start = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedStartBtn" ) );
  auto *run = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedRunBtn" ) );
  auto *next = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedNextBtn" ) );
  REQUIRE( start != nullptr );
  REQUIRE( run != nullptr );
  REQUIRE( next != nullptr );

  start->click();
  run->click();
  // Wait for the failure callback to land, then navigate to the end.
  auto *browser = widget->findChild<QTextBrowser *>();
  REQUIRE( browser != nullptr );
  QElapsedTimer t;
  t.start();
  while ( browser->toPlainText().isEmpty() && t.elapsed() < 15000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 25 );
    QThread::msleep( 10 );
  }
  pumpMs( 500 );

  // The job is done (failed) at this point; navigating past it must still
  // gate completion — the cancelled/failed step was never re-run.
  REQUIRE( next->isEnabled() );
  next->click();
  pumpMs( 200 );
  REQUIRE( next->isEnabled() );
  next->click();
  pumpMs( 200 );

  CHECK( completedSpy.isEmpty() );
}

TEST_CASE( "GuidedWorkflow: an uncreatable output directory aborts before submitting",
           "[app][widgets][guided][r26-workbench]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Output under /proc can never be created — the operator would just fail
  // again with a less actionable error.
  const QString lab = QStringLiteral( R"( {
    "spec_version": 1,
    "id": "lab98_r26_probe",
    "title": "R26 Probe",
    "title_zh": "探针",
    "objective": "Abort on bad output dir.",
    "steps": [
      { "title": "Compute", "title_zh": "计算", "description_zh": "跑一步。",
        "operator_id": "rs:spectral_index",
        "params": { "input": "data/samples/landsat_sample.tif",
                    "output": "/proc/r26/output/labs/ndvi.tif", "index": "NDVI" } }
    ]
  } )" );

  pinRuntimeDataRoot( dir );
  std::unique_ptr<GuidedWorkflowWidget> widget( makeWidgetWithLab( lab, dir ) );
  auto *start = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedStartBtn" ) );
  auto *run = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedRunBtn" ) );
  auto *browser = widget->findChild<QTextBrowser *>();
  REQUIRE( start != nullptr );
  REQUIRE( run != nullptr );
  REQUIRE( browser != nullptr );

  start->click();
  REQUIRE( run->isEnabled() );
  run->click();
  pumpMs( 3000 );  // long enough for a submitted task to fail, if one was

  const QString transcript = browser->toPlainText();
  CHECK( transcript.contains( QStringLiteral( "Cannot create the output directory" ) ) );
  // The doomed task must not have been submitted on top of the clear error.
  CHECK_FALSE( transcript.contains( QStringLiteral( "failed:" ) ) );
  // The button never entered (or stayed in) the Running state.
  CHECK( run->text() != QStringLiteral( "Running…" ) );
}

TEST_CASE( "GuidedWorkflow: a failed step blocks workflowCompleted",
           "[app][widgets][guided][r26-workbench]" )
{
  ensureApp();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Step 1 references a raster that does not exist: the operator fails.
  const QString lab = QStringLiteral( R"( {
    "spec_version": 1,
    "id": "lab98_r26_probe",
    "title": "R26 Probe",
    "title_zh": "探针",
    "objective": "Fail one step.",
    "steps": [
      { "title": "Compute", "title_zh": "计算", "description_zh": "跑一步。",
        "operator_id": "rs:spectral_index",
        "params": { "input": "data/samples/r26_missing_input_xyz.tif",
                    "output": "outputs/ndvi_r26.tif", "index": "NDVI" } },
      { "title": "Wrap", "title_zh": "收尾", "description_zh": "收尾。" }
    ]
  } )" );

  pinRuntimeDataRoot( dir );
  std::unique_ptr<GuidedWorkflowWidget> widget( makeWidgetWithLab( lab, dir ) );
  QSignalSpy completedSpy( widget.get(), &GuidedWorkflowWidget::workflowCompleted );
  auto *start = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedStartBtn" ) );
  auto *run = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedRunBtn" ) );
  auto *next = widget->findChild<QPushButton *>( QStringLiteral( "rsGuidedNextBtn" ) );
  auto *browser = widget->findChild<QTextBrowser *>();
  REQUIRE( start != nullptr );
  REQUIRE( run != nullptr );
  REQUIRE( next != nullptr );
  REQUIRE( browser != nullptr );

  start->click();
  run->click();
  // Wait for the failure to land (Run re-enables via restoreButton).
  QElapsedTimer t;
  t.start();
  while ( ( !run->isEnabled() || run->text() == QStringLiteral( "Running…" ) )
          && t.elapsed() < 15000 )
  {
    QCoreApplication::processEvents( QEventLoop::AllEvents, 25 );
    QThread::msleep( 10 );
  }
  REQUIRE( browser->toPlainText().contains( QStringLiteral( "failed:" ) ) );

  // Navigate through the last step and try to finish.
  REQUIRE( next->isEnabled() );
  next->click();
  pumpMs( 200 );
  REQUIRE( next->isEnabled() );
  next->click();
  pumpMs( 200 );

  CHECK( completedSpy.isEmpty() );  // a failed experiment is not "complete"
  CHECK( browser->toPlainText().contains( QStringLiteral( "not complete" ) ) );
}
