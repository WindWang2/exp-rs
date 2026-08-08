// test_raster_processing_dialog_base.cpp — TDD for dialog base run lifecycle
#include <catch2/catch_test_macros.hpp>

#include "app/dialogs/raster_processing_dialog_base.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/task_center.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QPushButton>
#include <QEventLoop>
#include <QTimer>

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

    Json::Value params( Json::objectValue );
    params["input"] = "/tmp/in.tif";

    Json::Value received;
    bool gotResult = false;
    dialog.runOperatorTask( "rs:base_noop", params,
                            [&]( const Json::Value &r ) { received = r; gotResult = true; } );

    REQUIRE( dialog.isRunning() );

    QEventLoop loop;
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    QObject::connect( &dialog, &QDialog::accepted, &loop, &QEventLoop::quit );
    QObject::connect( &dialog, &QDialog::rejected, &loop, &QEventLoop::quit );
    loop.exec();

    REQUIRE( gotResult );
    CHECK( received["output"].asString() == "/tmp/base_noop.tif" );
    CHECK( received["stats"].asInt() == 42 );
    REQUIRE_FALSE( dialog.isRunning() );
}
