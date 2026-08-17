// tests/test_vector_overlay_crs_transform.cpp
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsgeometry.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsprocessingcontext.h>
#include <qgsprocessingfeedback.h>
#include <qgsexception.h>

#include "processing/providers/qgis_algorithms/algorithms/native/native_difference.h"
#include "processing/providers/qgis_algorithms/algorithms/native/native_intersection.h"
#include "processing/providers/qgis_algorithms/algorithms/native/native_union.h"
#include "processing/providers/qgis_algorithms/algorithms/vector/vector_difference.h"
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
