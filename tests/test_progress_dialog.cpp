// Progress Dialog tests — verify progress tracking and cancel functionality
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTimer>
#include <app/widgets/progress_dialog.h>

// Helper to ensure single QApplication instance
static QApplication *ensureApp()
{
    if (!qApp) {
        static int argc = 1;
        static char appName[] = "test_runner";
        static char *argv[] = { appName, nullptr };
        new QApplication(argc, argv);
    }
    return static_cast<QApplication*>(qApp);
}


TEST_CASE("ProgressDialog creation", "[gui][progress]") {
    ensureApp();

    SECTION("Creates with default parameters") {
        ProgressDialog dialog;
        CHECK(dialog.windowTitle() == "Processing...");
        CHECK(dialog.minimum() == 0);
        CHECK(dialog.maximum() == 100);
        CHECK(dialog.value() == 0);
        CHECK(dialog.isCancelled() == false);
    }

    SECTION("Creates with custom title") {
        ProgressDialog dialog("Running GDAL...");
        CHECK(dialog.windowTitle() == "Running GDAL...");
    }
}

TEST_CASE("ProgressDialog setValue", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Updates progress value") {
        dialog.setValue(50);
        CHECK(dialog.value() == 50);
    }

    SECTION("Clamps to minimum") {
        dialog.setValue(-10);
        CHECK(dialog.value() == 0);
    }

    SECTION("Clamps to maximum") {
        dialog.setValue(150);
        CHECK(dialog.value() == 100);
    }
}

TEST_CASE("ProgressDialog setRange", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Updates range") {
        dialog.setRange(0, 500);
        CHECK(dialog.minimum() == 0);
        CHECK(dialog.maximum() == 500);
    }
}

TEST_CASE("ProgressDialog cancel", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Initially not cancelled") {
        CHECK(dialog.isCancelled() == false);
    }

    SECTION("Cancel sets flag") {
        dialog.cancel();
        CHECK(dialog.isCancelled() == true);
    }

    SECTION("Reset allows reuse") {
        dialog.cancel();
        dialog.reset();
        CHECK(dialog.isCancelled() == false);
        CHECK(dialog.value() == 0);
    }
}

TEST_CASE("ProgressDialog setLabelText", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Updates label text") {
        dialog.setLabelText("Processing band 3 of 7...");
        CHECK(dialog.labelText() == "Processing band 3 of 7...");
    }
}

TEST_CASE("ProgressDialog elapsed time", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Elapsed time starts at zero") {
        CHECK(dialog.elapsedMs() >= 0);
        CHECK(dialog.elapsedMs() < 100); // Should be very small
    }
}

TEST_CASE("ProgressDialog auto-close on 100%", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;

    SECTION("Auto-close enabled by default") {
        CHECK(dialog.autoClose() == true);
    }

    SECTION("Can disable auto-close") {
        dialog.setAutoClose(false);
        CHECK(dialog.autoClose() == false);
    }
}

TEST_CASE("ProgressDialog cancel signal", "[gui][progress]") {
    ensureApp();

    ProgressDialog dialog;
    bool signalReceived = false;
    QObject::connect(&dialog, &ProgressDialog::cancelled, [&]() {
        signalReceived = true;
    });

    SECTION("Emits cancelled signal on cancel") {
        dialog.cancel();
        CHECK(signalReceived == true);
    }
}
