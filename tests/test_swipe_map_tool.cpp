// Swipe Map Tool tests — verify tool creation, layer assignment, and position control
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QSignalSpy>

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include "app/map_tools/swipe_map_tool.h"

namespace {

// Heap-allocated, intentionally leaked: one QApplication per process (the
// established pattern in this suite's GUI tests). Stack-constructing one per
// TEST_CASE constructs/destructs the app five times per binary run (#568).
void ensureApp()
{
    if ( QApplication::instance() )
        return;
    static int argc = 0;
    static char *argv[] = { nullptr };
    new QApplication( argc, argv );
}

} // namespace

TEST_CASE("SwipeMapTool creation and defaults", "[gui][swipe]") {
    ensureApp();
    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    CHECK(tool.swipePosition() == Catch::Approx(0.5));
    CHECK(tool.direction() == SwipeMapTool::Direction::Vertical);
    CHECK(tool.mouseFollow() == true);
    CHECK(tool.baseLayer() == nullptr);
    CHECK(tool.compareLayer() == nullptr);
}

TEST_CASE("SwipeMapTool position clamping", "[gui][swipe]") {
    ensureApp();
    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    SECTION("Clamps below 0") {
        tool.setSwipePosition(-0.5);
        CHECK(tool.swipePosition() == 0.0);
    }

    SECTION("Clamps above 1") {
        tool.setSwipePosition(1.5);
        CHECK(tool.swipePosition() == 1.0);
    }

    SECTION("Accepts valid position") {
        tool.setSwipePosition(0.25);
        CHECK(tool.swipePosition() == Catch::Approx(0.25));
    }
}

TEST_CASE("SwipeMapTool direction switching", "[gui][swipe]") {
    ensureApp();
    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    QSignalSpy spy(&tool, &SwipeMapTool::directionChanged);
    REQUIRE(spy.isValid());

    tool.setDirection(SwipeMapTool::Direction::Horizontal);
    CHECK(tool.direction() == SwipeMapTool::Direction::Horizontal);
    CHECK(spy.count() == 1);

    tool.setDirection(SwipeMapTool::Direction::Vertical);
    CHECK(tool.direction() == SwipeMapTool::Direction::Vertical);
    CHECK(spy.count() == 2);
}

TEST_CASE("SwipeMapTool layer assignment", "[gui][swipe]") {
    ensureApp();
    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    // #656: sample rasters under data/ are not committed - raw relative
    // paths made this a shape-only assertion on nonexistent layers. The
    // layers are expected to be invalid here; the contract under test is
    // pointer plumbing, so the invalidity is asserted explicitly instead of
    // being left silently unchecked.
    auto *layer1 = new QgsRasterLayer(QStringLiteral("data/dem_sample.tif"), QStringLiteral("dem"));
    auto *layer2 = new QgsRasterLayer(QStringLiteral("data/landsat_sample.tif"), QStringLiteral("landsat"));
    CHECK_FALSE(layer1->isValid());
    CHECK_FALSE(layer2->isValid());

    tool.setBaseLayer(layer1);
    tool.setCompareLayer(layer2);

    CHECK(tool.baseLayer() == layer1);
    CHECK(tool.compareLayer() == layer2);

    // Null-safe assignment
    tool.setCompareLayer(nullptr);
    CHECK(tool.compareLayer() == nullptr);
}

TEST_CASE("SwipeMapTool emits swipePositionChanged", "[gui][swipe]") {
    ensureApp();
    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    QSignalSpy spy(&tool, &SwipeMapTool::swipePositionChanged);
    REQUIRE(spy.isValid());

    tool.setSwipePosition(0.75);
    REQUIRE(spy.count() == 1);
    CHECK(spy.takeFirst().at(0).toDouble() == Catch::Approx(0.75));
}
