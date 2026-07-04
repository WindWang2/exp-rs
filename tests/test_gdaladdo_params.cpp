// tests/test_gdaladdo_params.cpp — TDD for GDAL Addo wrapper buildArgs
#include <catch2/catch_test_macros.hpp>
#include "processing/providers/gdal_tools/algorithms/gdaladdo.h"
#include <processing/qgsprocessingcontext.h>

class TestableGdalAddo : public GdalAddoAlgorithm {
public:
    QStringList testBuildArgs(const QVariantMap &p) {
        QgsProcessingContext ctx;
        return buildArgs(p, ctx, nullptr);
    }
};

TEST_CASE("GDAL Addo: buildArgs", "[gdal][processing]") {
    TestableGdalAddo algo;
    QVariantMap p;
    p["INPUT"] = "/data/in.tif";
    p["LEVELS"] = 5;
    p["OUTPUT"] = "/data/out.tif";
    QStringList args = algo.testBuildArgs(p);
    CHECK(args.contains("/data/in.tif"));
    CHECK(args.indexOf("-r") >= 0);
}