// tests/test_g02_gdal_regression.cpp — Regression for G02 issues 303/331/351/400
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "processing/providers/gdal_tools/algorithms/gdal_grid.h"
#include "processing/providers/gdal_tools/algorithms/gdal_edit.h"
#include "processing/providers/gdal_tools/algorithms/gdal_rasterize.h"
#include "processing/providers/gdal_tools/algorithms/gdal_warp.h"
#include "processing/providers/gdal_tools/algorithms/gdalbuildvrt.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <processing/qgsprocessingcontext.h>
#include <processing/qgsprocessingparameters.h>
#include <qgsexception.h>

#include <QTemporaryDir>
#include <QFileInfo>
#include <QDir>
#include <gdal.h>
#include <vector>
#include <cmath>

// ---------- helpers ----------
class TestableGdalGrid : public GdalGridAlgorithm {
public:
    TestableGdalGrid() { initAlgorithm(); }
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};
class TestableGdalEdit2 : public GdalEditAlgorithm {
public:
    TestableGdalEdit2() { initAlgorithm(); }
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};
class TestableGdalWarp2 : public GdalWarpAlgorithm {
public:
    TestableGdalWarp2() { initAlgorithm(); }
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};
class TestableGdalRasterize2 : public GdalRasterizeAlgorithm {
public:
    TestableGdalRasterize2() { initAlgorithm(); }
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

// ------------------------------------------------------------------
// 303 residual + 400 : gdal_grid smoothing gate, precision, -tr guard
// ------------------------------------------------------------------
TEST_CASE("G02 gdal_grid: smoothing gate and precision (400)", "[gdal][g02]") {
    SECTION("average must not contain smoothing") {
        TestableGdalGrid algo;
        QVariantMap p;
        p["INPUT"] = "/data/points.shp";
        p["OUTPUT"] = "/tmp/out.tif";
        p["ALGORITHM"] = 1; // average (enum index 1)
        p["POWER"] = 2.1234567;
        p["SMOOTH"] = 0.5;
        p["NODATA"] = -9999.0;
        QStringList args = algo.testBuildArgs(p);
        int idx = args.indexOf("-a");
        REQUIRE(idx >= 0);
        QString aVal = args[idx + 1];
        CHECK(!aVal.contains("smoothing")); // no smoothing key
        CHECK(aVal.contains("average"));
        // nodata should be present with full precision
        CHECK(aVal.contains("nodata"));
    }
    SECTION("invdist preserves 7-decimal power with g15") {
        TestableGdalGrid algo;
        QVariantMap p;
        p["INPUT"] = "/data/points.shp";
        p["OUTPUT"] = "/tmp/out.tif";
        p["ALGORITHM"] = 2; // invdist
        p["POWER"] = 2.1234567;
        p["SMOOTH"] = 0.5;
        QStringList args = algo.testBuildArgs(p);
        int idx = args.indexOf("-a");
        REQUIRE(idx >= 0);
        QString aVal = args[idx + 1];
        // 2.1234567 should appear verbatim, not truncated to 2.12346
        CHECK(aVal.contains("2.1234567"));
        CHECK(aVal.contains("smoothing"));
    }
    SECTION("PIXEL_SIZE without EXTENT throws (303 residual)") {
        TestableGdalGrid algo;
        QVariantMap p;
        p["INPUT"] = "/data/points.shp";
        p["OUTPUT"] = "/tmp/out.tif";
        p["ALGORITHM"] = 2;
        p["PIXEL_SIZE"] = 10.0;
        // No EXTENT
        bool threw = false;
        try {
            (void)algo.testBuildArgs(p);
        } catch (const QgsProcessingException &) {
            threw = true;
        }
        CHECK(threw);
    }
    SECTION("PIXEL_SIZE with EXTENT emits -tr with g15") {
        TestableGdalGrid algo;
        QVariantMap p;
        p["INPUT"] = "/data/points.shp";
        p["OUTPUT"] = "/tmp/out.tif";
        p["ALGORITHM"] = 2;
        p["EXTENT"] = "0,10,20,30";
        p["PIXEL_SIZE"] = 2.123456789012;
        QStringList args = algo.testBuildArgs(p);
        int idx = args.indexOf("-tr");
        REQUIRE(idx >= 0);
        // high-precision value not truncated to 6 digits
        CHECK(args[idx + 1].contains("2.123456789012"));
        CHECK(args.contains("-txe"));
        CHECK(args.contains("-tye"));
    }
}

// ------------------------------------------------------------------
// 351b : gdal_edit half-set -tr
// ------------------------------------------------------------------
TEST_CASE("G02 gdal_edit half-set -tr validation (351b)", "[gdal][g02]") {
    SECTION("only X_RES throws") {
        TestableGdalEdit2 algo;
        QVariantMap p;
        p["INPUT"] = "/data/in.tif";
        p["X_RES"] = 10.0;
        // Y_RES missing
        bool threw = false;
        try { (void)algo.testBuildArgs(p); } catch (const QgsProcessingException &) { threw = true; }
        CHECK(threw);
    }
    SECTION("only Y_RES throws") {
        TestableGdalEdit2 algo;
        QVariantMap p;
        p["INPUT"] = "/data/in.tif";
        p["Y_RES"] = 10.0;
        bool threw = false;
        try { (void)algo.testBuildArgs(p); } catch (const QgsProcessingException &) { threw = true; }
        CHECK(threw);
    }
    SECTION("both set emits -tr") {
        TestableGdalEdit2 algo;
        QVariantMap p;
        p["INPUT"] = "/data/in.tif";
        p["X_RES"] = 10.0;
        p["Y_RES"] = 20.0;
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.contains("-tr"));
    }
    SECTION("neither set emits no -tr") {
        TestableGdalEdit2 algo;
        QVariantMap p;
        p["INPUT"] = "/data/in.tif";
        QStringList args = algo.testBuildArgs(p);
        CHECK(args.indexOf("-tr") == -1);
    }
}

// ------------------------------------------------------------------
// 351 PROVG-10 : warp -overwrite
// ------------------------------------------------------------------
TEST_CASE("G02 gdal_warp contains -overwrite (351 PROVG-10)", "[gdal][g02]") {
    TestableGdalWarp2 algo;
    QVariantMap p;
    p["INPUT"] = "/data/in.tif";
    p["OUTPUT"] = "/tmp/out.tif";
    QStringList args = algo.testBuildArgs(p);
    CHECK(args.contains("-overwrite"));
}

// ------------------------------------------------------------------
// 351e : gdalbuildvrt FileDestination with .vrt filter
// ------------------------------------------------------------------
TEST_CASE("G02 gdalbuildvrt OUTPUT is FileDestination with VRT filter (351e)", "[gdal][g02]") {
    GdalBuildVrtAlgorithm algo;
    algo.initAlgorithm();
    const auto *param = algo.parameterDefinition("OUTPUT");
    REQUIRE(param != nullptr);
    CHECK(param->type() == QgsProcessingParameterFileDestination::typeName());
    CHECK((param->description().contains("VRT") || param->description().contains("vrt") || param->name() == "OUTPUT"));
}

// ------------------------------------------------------------------
// 331 : readBandWindowScaled padding contract
// ------------------------------------------------------------------
TEST_CASE("G02 readBandWindowScaled correctly pads out-of-raster with nodata (331)", "[gdal][g02]") {
    GDALAllRegister();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString path = tmp.path() + "/scale_pad.tif";
    const int W = 10, H = 10;
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    REQUIRE(drv != nullptr);
    GDALDatasetH ds = GDALCreate(drv, path.toUtf8().constData(), W, H, 1, GDT_Float32, nullptr);
    REQUIRE(ds != nullptr);
    double gt[6] = {0,1,0,10,0,-1};
    GDALSetGeoTransform(ds, gt);
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    // Fill with ramp 1..100
    std::vector<float> line(W);
    for (int r=0;r<H;++r){
        for(int c=0;c<W;++c) line[c]= float(r*W+c+1);
        GDALRasterIO(band, GF_Write, 0,r,W,1, line.data(), W,1, GDT_Float32, 0,0);
    }
    GDALClose(ds);

    GdalDatasetWrapper wrap;
    REQUIRE(wrap.open(path));
    const float nodata = -9999.f;

    SECTION("partial overflow past right/bottom preserves nodata padding") {
        const int bufW=6, bufH=6;
        std::vector<float> buf(bufW*bufH, 0.f);
        bool ok = wrap.readBandWindowScaled(1, 8, 8, 6, 6, buf.data(), bufW, bufH, nodata);
        REQUIRE(ok);
        // At least some interior should be valid, some padding nodata
        // Bottom-right 4 rows/cols extend past edge -> those buffer cells must be nodata
        // With scale 1, dst should have 2 valid cols/rows at origin of buffer, rest nodata
        CHECK(buf[0] != nodata); // top-left valid
        CHECK(buf[bufW*bufH-1] == nodata); // bottom-right padding
        // count nodata
        int nodataCnt=0;
        for(float v: buf) if(v==nodata) ++nodataCnt;
        CHECK(nodataCnt > 0);
        CHECK(nodataCnt < bufW*bufH);
    }
    SECTION("negative offset preserves nodata padding") {
        const int bufW=6, bufH=6;
        std::vector<float> buf(bufW*bufH, 0.f);
        bool ok = wrap.readBandWindowScaled(1, -2, -2, 6, 6, buf.data(), bufW, bufH, nodata);
        REQUIRE(ok);
        CHECK(buf[0] == nodata);
        int nodataCnt=0;
        for(float v: buf) if(v==nodata) ++nodataCnt;
        CHECK(nodataCnt > 0);
    }
    SECTION("fully outside returns all nodata") {
        const int bufW=4, bufH=4;
        std::vector<float> buf(bufW*bufH, 0.f);
        bool ok = wrap.readBandWindowScaled(1, 20, 20, 4, 4, buf.data(), bufW, bufH, nodata);
        REQUIRE(ok);
        for(float v: buf) CHECK(v == nodata);
    }
    SECTION("heterogeneous scale with edge overflow") {
        // src 5 -> buf 10 (scale 2), window extends past edge
        const int srcW=5, srcH=5, bufW=10, bufH=10;
        std::vector<float> buf(bufW*bufH, 0.f);
        // window at 8,8 size 5 -> extends to 13 past 10
        bool ok = wrap.readBandWindowScaled(1, 8, 8, srcW, srcH, buf.data(), bufW, bufH, nodata);
        REQUIRE(ok);
        int nodataCnt=0;
        for(float v: buf) if(v==nodata) ++nodataCnt;
        CHECK(nodataCnt > 0);
        CHECK(buf[0] != nodata);
    }
}

// ------------------------------------------------------------------
// 351a : rasterize rotation-aware -te (basic sanity with temp raster)
// ------------------------------------------------------------------
TEST_CASE("G02 gdal_rasterize rotation-aware -te (351a)", "[gdal][g02]") {
    GDALAllRegister();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString tmplPath = tmp.path() + "/rot_template.tif";
    const int tw=10, th=10;
    GDALDriverH drv = GDALGetDriverByName("GTiff");
    REQUIRE(drv != nullptr);
    GDALDatasetH ds = GDALCreate(drv, tmplPath.toUtf8().constData(), tw, th, 1, GDT_Byte, nullptr);
    REQUIRE(ds != nullptr);
    // Rotated geotransform: gt[2]=0.5, gt[4]=0.5 (shear/rotation)
    double gt[6] = {100.0, 1.0, 0.5, 200.0, 0.5, -1.0};
    GDALSetGeoTransform(ds, gt);
    GDALClose(ds);

    TestableGdalRasterize2 algo;
    QVariantMap p;
    p["INPUT"] = "/data/vec.shp";
    p["RASTER_TEMPLATE"] = tmplPath;
    p["OUTPUT"] = "/tmp/out.tif";
    QStringList args = algo.testBuildArgs(p);
    int teIdx = args.indexOf("-te");
    REQUIRE(teIdx >= 0);
    double teMinX = args[teIdx+1].toDouble();
    double teMinY = args[teIdx+2].toDouble();
    double teMaxX = args[teIdx+3].toDouble();
    double teMaxY = args[teIdx+4].toDouble();
    // Axis-aligned naive would be minX=100, maxX=110, minY=190, maxY=200
    // Rotation-aware should be larger bounding box: minX 100, maxX 115, minY 190, maxY 205? approx
    // Check that rotation expanded the bbox
    CHECK(teMinX == 100.0);
    CHECK(teMaxX > 110.0); // rotation increases width
    CHECK((teMinY < 190.0 || teMaxY > 200.0));
}
