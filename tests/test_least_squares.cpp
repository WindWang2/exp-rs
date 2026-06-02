// tests/test_least_squares.cpp — TDD for QgsLeastSquares (georeferencing)
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "qgsleastsquares.h"
#include "qgspointxy.h"

#include <QVector>

using Catch::Approx;

TEST_CASE("LeastSquares linear: pure translation +100,+200", "[georef][leastsquares]") {
    QVector<QgsPointXY> src = { {0, 0}, {10, 0}, {0, 10}, {10, 10} };
    QVector<QgsPointXY> dst = { {100, 200}, {110, 200}, {100, 210}, {110, 210} };
    QgsPointXY origin;
    double pixelXSize = 0.0;
    double pixelYSize = 0.0;
    QgsLeastSquares::linear(src, dst, origin, pixelXSize, pixelYSize);
    REQUIRE(origin.x() == Approx(100.0).margin(1e-9));
    REQUIRE(origin.y() == Approx(200.0).margin(1e-9));
    REQUIRE(pixelXSize == Approx(1.0).margin(1e-9));
    REQUIRE(pixelYSize == Approx(1.0).margin(1e-9));
}

TEST_CASE("LeastSquares linear: throws SingularException on collinear GCPs", "[georef][leastsquares][error]") {
    // All source points share x=0 (degenerate in x): deltaX = n*sumPx2 - sumPx^2 = 0
    QVector<QgsPointXY> src = { {0, 0}, {0, 1}, {0, 2}, {0, 3} };
    QVector<QgsPointXY> dst = { {0, 0}, {0, 2}, {0, 4}, {0, 6} };
    QgsPointXY origin;
    double pixelXSize = 0.0;
    double pixelYSize = 0.0;
    REQUIRE_THROWS_AS(
        QgsLeastSquares::linear(src, dst, origin, pixelXSize, pixelYSize),
        QgsLeastSquares::SingularException);
}
