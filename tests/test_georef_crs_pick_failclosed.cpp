// test_georef_crs_pick_failclosed.cpp — F13 regression for issue #1005:
// mapPickToLayerCrs used to return the raw canvas coordinate when the CRS
// transform failed, silently storing a wrong GCP. The pure seam
// rsGeorefTransformPickBetweenCrs (which the shell window now delegates to)
// must fail closed instead. Expected values come from PROJ semantics
// (WGS84 lon/lat vs EPSG:32633 UTM), documented below.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "rs_georef_crs_pick.h"

#include <qgsapplication.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <qgscoordinatereferencesystem.h>
#include <qgscoordinatetransformcontext.h>
#include <qgspointxy.h>

using Catch::Approx;

namespace {
// QGIS global state (CRS registry / PROJ) needs one QgsApplication per
// process; other georef tests follow the same pattern.
struct QgisEnv {
    QgisEnv()
    {
        static int argc = 1;
        static char arg0[] = "test_georef_crs_pick_failclosed";
        static char* argv[] = {arg0};
        app = new QgsApplication(argc, argv, false);
        QgsApplication::initQgis();
    }
    // Intentionally no teardown: QgsApplication::exitQgis() hangs this
    // binary at exit (PROJ/thread teardown), long after the assertions
    // have run. The instance is leaked by design.
    QgsApplication* app;
};
} // namespace

static QgisEnv& qgisEnv()
{
    static QgisEnv env;
    return env;
}

TEST_CASE("georef pick: same CRS is the exact identity, not a failure", "[f13][issue1005]")
{
    qgisEnv();
    const auto crs = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    REQUIRE(crs.isValid());
    const QgsPointXY pt(16.3738, 48.2082);
    const auto out =
        rsGeorefTransformPickBetweenCrs(crs, crs, QgsCoordinateTransformContext(),
                                                      pt);
    REQUIRE(out.has_value());
    REQUIRE(out->x() == Approx(16.3738).margin(1e-12));
    REQUIRE(out->y() == Approx(48.2082).margin(1e-12));
}

TEST_CASE("georef pick: invalid CRS refuses instead of returning the raw point", "[f13][issue1005]"
          "[negative]")
{
    qgisEnv();
    const auto valid = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    QgsCoordinateReferenceSystem invalid; // default-constructed: invalid
    REQUIRE_FALSE(invalid.isValid());

    const QgsPointXY raw(16.3738, 48.2082);
    // Invalid destination CRS: the pre-#1005 code returned `raw` here.
    REQUIRE_FALSE(
        rsGeorefTransformPickBetweenCrs(valid, invalid,
                                                      QgsCoordinateTransformContext(), raw)
            .has_value());
    // Invalid source CRS likewise.
    REQUIRE_FALSE(
        rsGeorefTransformPickBetweenCrs(invalid, valid,
                                                      QgsCoordinateTransformContext(), raw)
            .has_value());
}

TEST_CASE("georef pick: WGS84 -> UTM33N transforms to documented meter coordinates",
          "[f13][issue1005]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    const auto utm33 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:32633"));
    REQUIRE(wgs84.isValid());
    REQUIRE(utm33.isValid());

    // Vienna (16.3738 E, 48.2082 N) in UTM 33N is roughly (613044, 5341906) m;
    // the assertion pins order of magnitude and the northern-hemisphere
    // northing, loose enough for PROJ grid differences across platforms.
    const auto out = rsGeorefTransformPickBetweenCrs(
        wgs84, utm33, QgsCoordinateTransformContext(), QgsPointXY(16.3738, 48.2082));
    REQUIRE(out.has_value());
    REQUIRE(out->x() > 500000.0);
    REQUIRE(out->x() < 700000.0);
    REQUIRE(out->y() > 5000000.0);
    REQUIRE(out->y() < 5600000.0);
}
