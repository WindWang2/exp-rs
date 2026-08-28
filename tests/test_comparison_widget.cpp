#include <catch2/catch_test_macros.hpp>
#include <QThread>
#include <catch2/catch_approx.hpp>
#include "app/widgets/comparison_widget.h"
#include "app/dialogs/comparison_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QApplication>
#include <QPixmap>
#include <QTemporaryDir>

#include <array>
#include <vector>

#include <qgsapplication.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

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

// Helper to create a test QPixmap with a specific color
static QPixmap createTestPixmap(int width, int height, const QColor &color)
{
    QPixmap pixmap(width, height);
    pixmap.fill(color);
    return pixmap;
}

TEST_CASE("ComparisonWidget initialization", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;
    REQUIRE(widget.mode() == ComparisonWidget::ComparisonMode::SplitScreen);
    REQUIRE(widget.flickerInterval() == 500);
}

TEST_CASE("ComparisonWidget set/get mode", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    widget.setMode(ComparisonWidget::ComparisonMode::Flicker);
    REQUIRE(widget.mode() == ComparisonWidget::ComparisonMode::Flicker);

    widget.setMode(ComparisonWidget::ComparisonMode::SplitScreen);
    REQUIRE(widget.mode() == ComparisonWidget::ComparisonMode::SplitScreen);
}

TEST_CASE("ComparisonWidget flicker interval", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    widget.setFlickerInterval(1000);
    REQUIRE(widget.flickerInterval() == 1000);

    widget.setFlickerInterval(250);
    REQUIRE(widget.flickerInterval() == 250);
}

TEST_CASE("ComparisonWidget mode change signal", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    bool signalEmitted = false;
    ComparisonWidget::ComparisonMode receivedMode = ComparisonWidget::ComparisonMode::SplitScreen;

    QObject::connect(&widget, &ComparisonWidget::modeChanged,
                     [&](ComparisonWidget::ComparisonMode mode) {
                         signalEmitted = true;
                         receivedMode = mode;
                     });

    widget.setMode(ComparisonWidget::ComparisonMode::Flicker);
    REQUIRE(signalEmitted);
    REQUIRE(receivedMode == ComparisonWidget::ComparisonMode::Flicker);
}

TEST_CASE("ComparisonWidget flicker interval signal", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    bool signalEmitted = false;
    int receivedInterval = 0;

    QObject::connect(&widget, &ComparisonWidget::flickerIntervalChanged,
                     [&](int interval) {
                         signalEmitted = true;
                         receivedInterval = interval;
                     });

    widget.setFlickerInterval(750);
    REQUIRE(signalEmitted);
    REQUIRE(receivedInterval == 750);
}

TEST_CASE("ComparisonWidget set images", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    QPixmap left = createTestPixmap(100, 100, Qt::red);
    QPixmap right = createTestPixmap(100, 100, Qt::blue);

    widget.setLeftImage(left);
    widget.setRightImage(right);

    REQUIRE(widget.hasLeftImage());
    REQUIRE(widget.hasRightImage());
}

TEST_CASE("ComparisonWidget same mode no signal", "[comparison]") {
    ensureApp();

    ComparisonWidget widget;

    bool signalEmitted = false;
    QObject::connect(&widget, &ComparisonWidget::modeChanged,
                     [&]() { signalEmitted = true; });

    // Already in SplitScreen mode, setting again should not emit
    widget.setMode(ComparisonWidget::ComparisonMode::SplitScreen);
    REQUIRE_FALSE(signalEmitted);
}

TEST_CASE("ComparisonDialog renders real raster previews", "[comparison][dialog][render]") {
    ensureApp();
    QgsApplication::initQgis();

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString before = dir.filePath("before.tif");
    const QString after = dir.filePath("after.tif");
    std::vector<std::vector<float>> b(2, std::vector<float>(64, 10.0f));
    std::vector<std::vector<float>> a(2, std::vector<float>(64, 200.0f));
    std::array<double, 6> gt = {0, 1, 0, 0, 0, -1};
    QString err;
    REQUIRE(writeGdalOutput(before, 8, 8, b, gt, "EPSG:4326", &err));
    REQUIRE(writeGdalOutput(after, 8, 8, a, gt, "EPSG:4326", &err));

    QgsRasterLayer beforeLayer(before, QStringLiteral("before"));
    QgsRasterLayer afterLayer(after, QStringLiteral("after"));
    REQUIRE(beforeLayer.isValid());
    REQUIRE(afterLayer.isValid());

    ComparisonDialog dialog;
    auto *widget = dialog.findChild<ComparisonWidget *>();
    REQUIRE(widget != nullptr);
    CHECK_FALSE(widget->hasLeftImage());
    CHECK_FALSE(widget->hasRightImage());

    dialog.setLeftLayer(&beforeLayer);
    dialog.setRightLayer(&afterLayer);
    // Placeholders land synchronously; the async renders deliver via the
    // event loop (#634) - drain it with a bounded wait.
    for ( int i = 0; i < 200; ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
    CHECK(widget->hasLeftImage());
    CHECK(widget->hasRightImage());

    QgsProject::instance()->clear();
}
