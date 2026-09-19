// tests/test_vector_overlay_crs_transform.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <qgsapplication.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeatureiterator.h>
#include <qgsgeometry.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsexception.h>

#include "processing/providers/qgis_algorithms/algorithms/native/native_difference.h"
#include "processing/providers/qgis_algorithms/algorithms/native/native_intersection.h"
#include "processing/providers/qgis_algorithms/algorithms/native/native_union.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_difference.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_symmetrical_difference.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_spatial_query.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_select_by_location.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_extract_by_location.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_merge.h"

namespace {

QgsVectorLayer *createLayer4326(const QString &wktGeom)
{
    auto *layer = new QgsVectorLayer("Polygon?crs=EPSG:4326&field=name:string", "layer_4326", "memory");
    QgsFeature feat(layer->fields());
    feat.setAttribute("name", "feat_4326");
    feat.setGeometry(QgsGeometry::fromWkt(wktGeom));
    QgsFeatureList flist = {feat};
    layer->dataProvider()->addFeatures(flist);
    layer->updateExtents();
    return layer;
}

QgsVectorLayer *createLayer3857(const QString &wktGeom)
{
    auto *layer = new QgsVectorLayer("Polygon?crs=EPSG:3857&field=name:string", "layer_3857", "memory");
    QgsFeature feat(layer->fields());
    feat.setAttribute("name", "feat_3857");
    feat.setGeometry(QgsGeometry::fromWkt(wktGeom));
    QgsFeatureList flist = {feat};
    layer->dataProvider()->addFeatures(flist);
    layer->updateExtents();
    return layer;
}

} // namespace

TEST_CASE("Vector overlay algorithms transform geometries across different CRSs (#304)", "[processing][vector][crs]") {
    // Layer 1: EPSG:4326 covering (0,0) to (1,1)
    // In EPSG:3857, (0,0) to (1,1) is approx (0,0) to (111319.49, 111325.14)
    std::unique_ptr<QgsVectorLayer> in4326(createLayer4326("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))"));
    REQUIRE(in4326->isValid());

    // Layer 2: EPSG:3857 covering (0,0) to (50000, 50000)
    // This is roughly (0,0) to (0.449, 0.449) in EPSG:4326, which overlaps the bottom-left quarter of in4326.
    std::unique_ptr<QgsVectorLayer> ov3857(createLayer3857("POLYGON((0 0, 50000 0, 50000 50000, 0 50000, 0 0))"));
    REQUIRE(ov3857->isValid());

    QgsProcessingContext context;
    QgsProcessingFeedback feedback;

    SECTION("QgsDifferenceAlgorithm with differing CRSs") {
        QgsDifferenceAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("OVERLAY")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        REQUIRE(outLayer->featureCount() == 1);
        QgsFeature outFeat;
        QgsFeatureIterator it = outLayer->getFeatures();
        REQUIRE(it.nextFeature(outFeat));
        // The original area was 1.0 sq degrees. Difference should subtract the transformed ~0.2 sq degrees.
        CHECK(outFeat.geometry().area() < 0.95);
        CHECK(outFeat.geometry().area() > 0.5);
    }

    SECTION("QgsIntersectionAlgorithm with differing CRSs") {
        QgsIntersectionAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("OVERLAY")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        REQUIRE(outLayer->featureCount() == 1);
        QgsFeature outFeat;
        QgsFeatureIterator it = outLayer->getFeatures();
        REQUIRE(it.nextFeature(outFeat));
        CHECK(outFeat.geometry().area() > 0.1);
        CHECK(outFeat.geometry().area() < 0.3);
    }

    SECTION("VectorDifferenceAlgorithm with differing CRSs") {
        VectorDifferenceAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("OVERLAY")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        REQUIRE(outLayer->featureCount() == 1);
        QgsFeature outFeat;
        QgsFeatureIterator it = outLayer->getFeatures();
        REQUIRE(it.nextFeature(outFeat));
        CHECK(outFeat.geometry().area() < 0.95);
        CHECK(outFeat.geometry().area() > 0.5);
    }

    SECTION("VectorSpatialQueryAlgorithm with differing CRSs") {
        VectorSpatialQueryAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("INTERSECT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("PREDICATE")] = 0; // intersects
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        CHECK(outLayer->featureCount() == 1);
    }

    SECTION("VectorSelectByLocationAlgorithm with differing CRSs") {
        VectorSelectByLocationAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("INTERSECT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("PREDICATE")] = 0; // intersects
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        CHECK(outLayer->featureCount() == 1);
    }

    SECTION("VectorExtractByLocationAlgorithm with differing CRSs") {
        VectorExtractByLocationAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get()));
        params[QStringLiteral("INTERSECT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()));
        params[QStringLiteral("PREDICATE")] = 0; // intersects
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        CHECK(outLayer->featureCount() == 1);
    }

    SECTION("VectorMergeAlgorithm with differing CRSs") {
        VectorMergeAlgorithm proto;
        std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

        QVariantMap params;
        QVariantList mapLayers = {
            QVariant::fromValue(static_cast<QgsMapLayer *>(in4326.get())),
            QVariant::fromValue(static_cast<QgsMapLayer *>(ov3857.get()))
        };
        params[QStringLiteral("INPUT_LAYERS")] = mapLayers;
        params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

        bool ok = false;
        QVariantMap res = alg->run(params, context, &feedback, &ok);
        REQUIRE(ok);
        REQUIRE(res.contains(QStringLiteral("OUTPUT")));
        auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
        REQUIRE(outLayer != nullptr);
        REQUIRE(outLayer->featureCount() == 2);

        // Both features should now be in EPSG:4326 space (extents between -180 and 180 deg)
        QgsFeatureIterator it = outLayer->getFeatures();
        QgsFeature f1, f2;
        REQUIRE(it.nextFeature(f1));
        REQUIRE(it.nextFeature(f2));
        CHECK(f1.geometry().boundingBox().xMaximum() <= 1.01);
        CHECK(f2.geometry().boundingBox().xMaximum() <= 1.01);
    }
}

TEST_CASE("VectorMergeAlgorithm refuses unwritable features instead of dropping them (#1043)",
          "[processing][vector][merge]") {
    // The sink is typed from the FIRST layer's wkbType while INPUT_LAYERS
    // accepts VectorAnyGeometry: merging a Point layer with a Polygon layer
    // used to silently drop every non-converting feature while the task
    // reported success. The addFeature() check (#1043) turns that into a
    // loud QgsProcessingException.
    std::unique_ptr<QgsVectorLayer> pointLayer(new QgsVectorLayer(
        "Point?crs=EPSG:4326&field=name:string", "merge_pt", "memory"));
    REQUIRE(pointLayer->isValid());
    QgsFeature pt(pointLayer->fields());
    pt.setAttribute("name", "pt");
    pt.setGeometry(QgsGeometry::fromWkt("POINT(0 0)"));
    QgsFeatureList ptList = {pt};
    REQUIRE(pointLayer->dataProvider()->addFeatures(ptList));

    std::unique_ptr<QgsVectorLayer> polyLayer(new QgsVectorLayer(
        "Polygon?crs=EPSG:4326&field=name:string", "merge_poly", "memory"));
    REQUIRE(polyLayer->isValid());
    QgsFeature pg(polyLayer->fields());
    pg.setAttribute("name", "pg");
    pg.setGeometry(QgsGeometry::fromWkt("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))"));
    QgsFeatureList pgList = {pg};
    REQUIRE(polyLayer->dataProvider()->addFeatures(pgList));

    VectorMergeAlgorithm proto;
    std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    QVariantList mapLayers = {
        QVariant::fromValue(static_cast<QgsMapLayer *>(pointLayer.get())),
        QVariant::fromValue(static_cast<QgsMapLayer *>(polyLayer.get()))
    };
    params[QStringLiteral("INPUT_LAYERS")] = mapLayers;
    params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

    bool ok = true;
    try {
        (void)alg->run(params, context, &feedback, &ok);
        FAIL("expected QgsProcessingException for features the sink cannot store");
    } catch (const QgsProcessingException &e) {
        const QString message = e.what();
        CHECK(message.contains(QStringLiteral("Could not write feature"), Qt::CaseInsensitive));
        CHECK(message.contains(QStringLiteral("geometry type"), Qt::CaseInsensitive));
    }
    CHECK_FALSE(ok);

    // Same-type merges still succeed (the guard must not reject valid work).
    std::unique_ptr<QgsVectorLayer> pointLayer2(new QgsVectorLayer(
        "Point?crs=EPSG:4326&field=name:string", "merge_pt2", "memory"));
    REQUIRE(pointLayer2->isValid());
    QgsFeature pt2(pointLayer2->fields());
    pt2.setAttribute("name", "pt2");
    pt2.setGeometry(QgsGeometry::fromWkt("POINT(2 2)"));
    QgsFeatureList pt2List = {pt2};
    REQUIRE(pointLayer2->dataProvider()->addFeatures(pt2List));

    QVariantMap params2;
    QVariantList mapLayers2 = {
        QVariant::fromValue(static_cast<QgsMapLayer *>(pointLayer.get())),
        QVariant::fromValue(static_cast<QgsMapLayer *>(pointLayer2.get()))
    };
    params2[QStringLiteral("INPUT_LAYERS")] = mapLayers2;
    params2[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");
    bool ok2 = false;
    QVariantMap res2 = alg->run(params2, context, &feedback, &ok2);
    REQUIRE(ok2);
    auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res2[QStringLiteral("OUTPUT")].toString()));
    REQUIRE(outLayer != nullptr);
    CHECK(outLayer->featureCount() == 2);
}

TEST_CASE("VectorDissolveAlgorithm unions groups once and keeps every group (#1056)",
          "[processing][vector][dissolve]") {
    // Two adjacent squares share the edge x=1; dissolving by the group field
    // must yield ONE feature per group with the correct union geometry. The
    // historical implementation re-copied the accumulated geometry on every
    // feature (O(group²)); the refactor collects parts and unions once.
    std::unique_ptr<QgsVectorLayer> layer(new QgsVectorLayer(
        "Polygon?crs=EPSG:4326&field=grp:string", "dissolve_in", "memory"));
    REQUIRE(layer->isValid());
    QgsFeatureList feats;
    const char *groups[3] = {"a", "a", "b"};
    const char *wkts[3] = {
        "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))",
        "POLYGON((1 0, 2 0, 2 1, 1 1, 1 0))",
        "POLYGON((5 5, 6 5, 6 6, 5 6, 5 5))"
    };
    for (int i = 0; i < 3; ++i) {
        QgsFeature f(layer->fields());
        f.setAttribute("grp", QString::fromUtf8(groups[i]));
        f.setGeometry(QgsGeometry::fromWkt(wkts[i]));
        feats.append(f);
    }
    REQUIRE(layer->dataProvider()->addFeatures(feats));

    VectorDissolveAlgorithm proto;
    std::unique_ptr<QgsProcessingAlgorithm> alg(proto.create());

    QgsProcessingContext context;
    QgsProcessingFeedback feedback;
    QVariantMap params;
    params[QStringLiteral("INPUT")] = QVariant::fromValue(static_cast<QgsMapLayer *>(layer.get()));
    params[QStringLiteral("FIELD")] = QStringLiteral("grp");
    params[QStringLiteral("OUTPUT")] = QStringLiteral("memory:");

    bool ok = false;
    QVariantMap res = alg->run(params, context, &feedback, &ok);
    REQUIRE(ok);
    auto *outLayer = qobject_cast<QgsVectorLayer *>(context.getMapLayer(res[QStringLiteral("OUTPUT")].toString()));
    REQUIRE(outLayer != nullptr);
    // Group "a" (two adjacent squares) and group "b" (one square).
    REQUIRE(outLayer->featureCount() == 2);

    double areaA = -1.0;
    double areaB = -1.0;
    QgsFeatureIterator it = outLayer->getFeatures();
    QgsFeature of;
    while (it.nextFeature(of)) {
        const double area = of.geometry().area();
        if (of.attribute("grp").toString() == QStringLiteral("a"))
            areaA = area;
        else
            areaB = area;
    }
    CHECK_THAT(areaA, Catch::Matchers::WithinAbs(2.0, 1e-6));
    CHECK_THAT(areaB, Catch::Matchers::WithinAbs(1.0, 1e-6));
}
