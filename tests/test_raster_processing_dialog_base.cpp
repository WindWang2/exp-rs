// test_raster_processing_dialog_base.cpp — TDD for dialog base run lifecycle
#include <catch2/catch_test_macros.hpp>

#include "app/dialogs/raster_processing_dialog_base.h"
#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/framework/task_center.h"

#include <QApplication>
#include <QEventLoop>
#include <QLabel>
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

        dialog.runGdalTask([](const RasterProcessingDialogBase::GdalTaskCancelProbe &) -> QString {
            return QStringLiteral("/tmp/test_out.tif");
        });

        REQUIRE_FALSE(dialog.runButton()->isEnabled());
        REQUIRE(dialog.isRunning());
        REQUIRE(dialog.pendingTaskId() > 0);
        REQUIRE(sicnu::TaskCenter::instance().allTasks().size() == taskCountBefore + 1);

        for (int i = 0; i < 500 && (!accepted || dialog.isRunning()); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }

        REQUIRE(accepted);
        REQUIRE(dialog.runButton()->isEnabled());
        REQUIRE_FALSE(dialog.isRunning());
    }

    SECTION("cancel during a long GDAL task aborts it promptly and the dialog recovers (#1560)")
    {
        // Simulated long GDAL I/O: a stepped loop that observes cancellation
        // as soon as the seam exposes it. On the unfixed seam the task lambda
        // is opaque to the JobEngine cancel flag, so it runs every step and
        // the cancel only lands after the fact — steps reaching kTotalSteps
        // proves the abort was NOT prompt. Oracle (#1560): prompt abort +
        // no success surface + UI recovery + a follow-up task still runs.
        constexpr int kTotalSteps = 300;
        std::atomic<int> steps{0};
        bool accepted = false;
        bool resultAnnounced = false;
        QObject::connect(&dialog, &QDialog::accepted, [&]() { accepted = true; });
        QObject::connect(&dialog, &RasterProcessingDialogBase::resultReadyForDisplay,
                         [&]([[maybe_unused]] const QString &path) { resultAnnounced = true; });

        dialog.runGdalTask([&](const RasterProcessingDialogBase::GdalTaskCancelProbe &probe) -> QString {
            // The seam hands the task the live cancel probe (#1560): a real
            // GDAL task polls it from its GDALProgressFunc
            // (gdalCancelProgress); this simulation polls per step. RED on
            // the pre-fix seam ran the same loop with no probe to poll.
            for (int i = 0; i < kTotalSteps; ++i)
            {
                if (probe && probe())
                    return QString();   // prompt abort, mid-flight
                ++steps;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return QString();
        });

        // Wait until the worker is inside the loop.
        for (int i = 0; i < 400 && steps.load() < 3; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(steps.load() >= 3);
        REQUIRE(dialog.isRunning());

        dialog.cancelButton()->click();

        // Drain to the terminal state (cancel path: no modal error box).
        for (int i = 0; i < 600 && dialog.isRunning(); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }

        REQUIRE_FALSE(dialog.isRunning());
        REQUIRE(dialog.runButton()->isEnabled());
        REQUIRE_FALSE(accepted);
        REQUIRE_FALSE(resultAnnounced);
        // Promptness: the task observed the cancel and stopped stepping
        // well before the unabortable full run would have finished.
        REQUIRE(steps.load() < kTotalSteps);

        // Recovery: a follow-up task through the same seam still runs.
        bool accepted2 = false;
        QObject::connect(&dialog, &QDialog::accepted, [&]() { accepted2 = true; });
        dialog.runGdalTask([](const RasterProcessingDialogBase::GdalTaskCancelProbe &) -> QString {
            return QStringLiteral("/tmp/test_out_after_cancel.tif");
        });
        for (int i = 0; i < 500 && (!accepted2 || dialog.isRunning()); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }
        REQUIRE(accepted2);
        REQUIRE_FALSE(dialog.isRunning());
        REQUIRE(dialog.runButton()->isEnabled());
    }

    SECTION("runGdalTask hands the task a live, initially-clear cancel probe (#1560)")
    {
        bool probeReceived = false;
        bool probeFalseAtStart = false;
        bool accepted = false;
        QObject::connect(&dialog, &QDialog::accepted, [&]() { accepted = true; });

        dialog.runGdalTask([&](const RasterProcessingDialogBase::GdalTaskCancelProbe &probe) -> QString {
            if (probe) {
                probeReceived = true;
                probeFalseAtStart = !probe();   // no cancel requested yet
            }
            return QStringLiteral("/tmp/test_probe.tif");
        });

        for (int i = 0; i < 500 && (!accepted || dialog.isRunning()); ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }

        REQUIRE(accepted);
        REQUIRE(probeReceived);
        REQUIRE(probeFalseAtStart);
    }

    SECTION("gdalCancelProgress aborts GDAL (returns 0) exactly when the probe fires (#1560)")
    {
        bool cancelled = false;
        RasterProcessingDialogBase::GdalTaskCancelProbe probe = [&cancelled]() { return cancelled; };

        // Continue while not cancelled, regardless of completion fraction.
        REQUIRE(RasterProcessingDialogBase::gdalCancelProgress(0.0, "msg", &probe) != 0);
        REQUIRE(RasterProcessingDialogBase::gdalCancelProgress(0.5, "msg", &probe) != 0);
        REQUIRE(RasterProcessingDialogBase::gdalCancelProgress(1.0, nullptr, &probe) != 0);
        // Abort (0) as soon as the probe reports cancellation.
        cancelled = true;
        REQUIRE(RasterProcessingDialogBase::gdalCancelProgress(0.5, "msg", &probe) == 0);
        // Defensive: null/absent probe keeps GDAL running.
        REQUIRE(RasterProcessingDialogBase::gdalCancelProgress(0.5, "msg", nullptr) != 0);
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
    class SlowResultOperator : public sicnu::operators::RSOperator
    {
    public:
        std::string name() const override { return "rs:base_slow_noop"; }
        Json::Value run( const Json::Value &, sicnu::operators::RSOperatorContext &ctx ) override
        {
            for (int i = 0; i < 5; ++i) {
                ctx.throwIfCancelled();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            Json::Value result( Json::objectValue );
            result["output"] = "/tmp/base_noop.tif";
            result["stats"] = 42;
            return result;
        }
    };

    sicnu::operators::RSOperatorRegistry::instance().registerOperator(
        "rs:base_slow_noop", []() { return std::make_unique<SlowResultOperator>(); } );

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
    dialog.runOperatorTask( "rs:base_slow_noop", params,
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

    dialog.runGdalTask([](const RasterProcessingDialogBase::GdalTaskCancelProbe &) -> QString {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
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

TEST_CASE("RasterProcessingDialogBase UI components and button box", "[dialog][base][ui]")
{
    TestRasterDialog dialog;

    SECTION("Button box and buttons initialization")
    {
        REQUIRE(dialog.buttonBox() != nullptr);
        REQUIRE(dialog.runButton() != nullptr);
        REQUIRE(dialog.cancelButton() != nullptr);
        REQUIRE(dialog.resetButton() != nullptr);
        REQUIRE(dialog.helpButton() != nullptr);

        CHECK(dialog.buttonBox()->buttonRole(dialog.runButton()) == QDialogButtonBox::AcceptRole);
        CHECK(dialog.buttonBox()->buttonRole(dialog.cancelButton()) == QDialogButtonBox::RejectRole);
        CHECK(dialog.buttonBox()->buttonRole(dialog.resetButton()) == QDialogButtonBox::ResetRole);
        CHECK(dialog.buttonBox()->buttonRole(dialog.helpButton()) == QDialogButtonBox::HelpRole);

        CHECK(dialog.runButton()->text() == QStringLiteral("Run"));
        CHECK(dialog.cancelButton()->text() == QStringLiteral("Cancel"));
        CHECK(dialog.resetButton()->text() == QStringLiteral("Reset"));
        CHECK(dialog.helpButton()->text() == QStringLiteral("Help"));
    }

    SECTION("minimumSizeHint and sizeHint dynamic dimensions")
    {
        QSize minSize = dialog.minimumSizeHint();
        CHECK(minSize.width() >= 520);
        CHECK(minSize.height() >= 420);

        QSize preferredSize = dialog.sizeHint();
        CHECK(preferredSize.width() >= minSize.width());
        CHECK(preferredSize.height() >= minSize.height());
    }

    SECTION("auto-accept on success property")
    {
        CHECK(dialog.shouldAutoAcceptOnSuccess());
        dialog.setShouldAutoAcceptOnSuccess(false);
        CHECK_FALSE(dialog.shouldAutoAcceptOnSuccess());
        dialog.setShouldAutoAcceptOnSuccess(true);
        CHECK(dialog.shouldAutoAcceptOnSuccess());
    }
}

class TestSubclassedRasterDialog : public RasterProcessingDialogBase
{
public:
    QGroupBox *inputGroup = nullptr;
    QGroupBox *paramGroup = nullptr;
    QGroupBox *advGroup = nullptr;
    QGroupBox *outGroup = nullptr;
    int runCount = 0;
    bool resetHookCalled = false;
    bool helpHookCalled = false;

    explicit TestSubclassedRasterDialog(QWidget *parent = nullptr)
        : RasterProcessingDialogBase(parent)
    {
        auto *layout = new QVBoxLayout(this);
        inputGroup = setupInputGroup(layout, QStringLiteral("自定义输入"));
        paramGroup = setupParamGroup(layout);
        advGroup = setupAdvancedGroup(layout);
        outGroup = setupOutputGroup(layout);
        setupButtonBar(layout);
    }

    void triggerReset() { onResetClicked(); }
    void triggerHelp() { onHelpClicked(); }
    void triggerRun() { onRunClicked(); }
    void setValidOutputPath(const QString &p) {
        if (m_outputEdit) m_outputEdit->setText(p);
    }

    QGroupBox *callSetupInputGroup(QVBoxLayout *l, const QString &t = QString()) { return setupInputGroup(l, t); }
    QGroupBox *callSetupParamGroup(QVBoxLayout *l, const QString &t = QString()) { return setupParamGroup(l, t); }
    QGroupBox *callSetupAdvancedGroup(QVBoxLayout *l, const QString &t = QString()) { return setupAdvancedGroup(l, t); }
    QGroupBox *callSetupOutputGroup(QVBoxLayout *l, const QString &t = QString()) { return setupOutputGroup(l, t); }
    void callSetupButtonBar(QVBoxLayout *l) { setupButtonBar(l); }
    void callSetupHelpBanner(QVBoxLayout *l) { setupHelpBanner(l); }

protected:
    QString toolName() const override { return QStringLiteral("test_subclass_dialog"); }
    QString dialogTitle() const override { return QStringLiteral("Test Subclass Dialog"); }
    bool validateInputs() override { return true; }
    void onRun() override { runCount++; }

    void onResetClicked() override
    {
        RasterProcessingDialogBase::onResetClicked();
        resetHookCalled = true;
    }

    void onHelpClicked() override
    {
        helpHookCalled = true;
    }
};

TEST_CASE("RasterProcessingDialogBase group builders and custom hooks", "[dialog][base][ui]")
{
    TestSubclassedRasterDialog dialog;

    SECTION("Group containers created with standard styling and titles")
    {
        REQUIRE(dialog.inputGroup != nullptr);
        CHECK(dialog.inputGroup->title() == QStringLiteral("自定义输入"));

        REQUIRE(dialog.paramGroup != nullptr);
        CHECK(dialog.paramGroup->title() == QStringLiteral("Algorithm Parameters"));

        REQUIRE(dialog.advGroup != nullptr);
        CHECK(dialog.advGroup->title() == QStringLiteral("Advanced Options"));

        REQUIRE(dialog.outGroup != nullptr);
        CHECK(dialog.outGroup->title() == QStringLiteral("Output Settings"));
    }

    SECTION("Reset button clears output edit and invokes onResetClicked hook")
    {
        dialog.setValidOutputPath(QStringLiteral("/tmp/custom_out.tif"));
        CHECK(dialog.outputPath() == QStringLiteral("/tmp/custom_out.tif"));
        dialog.resetButton()->click();
        CHECK(dialog.resetHookCalled);
        CHECK(dialog.outputPath().isEmpty());
    }

    SECTION("Help button invokes onHelpClicked hook")
    {
        dialog.helpButton()->click();
        CHECK(dialog.helpHookCalled);
    }
}

TEST_CASE("RasterProcessingDialogBase adversarial signal and click stress testing", "[dialog][base][adversarial]")
{
    TestSubclassedRasterDialog dialog;

    SECTION("Single Run click invokes onRun exactly once")
    {
        CHECK(dialog.runCount == 0);
        dialog.runButton()->click();
        CHECK(dialog.runCount == 1);
    }

    SECTION("Multi-clicking Run while running does not double-trigger onRun")
    {
        CHECK(dialog.runCount == 0);
        dialog.startRun();
        REQUIRE(dialog.isRunning());

        // Simulate rapid repeated clicks or Enter presses
        dialog.runButton()->click();
        dialog.triggerRun();
        dialog.triggerRun();
        CHECK(dialog.runCount == 0); // Guarded by isRunning()

        dialog.finishRun();
        REQUIRE_FALSE(dialog.isRunning());
        dialog.runButton()->click();
        CHECK(dialog.runCount == 1);
    }

    SECTION("Cancel button while running invokes cancel but does not close dialog")
    {
        dialog.show();
        dialog.startRun();
        REQUIRE(dialog.isRunning());
        REQUIRE(dialog.isVisible());

        // Clicking Cancel while running should not dismiss
        dialog.cancelButton()->click();
        CHECK(dialog.isRunning());
        CHECK(dialog.isVisible());

        dialog.finishRun();
        // Clicking Cancel while idle dismisses
        dialog.cancelButton()->click();
        CHECK_FALSE(dialog.isVisible());
    }

    SECTION("Reset and Help clicks while running are safe")
    {
        dialog.startRun();
        REQUIRE(dialog.isRunning());
        REQUIRE_FALSE(dialog.resetButton()->isEnabled());

        // Even if triggered programmatically while running
        dialog.triggerReset();
        CHECK(dialog.resetHookCalled);

        dialog.triggerHelp();
        CHECK(dialog.helpHookCalled);

        dialog.finishRun();
    }
}

TEST_CASE("RasterProcessingDialogBase DPI scaling and layout expansion stress testing", "[dialog][base][dpi]")
{
    TestSubclassedRasterDialog dialog;

    SECTION("Base minimum size satisfies minimum bounds")
    {
        QSize minSize = dialog.minimumSizeHint();
        CHECK(minSize.width() >= 520);
        CHECK(minSize.height() >= 420);
    }

    SECTION("Large High-DPI font scales minimumSizeHint proportionally")
    {
        QFont largeFont = dialog.font();
        largeFont.setPointSize(24);
        dialog.setFont(largeFont);

        const int charWidth = dialog.fontMetrics().horizontalAdvance(QLatin1Char('M'));
        const int lineHeight = dialog.fontMetrics().lineSpacing();
        const int expectedMinW = std::max(520, charWidth * 38);
        const int expectedMinH = std::max(420, lineHeight * 16);

        QSize scaledMinSize = dialog.minimumSizeHint();
        CHECK(scaledMinSize.width() >= expectedMinW);
        CHECK(scaledMinSize.height() >= expectedMinH);

        QSize scaledSizeHint = dialog.sizeHint();
        CHECK(scaledSizeHint.width() >= scaledMinSize.width());
        CHECK(scaledSizeHint.height() >= scaledMinSize.height());
    }

    SECTION("Heavy content expansion preserves layout height requirement")
    {
        // Add 30 labels to layout to simulate a tall dialog
        for (int i = 0; i < 30; ++i) {
            dialog.layout()->addWidget(new QLabel(QStringLiteral("Row label %1").arg(i), &dialog));
        }

        QSize expandedMinSize = dialog.minimumSizeHint();
        // The layout requires significantly more than 420px height
        CHECK(expandedMinSize.height() > 420);
        CHECK(dialog.sizeHint().height() >= expandedMinSize.height());
    }
}

TEST_CASE("RasterProcessingDialogBase destructor safety during active jobs", "[dialog][base][safety]")
{
    auto *dialog = new TestRasterDialog();
    dialog->runGdalTask([](const RasterProcessingDialogBase::GdalTaskCancelProbe &) -> QString {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return QStringLiteral("/tmp/destroyed_out.tif");
    });
    REQUIRE(dialog->isRunning());

    // Deleting the dialog while job is active should cancel safely without crashing
    delete dialog;

    // Process events to verify no dangling pointers trigger callbacks
    for (int i = 0; i < 15; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    SUCCEED("Destruction during running task completed cleanly without crash");
}

TEST_CASE("RasterProcessingDialogBase null safety in helper functions", "[dialog][base][nullsafety]")
{
    TestSubclassedRasterDialog dialog;
    CHECK(dialog.callSetupInputGroup(nullptr) == nullptr);
    CHECK(dialog.callSetupParamGroup(nullptr) == nullptr);
    CHECK(dialog.callSetupAdvancedGroup(nullptr) == nullptr);
    CHECK(dialog.callSetupOutputGroup(nullptr) == nullptr);
    dialog.callSetupButtonBar(nullptr);
    dialog.callSetupHelpBanner(nullptr);
    SUCCEED("Null layout checks handled gracefully");
}



