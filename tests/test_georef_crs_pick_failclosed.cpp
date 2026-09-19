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

#include <cmath>

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

TEST_CASE("georef pick: CRS validity semantics -- unreferenced layer passes through, "
          "invalid canvas refuses",
          "[f13][issue1005][negative]")
{
    qgisEnv();
    const auto valid = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    QgsCoordinateReferenceSystem invalid; // default-constructed: invalid
    REQUIRE_FALSE(invalid.isValid());

    const QgsPointXY raw(16.3738, 48.2082);
    // An invalid LAYER CRS means "this raster has no CRS": canvas picks are
    // raw image coordinates -- the georeferencing workflow itself. This
    // pass-through is preserved semantics, not the #1005 failure.
    const auto out =
        rsGeorefTransformPickBetweenCrs(valid, invalid, QgsCoordinateTransformContext(), raw);
    REQUIRE(out.has_value());
    REQUIRE(out->x() == Approx(16.3738).margin(1e-12));

    // Invalid CANVAS CRS with a valid layer CRS is uninterpretable: the
    // pre-#1005 code returned `raw` here; fail closed now.
    REQUIRE_FALSE(
        rsGeorefTransformPickBetweenCrs(invalid, valid, QgsCoordinateTransformContext(), raw)
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

// #1030 F-1030-P1-gcp: the destination transform before a GCP is stored used
// to be an empty catch that kept the raster-CRS point and reported success.
// The decision now lives in the pure seam rsGeorefNormalizeGcpDestination.
TEST_CASE("georef GCP destination: normalization semantics before a GCP is stored",
          "[f13][issue1030][gcp]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    const auto utm33 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:32633"));
    const QgsCoordinateTransformContext ctx;
    const QgsPointXY pick(16.3738, 48.2082);

    // Unreferenced raster / no destination CRS chosen yet: the pick is stored
    // as-is. That is not a transform failure (pre-existing workflow).
    const auto passthrough = rsGeorefNormalizeGcpDestination(
        pick, QgsCoordinateReferenceSystem(), QgsCoordinateReferenceSystem(), ctx);
    REQUIRE_FALSE(passthrough.refuse);
    REQUIRE(passthrough.point.x() == Approx(pick.x()).margin(1e-12));

    // Same CRS: exact identity, not a failure.
    const auto same = rsGeorefNormalizeGcpDestination(pick, wgs84, wgs84, ctx);
    REQUIRE_FALSE(same.refuse);
    REQUIRE(same.point.x() == Approx(pick.x()).margin(1e-12));
    REQUIRE(same.point.y() == Approx(pick.y()).margin(1e-12));

    // Valid differing pair is transformed (same seam as mapPickToLayerCrs).
    const auto utm = rsGeorefNormalizeGcpDestination(pick, wgs84, utm33, ctx);
    REQUIRE_FALSE(utm.refuse);
    REQUIRE(utm.point.x() > 500000.0);
    REQUIRE(utm.point.x() < 700000.0);
    REQUIRE(utm.point.y() > 5000000.0);
}

TEST_CASE("georef GCP destination: an unplaceable pick is refused, never stored",
          "[f13][issue1030][gcp][negative]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    const auto utm33 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:32633"));
    const QgsCoordinateTransformContext ctx;

    // Latitude 95 is outside the UTM domain. PROJ either errors (refuse) or
    // yields a non-finite coordinate (also refused) -- what must never happen
    // is a silently plausible finite pair stored as a GCP.
    const auto outOfDomain =
        rsGeorefNormalizeGcpDestination(QgsPointXY(16.0, 95.0), wgs84, utm33, ctx);
    const bool usableAsIs = !outOfDomain.refuse
        && std::isfinite(outOfDomain.point.x()) && std::isfinite(outOfDomain.point.y());
    REQUIRE_FALSE(usableAsIs);
}
