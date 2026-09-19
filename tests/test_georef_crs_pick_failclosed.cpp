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
#include <qgscoordinatetransform.h>
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

TEST_CASE("georef pick: CRS validity semantics — unreferenced layer passes through, "
          "invalid canvas refuses",
          "[f13][issue1005][negative]")
{
    qgisEnv();
    const auto valid = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    QgsCoordinateReferenceSystem invalid; // default-constructed: invalid
    REQUIRE_FALSE(invalid.isValid());

    const QgsPointXY raw(16.3738, 48.2082);
    // An invalid LAYER CRS means "this raster has no CRS": canvas picks are
    // raw image coordinates — the georeferencing workflow itself. This
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

// ============================================================================
// #1037 F-1030-P1-gcp: the destination point stored on a GCP must be
// transformed from the reference raster CRS into the panel's target CRS.
// A required transform that is unbuildable (ct.isValid()==false) makes
// QgsCoordinateTransform::transform() return the INPUT silently — the seam
// must fail closed instead of storing an untransformed dst tagged with the
// target CRS.
// ============================================================================

TEST_CASE("georef dest store: same CRS is the exact identity", "[f13][issue1037]")
{
    qgisEnv();
    const auto crs = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    REQUIRE(crs.isValid());
    const QgsPointXY pt(16.3738, 48.2082);
    const auto out =
        rsGeorefTransformDestinationForStore(crs, crs, QgsCoordinateTransformContext(), pt);
    REQUIRE(out.has_value());
    REQUIRE(out->x() == Approx(16.3738).margin(1e-12));
    REQUIRE(out->y() == Approx(48.2082).margin(1e-12));
}

TEST_CASE("georef dest store: invalid CRS passes the pick through (unreferenced raster)",
          "[f13][issue1037]")
{
    qgisEnv();
    QgsCoordinateReferenceSystem invalid; // default-constructed: invalid
    const auto target = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:32633"));
    REQUIRE_FALSE(invalid.isValid());
    REQUIRE(target.isValid());

    const QgsPointXY raw(501234.5, 5123456.75);
    // Missing raster CRS means "raw image coordinates" — the georeferencing
    // workflow itself. Missing target CRS means the panel has no destination
    // to transform into. Both mirror the legacy guard's pass-through.
    const auto fromInvalidRaster =
        rsGeorefTransformDestinationForStore(invalid, target, QgsCoordinateTransformContext(), raw);
    REQUIRE(fromInvalidRaster.has_value());
    REQUIRE(fromInvalidRaster->x() == Approx(501234.5).margin(1e-12));
    REQUIRE(fromInvalidRaster->y() == Approx(5123456.75).margin(1e-12));

    const auto fromInvalidTarget =
        rsGeorefTransformDestinationForStore(target, invalid, QgsCoordinateTransformContext(), raw);
    REQUIRE(fromInvalidTarget.has_value());
    REQUIRE(fromInvalidTarget->x() == Approx(501234.5).margin(1e-12));
    REQUIRE(fromInvalidTarget->y() == Approx(5123456.75).margin(1e-12));
}

TEST_CASE("georef dest store: unbuildable required transform fails closed",
          "[f13][issue1037][negative]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    // EPSG:5703 (NAVD88 height) is a valid CRS, but a horizontal -> vertical
    // operation is unbuildable on this build (PROJ emits no pipeline).
    const auto vertical = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5703"));
    REQUIRE(wgs84.isValid());
    REQUIRE(vertical.isValid());

    // Explicit precondition: the operation is unbuildable, and transform()
    // would silently return the input (probe-verified) rather than throwing.
    const QgsCoordinateTransform unbuildable(wgs84, vertical, QgsCoordinateTransformContext());
    REQUIRE_FALSE(unbuildable.isValid());

    const QgsPointXY pt(16.3738, 48.2082);
    REQUIRE_FALSE(
        rsGeorefTransformDestinationForStore(wgs84, vertical, QgsCoordinateTransformContext(), pt)
            .has_value());
}

TEST_CASE("georef dest store: WGS84 -> UTM33N transforms to documented meter coordinates",
          "[f13][issue1037]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    const auto utm33 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:32633"));
    REQUIRE(wgs84.isValid());
    REQUIRE(utm33.isValid());

    const auto out = rsGeorefTransformDestinationForStore(
        wgs84, utm33, QgsCoordinateTransformContext(), QgsPointXY(16.3738, 48.2082));
    REQUIRE(out.has_value());
    REQUIRE(out->x() > 500000.0);
    REQUIRE(out->x() < 700000.0);
    REQUIRE(out->y() > 5000000.0);
    REQUIRE(out->y() < 5600000.0);
}

TEST_CASE("georef pick: unbuildable required transform fails closed", "[f13][issue1037][negative]")
{
    qgisEnv();
    const auto wgs84 = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:4326"));
    const auto vertical = QgsCoordinateReferenceSystem(QStringLiteral("EPSG:5703"));
    REQUIRE(wgs84.isValid());
    REQUIRE(vertical.isValid());
    const QgsCoordinateTransform unbuildable(wgs84, vertical, QgsCoordinateTransformContext());
    REQUIRE_FALSE(unbuildable.isValid());

    const QgsPointXY pt(16.3738, 48.2082);
    REQUIRE_FALSE(
        rsGeorefTransformPickBetweenCrs(wgs84, vertical, QgsCoordinateTransformContext(), pt)
            .has_value());
}
