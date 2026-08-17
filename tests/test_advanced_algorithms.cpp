// Advanced RS Algorithms tests — verify provider loading and algorithm counts
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <processing/providers/otb_tools/provider.h>
#include <processing/providers/qgis_algorithms/provider.h>

TEST_CASE("OTB provider loads successfully", "[processing][otb]") {
    OtbToolsProvider provider;
    provider.load();

    SECTION("Provider is loaded") {
        CHECK(provider.id() == "otb_tools");
    }

    SECTION("Has algorithms") {
        CHECK(provider.algorithms().size() > 0);
    }

    SECTION("Has minimum expected algorithms") {
        CHECK(provider.algorithms().size() >= 25);
    }
}

TEST_CASE("QGIS provider loads successfully", "[processing][qgis]") {
    QgisAlgorithmsProvider provider;
    provider.load();

    SECTION("Provider is loaded") {
        CHECK(provider.id() == "qgis_algorithms");
    }

    SECTION("Has algorithms") {
        CHECK(provider.algorithms().size() > 0);
    }

    SECTION("Has minimum expected algorithms") {
        CHECK(provider.algorithms().size() >= 20);
    }
}

TEST_CASE("OTB algorithms have proper metadata", "[processing][otb][metadata]") {
    OtbToolsProvider provider;
    provider.load();

    SECTION("All algorithms have non-empty names") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->name().isEmpty());
        }
    }

    SECTION("All algorithms have non-empty display names") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->displayName().isEmpty());
        }
    }

    SECTION("All algorithms have groups") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->group().isEmpty());
        }
    }
}

TEST_CASE("QGIS algorithms have proper metadata", "[processing][qgis][metadata]") {
    QgisAlgorithmsProvider provider;
    provider.load();

    SECTION("All algorithms have non-empty names") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->name().isEmpty());
        }
    }

    SECTION("All algorithms have non-empty display names") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->displayName().isEmpty());
        }
    }

    SECTION("All algorithms have groups") {
        for (const auto *alg : provider.algorithms()) {
            CHECK_FALSE(alg->group().isEmpty());
        }
    }

    SECTION("ExtractByAttribute algorithm defines OPERATOR and parameter definitions") {
        const auto *alg = provider.algorithm( "extractbyattribute" );
        REQUIRE( alg != nullptr );
        REQUIRE( alg->parameterDefinition( "INPUT" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "FIELD" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "OPERATOR" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "VALUE" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "OUTPUT" ) != nullptr );
    }

    SECTION("VectorDistanceMatrix algorithm defines parameters") {
        const auto *alg = provider.algorithm( "vector_distance_matrix" );
        REQUIRE( alg != nullptr );
        REQUIRE( alg->parameterDefinition( "INPUT" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "TARGET_LAYER" ) != nullptr );
        REQUIRE( alg->parameterDefinition( "OUTPUT_TYPE" ) != nullptr );
    }
}

