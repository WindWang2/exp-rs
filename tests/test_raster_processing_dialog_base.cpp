// test_raster_processing_dialog_base.cpp — TDD for dialog base run lifecycle
#include <catch2/catch_test_macros.hpp>

#include "app/dialogs/raster_processing_dialog_base.h"
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
