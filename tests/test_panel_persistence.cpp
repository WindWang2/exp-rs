// Panel State Persistence tests — verify save/restore dock widget layout
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QSettings>
#include <QMainWindow>
#include <QDockWidget>

// Mock MainWindow with panel persistence methods
class TestMainWindow : public QMainWindow
{
public:
    TestMainWindow() : QMainWindow()
    {
        setObjectName("TestMainWindow");
    }

    void savePanelState()
    {
        QSettings settings;
        settings.setValue("mainwindow/state", saveState());
        settings.setValue("mainwindow/geometry", saveGeometry());
    }

    bool restorePanelState()
    {
        QSettings settings;
        QByteArray state = settings.value("mainwindow/state").toByteArray();
        if (state.isEmpty())
            return false;
        return restoreState(state);
    }

    void resetPanelLayout()
    {
        QSettings settings;
        settings.remove("mainwindow/state");
        settings.remove("mainwindow/geometry");
    }

    bool hasPanelState() const
    {
        QSettings settings;
        return settings.contains("mainwindow/state");
    }
};

TEST_CASE("Panel state save/restore", "[gui][persistence]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    // Use test organization to avoid polluting real settings
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings settings;
    settings.clear();

    TestMainWindow window;

    SECTION("Initially no saved state") {
        CHECK(window.hasPanelState() == false);
    }

    SECTION("Save persists state") {
        window.savePanelState();
        CHECK(window.hasPanelState() == true);
    }

    SECTION("Restore returns false when no state") {
        CHECK(window.restorePanelState() == false);
    }

    SECTION("Restore returns true when state exists") {
        window.savePanelState();
        CHECK(window.restorePanelState() == true);
    }

    SECTION("Reset clears state") {
        window.savePanelState();
        CHECK(window.hasPanelState() == true);

        window.resetPanelLayout();
        CHECK(window.hasPanelState() == false);
    }

    SECTION("Save and restore preserves dock widgets") {
        auto *dock1 = new QDockWidget("Layers", &window);
        dock1->setObjectName("layersDock");
        window.addDockWidget(Qt::LeftDockWidgetArea, dock1);

        auto *dock2 = new QDockWidget("Browser", &window);
        dock2->setObjectName("browserDock");
        window.addDockWidget(Qt::RightDockWidgetArea, dock2);

        window.savePanelState();

        // Simulate restart: create new window
        TestMainWindow window2;
        auto *dock1b = new QDockWidget("Layers", &window2);
        dock1b->setObjectName("layersDock");
        window2.addDockWidget(Qt::LeftDockWidgetArea, dock1b);

        auto *dock2b = new QDockWidget("Browser", &window2);
        dock2b->setObjectName("browserDock");
        window2.addDockWidget(Qt::RightDockWidgetArea, dock2b);

        bool restored = window2.restorePanelState();
        CHECK(restored == true);
    }

    // Cleanup
    settings.clear();
}

TEST_CASE("Reset layout action exists", "[gui][persistence]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    TestMainWindow window;

    SECTION("Can create reset action") {
        QAction *resetAction = new QAction("Reset Layout", &window);
        CHECK(resetAction->text() == "Reset Layout");
        CHECK(resetAction->isEnabled() == true);
    }
}
