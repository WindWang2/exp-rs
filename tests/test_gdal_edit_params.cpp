// tests/test_gdal_edit_params.cpp — TDD for GDAL edit/pct2rgb/rgb2pct/gdal2xyz buildArgs
#include <catch2/catch_test_macros.hpp>
#include "processing/providers/gdal_tools/algorithms/gdal_edit.h"
#include "processing/providers/gdal_tools/algorithms/pct2rgb.h"
#include "processing/providers/gdal_tools/algorithms/rgb2pct.h"
#include "processing/providers/gdal_tools/algorithms/gdal2xyz.h"
#include <processing/qgsprocessingcontext.h>

class TestableGdalEdit : public GdalEditAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

class TestablePct2Rgb : public Pct2RgbAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

class TestableRgb2Pct : public Rgb2PctAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

class TestableGdal2Xyz : public Gdal2XyzAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

TEST_CASE("GDAL wrapper buildArgs", "[gdal][processing]") {
    SECTION("gdal_edit: metadata flags") {
        TestableGdalEdit algo;
        QVariantMap p;
        p["INPUT"] = "/data/in.tif";
        p["TARGET_CRS"] = "EPSG:4326";
        p["NODATA"] = -9999.0;
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.contains("/data/in.tif"));
        CHECK(args.indexOf("-a_srs") >= 0);
        CHECK(args.indexOf("-a_nodata") >= 0);
    }

    SECTION("pct2rgb: palette to RGB") {
        TestablePct2Rgb algo;
        QVariantMap p;
        p["INPUT"] = "/data/palette.tif";
        p["OUTPUT"] = "/data/rgb.tif";
        p["FORMAT"] = "GTiff";
        p["RGBA"] = true;
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.contains("/data/palette.tif"));
        CHECK(args.contains("/data/rgb.tif"));
        CHECK(args.indexOf("-of") >= 0);
        CHECK(args.contains("-rgba"));
    }

    SECTION("rgb2pct: RGB to palette") {
        TestableRgb2Pct algo;
        QVariantMap p;
        p["INPUT"] = "/data/rgb.tif";
        p["OUTPUT"] = "/data/palette.tif";
        p["COLORS"] = 128;
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.contains("/data/rgb.tif"));
        CHECK(args.contains("/data/palette.tif"));
        CHECK(args.indexOf("-n") >= 0);
        CHECK(args.contains("128"));
    }

    SECTION("gdal2xyz: raster to XYZ") {
        TestableGdal2Xyz algo;
        QVariantMap p;
        p["INPUT"] = "/data/dem.tif";
        p["OUTPUT"] = "/data/dem.xyz";
        p["CSV"] = true;
        p["SKIPNODATA"] = true;
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.contains("/data/dem.tif"));
        CHECK(args.contains("/data/dem.xyz"));
        CHECK(args.contains("-csv"));
        CHECK(args.contains("-skipnodata"));
    }
}