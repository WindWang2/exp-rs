// tests/test_algorithm_schema.cpp
#include <catch2/catch_test_macros.hpp>
#include <QJsonObject>
#include <QJsonDocument>

#include "processing/providers/qgis_algorithms/algorithms/remote_sensing/band_math_algorithm.h"
#include "processing/providers/qgis_algorithms/algorithms/remote_sensing/spectral_index_algorithm.h"
#include "processing/providers/qgis_algorithms/algorithms/remote_sensing/atmospheric_correction_algorithm.h"

TEST_CASE("Algorithm toJsonSchema and metadata tests", "[agent][schema]") {
    SECTION("BandMathAlgorithm schema and metadata") {
        BandMathAlgorithm alg;
        alg.initAlgorithm(); // Initialize parameter definitions

        QVariantMap schema = alg.toJsonSchema();
        REQUIRE_FALSE(schema.isEmpty());

        CHECK(schema.value("title").toString() == "rs_band_math");
        CHECK(schema.value("type").toString() == "object");

        QVariantMap properties = schema.value("properties").toMap();
        REQUIRE_FALSE(properties.isEmpty());

        REQUIRE(properties.contains("INPUT_LAYERS"));
        QVariantMap inputLayers = properties.value("INPUT_LAYERS").toMap();
        CHECK(inputLayers.value("type").toString() == "string");
        CHECK_FALSE(inputLayers.value("title").toString().isEmpty());

        REQUIRE(properties.contains("EXPRESSION"));
        QVariantMap expr = properties.value("EXPRESSION").toMap();
        CHECK(expr.value("type").toString() == "string");

        // Required parameters
        QStringList required = schema.value("required").toStringList();
        CHECK(required.contains("INPUT_LAYERS"));
        CHECK(required.contains("EXPRESSION"));

        // Metadata check
        QVariantMap meta = alg.metadata();
        CHECK(meta.contains("purpose"));
        CHECK(meta.contains("useCases"));
        CHECK(meta.contains("prerequisites"));
    }

    SECTION("SpectralIndexAlgorithm schema and metadata") {
        SpectralIndexAlgorithm alg;
        alg.initAlgorithm();

        QVariantMap schema = alg.toJsonSchema();
        REQUIRE_FALSE(schema.isEmpty());

        CHECK(schema.value("title").toString() == "rs_spectral_index");

        QVariantMap properties = schema.value("properties").toMap();
        REQUIRE(properties.contains("INDEX"));
        QVariantMap indexType = properties.value("INDEX").toMap();
        
        // INDEX is an Enum
        CHECK(indexType.value("type").toString() == "string");
        CHECK(indexType.contains("enum"));
        QStringList options = indexType.value("enum").toStringList();
        REQUIRE_FALSE(options.isEmpty());
        CHECK(options.contains("NDVI"));

        // Metadata check
        QVariantMap meta = alg.metadata();
        CHECK(meta.contains("purpose"));
        CHECK(alg.shortHelpString().contains("NDVI"));
    }

    SECTION("AtmosphericCorrectionAlgorithm schema and metadata") {
        AtmosphericCorrectionAlgorithm alg;
        alg.initAlgorithm();

        QVariantMap schema = alg.toJsonSchema();
        REQUIRE_FALSE(schema.isEmpty());
        CHECK(schema.value("title").toString() == "rs_atmospheric_correction");

        QVariantMap meta = alg.metadata();
        CHECK(meta.value("purpose").toString().contains("atmospheric"));
    }
}
