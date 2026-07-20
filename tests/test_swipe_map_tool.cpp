// Swipe Map Tool tests — verify tool creation, layer assignment, and position control
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QSignalSpy>

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>
#include <qgsproject.h>

#include "app/map_tools/swipe_map_tool.h"

TEST_CASE("SwipeMapTool creation and defaults", "[gui][swipe]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    CHECK(tool.swipePosition() == Catch::Approx(0.5));
    CHECK(tool.direction() == SwipeMapTool::Direction::Vertical);
    CHECK(tool.mouseFollow() == true);
    CHECK(tool.baseLayer() == nullptr);
    CHECK(tool.compareLayer() == nullptr);
}

TEST_CASE("SwipeMapTool position clamping", "[gui][swipe]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

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
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

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
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    auto *layer1 = new QgsRasterLayer(QStringLiteral("data/dem_sample.tif"), QStringLiteral("dem"));
    auto *layer2 = new QgsRasterLayer(QStringLiteral("data/landsat_sample.tif"), QStringLiteral("landsat"));

    tool.setBaseLayer(layer1);
    tool.setCompareLayer(layer2);

    CHECK(tool.baseLayer() == layer1);
    CHECK(tool.compareLayer() == layer2);

    // Null-safe assignment
    tool.setCompareLayer(nullptr);
    CHECK(tool.compareLayer() == nullptr);
}

TEST_CASE("SwipeMapTool emits swipePositionChanged", "[gui][swipe]") {
    int argc = 0;
    char *argv[] = { nullptr };
    QApplication app(argc, argv);

    QgsMapCanvas canvas;
    SwipeMapTool tool(&canvas);

    QSignalSpy spy(&tool, &SwipeMapTool::swipePositionChanged);
    REQUIRE(spy.isValid());

    tool.setSwipePosition(0.75);
    REQUIRE(spy.count() == 1);
    CHECK(spy.takeFirst().at(0).toDouble() == Catch::Approx(0.75));
}
