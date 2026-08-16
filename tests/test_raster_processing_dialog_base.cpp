// test_raster_processing_dialog_base.cpp — TDD for dialog base run lifecycle
#include <catch2/catch_test_macros.hpp>

#include "app/dialogs/raster_processing_dialog_base.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/task_center.h"

#include <QApplication>
#include <QEventLoop>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>
#include <thread>

namespace {

// The QApplication is created in test_raster_processing_dialog_base_main.cpp
// and destroyed at the end of main() (see that file for why).

class TestRasterDialog : public RasterProcessingDialogBase
{
public:
    explicit TestRasterDialog(QWidget *parent = nullptr)
        : RasterProcessingDialogBase(parent)
    {
        auto *layout = new QVBoxLayout(this);
        setupOutputRow(layout);
        setupButtonBar(layout);
    }

    QPushButton *runButton() const { return m_runButton; }
    long pendingTaskId() const { return m_jobHandle.taskId(); }

protected:
    QString toolName() const override { return QStringLiteral("test_dialog"); }
    QString dialogTitle() const override { return QStringLiteral("Test Dialog"); }
    void onRun() override {}
};

} // namespace

TEST_CASE("RasterProcessingDialogBase run lifecycle", "[dialog][base]")
{
    TestRasterDialog dialog;
    REQUIRE(dialog.runButton() != nullptr);
    REQUIRE(dialog.runButton()->isEnabled());
    REQUIRE_FALSE(dialog.isRunning());

    SECTION("startRun disables button and marks running")
    {
        dialog.startRun();
        REQUIRE_FALSE(dialog.runButton()->isEnabled());
        REQUIRE(dialog.isRunning());
    }

    SECTION("finishRun re-enables button and clears running")
    {
        dialog.startRun();
        dialog.finishRun();
        REQUIRE(dialog.runButton()->isEnabled());
        REQUIRE_FALSE(dialog.isRunning());
    }

    SECTION("onCompleted calls finishRun")
    {
        dialog.startRun();
        dialog.onCompleted(QStringLiteral("/tmp/out.tif"));
        REQUIRE(dialog.runButton()->isEnabled());
        REQUIRE_FALSE(dialog.isRunning());
    }
}

TEST_CASE("RasterProcessingDialogBase runGdalTask", "[dialog][base][async]")
{
    TestRasterDialog dialog;

    SECTION("runGdalTask executes task off UI thread and completes")
    {
        bool accepted = false;
        QObject::connect(&dialog, &QDialog::accepted, [&]() { accepted = true; });
        const auto taskCountBefore = sicnu::TaskCenter::instance().allTasks().size();

        dialog.runGdalTask([]() -> QString {
            return QStringLiteral("/tmp/test_out.tif");
        });

        REQUIRE_FALSE(dialog.runButton()->isEnabled());
        REQUIRE(dialog.isRunning());
        REQUIRE(dialog.pendingTaskId() > 0);
        REQUIRE(sicnu::TaskCenter::instance().allTasks().size() == taskCountBefore + 1);

        QEventLoop loop;
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        QObject::connect(&dialog, &QDialog::accepted, &loop, &QEventLoop::quit);
        QObject::connect(&dialog, &QDialog::rejected, &loop, &QEventLoop::quit);
        loop.exec();

        REQUIRE(accepted);
        REQUIRE(dialog.runButton()->isEnabled());
        REQUIRE_FALSE(dialog.isRunning());
    }
}

namespace {

/// Trivial operator returning a fixed result immediately, so the dialog base
/// can be tested end-to-end through the TaskCenter path.
class NoopResultOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "rs:base_noop"; }
    Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext & ) override
    {
        Json::Value result( Json::objectValue );
        result["output"] = "/tmp/base_noop.tif";
        result["stats"] = 42;
        return result;
    }
};

} // namespace

TEST_CASE("RasterProcessingDialogBase runOperatorTask delivers the result JSON", "[dialog][base][async]")
{
    sicnu::operators::RSOperatorRegistry::instance().registerOperator(
        "rs:base_noop", []() { return std::make_unique<NoopResultOperator>(); } );

    TestRasterDialog dialog;

    QTimer modalTimer;
    QObject::connect(&modalTimer, &QTimer::timeout, []() {
        const auto topWidgets = QApplication::topLevelWidgets();
        for (QWidget *w : topWidgets) {
            if (auto *box = qobject_cast<QMessageBox *>(w)) {
                box->accept();
            }
        }
    });
    modalTimer.start(10);

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/in.tif";

    Json::Value received;
    bool gotResult = false;
    dialog.runOperatorTask( "rs:base_noop", params,
                            [&]( const Json::Value &r ) { received = r; gotResult = true; } );

    REQUIRE( dialog.isRunning() );
    REQUIRE( dialog.pendingTaskId() > 0 );

    for (int i = 0; i < 200 && dialog.isRunning(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
    }
    modalTimer.stop();

    REQUIRE( gotResult );
    CHECK( received["output"].asString() == "/tmp/base_noop.tif" );
    CHECK( received["stats"].asInt() == 42 );
    REQUIRE_FALSE( dialog.isRunning() );
}

TEST_CASE("RasterProcessingDialogBase failure path re-enables button and keeps dialog open", "[dialog][base][async]")
{
    TestRasterDialog dialog;

    QTimer modalTimer;
    QObject::connect(&modalTimer, &QTimer::timeout, []() {
        const auto topWidgets = QApplication::topLevelWidgets();
        for (QWidget *w : topWidgets) {
            if (auto *box = qobject_cast<QMessageBox *>(w)) {
                box->accept();
            }
        }
    });
    modalTimer.start(10);

    bool accepted = false;
    QObject::connect(&dialog, &QDialog::accepted, [&]() { accepted = true; });

    dialog.runGdalTask([]() -> QString {
        return RasterProcessingDialogBase::gdalErrorMarker() + QStringLiteral("Mock computation error");
    });

    REQUIRE(dialog.isRunning());

    // Wait until task finishes and dialog is no longer running
    for (int i = 0; i < 200 && dialog.isRunning(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Flush event loop to ensure message boxes are completely closed
    for (int i = 0; i < 10; ++i) {
        QCoreApplication::processEvents();
    }
    modalTimer.stop();

    REQUIRE_FALSE(dialog.isRunning());
    REQUIRE(dialog.runButton()->isEnabled());
    REQUIRE_FALSE(accepted);
}

TEST_CASE("RasterProcessingDialogBase reject is guarded while running", "[dialog][base]")
{
    TestRasterDialog dialog;
    dialog.show();
    dialog.startRun();
    REQUIRE(dialog.isRunning());
    REQUIRE(dialog.isVisible());

    // Calling reject while running must be a no-op (dialog remains visible and running)
    dialog.reject();
    CHECK(dialog.isRunning());
    CHECK(dialog.isVisible());

    dialog.finishRun();
    REQUIRE_FALSE(dialog.isRunning());

    // Calling reject when idle closes the dialog
    dialog.reject();
    CHECK_FALSE(dialog.isVisible());
}
