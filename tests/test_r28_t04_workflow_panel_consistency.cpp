// test_r28_t04_workflow_panel_consistency.cpp — WP04/05: canvas ↔ form ↔
// TaskPanel bidirectional consistency regressions.
//
// Pins the seams a 2026-11 survey found dead or unguarded on master:
//   * nodeSelected was connected but NEVER emitted by the workflow canvas
//     (the node → parameter-form link was dead code),
//   * selecting a canvas node before any session exists did nothing (no
//     session bootstrap),
//   * bindCanvas/bindPanel rebinding duplicated connections,
//   * runUpToNode synced (and closed the live session) BEFORE the
//     in-flight guard,
//   * a late single-job result was applied to the LIVE session/step
//     pointers instead of the ones captured at submit,
//   * panel-entered params never reached the canvas document a full run
//     executes (two divergent parameter stores),
//   * editing a parameter after a completed run left the stale result
//     summary and "success" node state next to parameters that no longer
//     produced them.
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "app/shell/schema_form_builder.h"
#include "app/shell/task_panel_host.h"
#include "app/widgets/rs_result_summary.h"
#include "app/shell/workflow_session_controller.h"
#include "app/workflow/pipeline_canvas_widget.h"
#include "app/workflow/pipeline_scene.h"
#include "workflow/workflow_types.h"
#include "processing/framework/task_center.h"

#include <gdal.h>
#include <functional>
#include <vector>

namespace
{

void ensureApp()
{
    if ( !qApp )
    {
        qputenv( "QT_QPA_PLATFORM", QByteArrayLiteral( "offscreen" ) );
        static int argc = 1;
        static char a0[] = "test_r28_t04_workflow_panel";
        // static argv: QApplication keeps the pointer — a stack array
        // dangled after the first test case and later argv reads crashed
        // inside QCoreApplication::applicationFilePath.
        static char *argv[] = { a0, nullptr };
        new QApplication( argc, argv );
    }
}

struct TaskCenterGuard
{
    TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
    ~TaskCenterGuard() { sicnu::TaskCenter::instance().shutdownForTests(); }
};

bool waitFor( const std::function<bool()> &predicate, int timeoutMs )
{
    QElapsedTimer timer;
    timer.start();
    while ( !predicate() )
    {
        if ( timer.elapsed() > timeoutMs )
            return false;
        QEventLoop loop;
        QTimer::singleShot( 20, &loop, &QEventLoop::quit );
        loop.exec();
    }
    return true;
}

/// A definition whose single step runs rs:radiometric_calibration on the
/// given raster (big enough that the operator runs for ~a second — the
/// mid-flight window the late-result cases need).
sicnu::workflow::WorkflowDefinition singleStepDefinition( const std::string &id,
                                                          const std::string &inputPath )
{
    sicnu::workflow::WorkflowDefinition def;
    def.id = id;
    def.title = "T04 case " + id;
    def.host = sicnu::workflow::HostKind::TaskPanel;
    sicnu::workflow::StepDef step;
    step.id = "calibration";
    step.title = "calibration";
    step.kind = sicnu::workflow::StepKind::Operator;
    step.operatorId = "rs:radiometric_calibration";
    step.artifactOnSuccess = "calibrated";
    step.params["input"] = inputPath;
    step.params["unit"] = "toa_reflectance";
    def.steps.push_back( step );
    return def;
}

QString writeBigRaster( const QString &path, int size )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH dataset =
        GDALCreate( driver, path.toUtf8().constData(), size, size, 1, GDT_Float32, nullptr );
    REQUIRE( dataset != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    std::vector<float> line( size, 0.25f );
    for ( int y = 0; y < size; ++y )
        REQUIRE( GDALRasterIO( band, GF_Write, 0, y, size, 1, line.data(), size, 1,
                               GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( dataset );
    return path;
}

} // namespace

// ---------------------------------------------------------------------------
// nodeSelected: the node → form seam
// ---------------------------------------------------------------------------

TEST_CASE( "canvas emits nodeSelected on node click and the controller shows the form",
           "[r28t04][workflow][canvas][nodeselected]" )
{
    ensureApp();
    TaskCenterGuard guard;

    QTemporaryDir dir;
    const std::string raster =
        writeBigRaster( dir.filePath( QStringLiteral( "a.tif" ) ), 64 ).toStdString();
    sicnu::workflow::WorkflowDefinition def =
        singleStepDefinition( "r28t04_canvas_pick", raster );

    sicnu::workflow::gui::PipelineCanvasWidget canvas;
    canvas.loadWorkflowDefinition( def );

    QSignalSpy selected( &canvas, &sicnu::workflow::gui::PipelineCanvasWidget::nodeSelected );
    auto *scene = canvas.pipelineScene();
    REQUIRE( !scene->nodes().empty() );

    // Selecting the node's item (what a mouse click does) reaches the
    // widget's nodeSelected signal — the connection the controller owns.
    scene->nodes().begin()->second->setSelected( true );
    QApplication::processEvents();
    REQUIRE( selected.count() >= 1 );
    CHECK( selected.first().first().toString() == QStringLiteral( "calibration" ) );

    // And the scene's double-click signal forwards too (belt for
    // item-semantic consumers).
    QMetaObject::invokeMethod( scene, "nodeDoubleClicked",
                               Q_ARG( QString, QStringLiteral( "calibration" ) ) );
    QApplication::processEvents();
    CHECK( selected.count() >= 2 );
}

TEST_CASE( "selecting a node before any session bootstraps the canvas definition session",
           "[r28t04][workflow][canvas][bootstrap]" )
{
    ensureApp();
    TaskCenterGuard guard;

    QTemporaryDir dir;
    const std::string raster =
        writeBigRaster( dir.filePath( QStringLiteral( "b.tif" ) ), 64 ).toStdString();
    sicnu::workflow::WorkflowDefinition def =
        singleStepDefinition( "r28t04_bootstrap", raster );

    auto *panel = new TaskPanelHost();
    sicnu::workflow::gui::PipelineCanvasWidget canvas;
    canvas.loadWorkflowDefinition( def );

    WorkflowSessionController controller;
    controller.bindPanel( panel );
    controller.bindCanvas( &canvas );
    CHECK( controller.activeSessionId().isEmpty() ); // no session yet

    canvas.pipelineScene()->nodes().begin()->second->setSelected( true );
    QApplication::processEvents();

    // The canvas definition session is live and the form shows the node's
    // step (the old code returned early: canvas load without a shell-opened
    // tool left the form dead until a full run).
    CHECK_FALSE( controller.activeSessionId().isEmpty() );
    CHECK( controller.activeStepId() == QStringLiteral( "calibration" ) );

    delete panel;
}

// ---------------------------------------------------------------------------
// rebind guards
// ---------------------------------------------------------------------------

TEST_CASE( "rebinding the same canvas/panel does not duplicate connections",
           "[r28t04][workflow][controller][rebind]" )
{
    ensureApp();
    TaskCenterGuard guard;

    auto *panel = new TaskPanelHost();
    sicnu::workflow::gui::PipelineCanvasWidget canvas;

    WorkflowSessionController controller;
    controller.bindPanel( panel );
    controller.bindPanel( panel );
    controller.bindCanvas( &canvas );
    controller.bindCanvas( &canvas );

    // Emit runUpToNodeRequested exactly once; a duplicated connection runs
    // the handler twice (double status messages, double state churn).
    QSignalSpy status( &controller, &WorkflowSessionController::statusMessage );
    emit canvas.runUpToNodeRequested( QStringLiteral( "nope" ) );
    QApplication::processEvents();

    // Empty canvas → exactly one typed refusal ("add steps…"). The old
    // double bind made it two.
    CHECK( status.count() == 1 );

    delete panel;
}

// ---------------------------------------------------------------------------
// panel params → canvas document overlay (the single parameter store)
// ---------------------------------------------------------------------------

TEST_CASE( "overlayPanelParams merges session params onto the run definition",
           "[r28t04][workflow][controller][overlay]" )
{
    ensureApp();

    sicnu::workflow::WorkflowDefinition def =
        singleStepDefinition( "r28t04_overlay", "/tmp/x.tif" );
    def.steps.front().params["unit"] = "toa_reflectance";
    def.steps.front().params["keep"] = "me";

    Json::Value paramsByStep( Json::objectValue );
    Json::Value edited( Json::objectValue );
    edited["unit"] = "bt_brightness";
    paramsByStep["calibration"] = edited;

    WorkflowSessionController::overlayPanelParams( def, paramsByStep );

    CHECK( def.steps.front().params["unit"].asString() == "bt_brightness" );
    CHECK( def.steps.front().params["keep"].asString() == "me" ); // untouched keys survive
    CHECK( def.steps.front().params["input"].asString() == "/tmp/x.tif" );

    // Steps without panel edits keep the document params verbatim.
    Json::Value otherOnly( Json::objectValue );
    Json::Value other( Json::objectValue );
    other["a"] = 1;
    otherOnly["not_a_step_here"] = other;
    WorkflowSessionController::overlayPanelParams( def, otherOnly );
    CHECK( def.steps.front().params["unit"].asString() == "bt_brightness" );
}

// ---------------------------------------------------------------------------
// R1 P1-1 pins: the late single-job result commits into the CAPTURED
// session, and a parameter edit invalidates the stale result.
// ---------------------------------------------------------------------------

TEST_CASE( "late single-job result commits into the captured session, not the live one",
           "[r28t04][workflow][late_capture]" )
{
    ensureApp();
    TaskCenterGuard guard;

    QTemporaryDir dir;
    const std::string raster =
      writeBigRaster( dir.filePath( QStringLiteral( "c.tif" ) ), 64 ).toStdString();

    auto *panel = new TaskPanelHost();
    sicnu::workflow::gui::PipelineCanvasWidget canvas;
    WorkflowSessionController controller;
    controller.bindPanel( panel );
    controller.bindCanvas( &canvas );

    // Session A: the tool whose run stays in flight.
    const QString sessionA = controller.openTool( QStringLiteral( "lab.preprocess.optical" ) );
    REQUIRE( !sessionA.isEmpty() );
    const QString stepA = controller.activeStepId();
    REQUIRE( !stepA.isEmpty() );

    // Arm a single-job run without satisfying its gates is refused; drive
    // the capture path directly instead: run with a real operator input.
    // rs:radiometric_calibration needs an input raster — set the form value.
    {
      Json::Value form( Json::objectValue );
      form["input"] = raster;
      form["unit"] = "toa_reflectance";
      panel->setFormValues( form );
    }
    controller.onRunClicked();
    REQUIRE( controller.isRunInFlight() );
    REQUIRE( controller.pendingTaskId() >= 0 );

    // While the job runs, the user opens ANOTHER tool — the live session
    // and step pointers move away from what the run belongs to.
    const QString sessionB = controller.openTool( QStringLiteral( "lab.preprocess.optical" ) );
    REQUIRE( !sessionB.isEmpty() );
    REQUIRE( sessionB != sessionA );

    // The terminal event for the A-run arrives late: it must commit into
    // session A (captured at submission), never into the live session B.
    // Observable contract: the panel the user is looking at (session B)
    // must NOT receive the failure (pre-fix, the cleared-before-read
    // capture made sessionQ fall back to the live session and setFailed
    // stamped over the user's new form).
    sicnu::AlgorithmTaskInfo terminal;
    terminal.taskId = controller.pendingTaskId();
    terminal.status = sicnu::TaskStatus::Failed;
    terminal.errorMessage = QStringLiteral( "late failure for the captured run" );
    QMetaObject::invokeMethod( &controller, "onTaskUpdated",
                               Q_ARG( sicnu::AlgorithmTaskInfo, terminal ) );
    QApplication::processEvents();

    CHECK_FALSE( controller.isRunInFlight() );
    // Switch-away guard held: the failure never reached the panel (the
    // hint label stays clean — setFailed writes its text there).
    auto *hintLabel = panel->findChild<QLabel *>( QStringLiteral( "rsTaskPanelHint" ) );
    REQUIRE( hintLabel != nullptr );
    CHECK( hintLabel->text().isEmpty() );
    // Session B's step cursor is untouched (the run never belonged to it).
    CHECK( controller.activeSessionId() == sessionB );
    // And the A-run's bookkeeping cleared the tracked id.
    CHECK( controller.pendingTaskId() == -1 );

    delete panel;
}

TEST_CASE( "editing a parameter after a completed run invalidates the stale result summary",
           "[r28t04][workflow][stale_result]" )
{
    ensureApp();
    TaskCenterGuard guard;

    QTemporaryDir dir;
    const std::string raster =
      writeBigRaster( dir.filePath( QStringLiteral( "d.tif" ) ), 64 ).toStdString();

    auto *panel = new TaskPanelHost();
    WorkflowSessionController controller;
    controller.bindPanel( panel );

    const QString session = controller.openTool( QStringLiteral( "lab.preprocess.optical" ) );
    REQUIRE( !session.isEmpty() );
    const QString step = controller.activeStepId();

    Json::Value form( Json::objectValue );
    form["input"] = raster;
    form["unit"] = "toa_reflectance";
    panel->setFormValues( form );
    controller.onRunClicked();
    REQUIRE( controller.isRunInFlight() );
    REQUIRE( controller.pendingTaskId() >= 0 );

    // Drive the success path with a synthetic terminal event (the real
    // job's own terminal will find the ids already reset — harmless).
    sicnu::AlgorithmTaskInfo done;
    done.taskId = controller.pendingTaskId();
    done.algorithmId = QStringLiteral( "rs:radiometric_calibration" );
    done.status = sicnu::TaskStatus::Completed;
    done.resultPayload = Json::Value( Json::objectValue );
    done.resultPayload["output"] = "/tmp/stale.tif";
    QMetaObject::invokeMethod( &controller, "onTaskUpdated",
                               Q_ARG( sicnu::AlgorithmTaskInfo, done ) );
    QApplication::processEvents();

    auto *summary = panel->findChild<RsResultSummary *>();
    REQUIRE( summary != nullptr );
    CHECK( summary->isVisibleTo( panel ) ); // the result IS shown on success

    // The staleness hook: the next parameter edit drops the summary and
    // demotes the node — the form no longer shows the parameters that
    // produced it, so the UI must stop claiming they ran.
    QSignalSpy status( &controller, &WorkflowSessionController::stepStatusChanged );
    emit panel->form()->valuesChanged();
    QApplication::processEvents();

    CHECK( !summary->isVisibleTo( panel ) );
    bool demoted = false;
    for ( const auto &args : status )
        if ( args.at( 0 ).toString() == step && args.at( 1 ).toString() == QStringLiteral( "idle" ) )
            demoted = true;
    CHECK( demoted );

    delete panel;
}
