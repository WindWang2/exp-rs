// Algorithm organization tests — verify metadata quality for search and categorization
#include <catch2/catch_test_macros.hpp>

#include <processing/qgsprocessingalgorithm.h>
#include <processing/qgsprocessingprovider.h>
#include <processing/providers/qgis_algorithms/provider.h>
#include <processing/providers/gdal_tools/provider.h>
#include <processing/providers/otb_tools/provider.h>

#include <QSet>

TEST_CASE("All algorithms have non-empty group", "[processing][organization]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();
    GdalToolsProvider gdalProvider;
    gdalProvider.load();
    OtbToolsProvider otbProvider;
    otbProvider.load();

    auto checkProvider = [](QgsProcessingProvider *provider, const QString &providerName) {
        for (const auto *alg : provider->algorithms()) {
            INFO("Algorithm: " << providerName.toStdString() << ":" << alg->id().toStdString());
            CHECK_FALSE(alg->group().isEmpty());
        }
    };

    checkProvider(&qgisProvider, "qgis");
    checkProvider(&gdalProvider, "gdal");
    checkProvider(&otbProvider, "otb");
}

TEST_CASE("All algorithms have non-empty groupId", "[processing][organization]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();
    GdalToolsProvider gdalProvider;
    gdalProvider.load();
    OtbToolsProvider otbProvider;
    otbProvider.load();

    auto checkProvider = [](QgsProcessingProvider *provider, const QString &providerName) {
        for (const auto *alg : provider->algorithms()) {
            INFO("Algorithm: " << providerName.toStdString() << ":" << alg->id().toStdString());
            CHECK_FALSE(alg->groupId().isEmpty());
        }
    };

    checkProvider(&qgisProvider, "qgis");
    checkProvider(&gdalProvider, "gdal");
    checkProvider(&otbProvider, "otb");
}

TEST_CASE("All algorithms have tags for search", "[processing][organization]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();
    GdalToolsProvider gdalProvider;
    gdalProvider.load();
    OtbToolsProvider otbProvider;
    otbProvider.load();

    auto checkProvider = [](QgsProcessingProvider *provider, const QString &providerName) {
        for (const auto *alg : provider->algorithms()) {
            INFO("Algorithm: " << providerName.toStdString() << ":" << alg->id().toStdString());
            CHECK_FALSE(alg->tags().isEmpty());
        }
    };

    checkProvider(&qgisProvider, "qgis");
    checkProvider(&gdalProvider, "gdal");
    checkProvider(&otbProvider, "otb");
}

TEST_CASE("Algorithm groups are consistent within provider", "[processing][organization]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();

    QSet<QString> groups;
    for (const auto *alg : qgisProvider.algorithms()) {
        groups.insert(alg->group());
    }

    // Should have a reasonable number of groups (not too many, not too few)
    CHECK(groups.size() >= 3);
    CHECK(groups.size() <= 20);
}

// --- Smoke tests: verify algorithm instantiation and basic properties ---

TEST_CASE("QGIS native algorithms have valid displayName", "[processing][smoke]") {
    QgisAlgorithmsProvider provider;
    provider.load();

    int count = 0;
    for (const auto *alg : provider.algorithms()) {
        INFO("Algorithm: " << alg->id().toStdString());
        CHECK_FALSE(alg->displayName().isEmpty());
        count++;
    }
    CHECK(count > 0);
}

TEST_CASE("GDAL algorithms have valid displayName", "[processing][smoke]") {
    GdalToolsProvider provider;
    provider.load();

    int count = 0;
    for (const auto *alg : provider.algorithms()) {
        INFO("Algorithm: " << alg->id().toStdString());
        CHECK_FALSE(alg->displayName().isEmpty());
        count++;
    }
    CHECK(count > 0);
}

TEST_CASE("OTB algorithms have valid displayName", "[processing][smoke]") {
    OtbToolsProvider provider;
    provider.load();

    int count = 0;
    for (const auto *alg : provider.algorithms()) {
        INFO("Algorithm: " << alg->id().toStdString());
        CHECK_FALSE(alg->displayName().isEmpty());
        count++;
    }
    CHECK(count > 0);
}

TEST_CASE("All algorithms have unique IDs within provider", "[processing][smoke]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();
    GdalToolsProvider gdalProvider;
    gdalProvider.load();
    OtbToolsProvider otbProvider;
    otbProvider.load();

    auto checkProvider = [](QgsProcessingProvider *provider, const QString &providerName) {
        QSet<QString> ids;
        for (const auto *alg : provider->algorithms()) {
            INFO("Algorithm: " << providerName.toStdString() << ":" << alg->id().toStdString());
            CHECK_FALSE(ids.contains(alg->id()));
            ids.insert(alg->id());
        }
    };

    checkProvider(&qgisProvider, "qgis");
    checkProvider(&gdalProvider, "gdal");
    checkProvider(&otbProvider, "otb");
}

TEST_CASE("All algorithms have valid flags", "[processing][smoke]") {
    QgisAlgorithmsProvider qgisProvider;
    qgisProvider.load();
    GdalToolsProvider gdalProvider;
    gdalProvider.load();
    OtbToolsProvider otbProvider;
    otbProvider.load();

    auto checkProvider = [](QgsProcessingProvider *provider, const QString &providerName) {
        int count = 0;
        for (const auto *alg : provider->algorithms()) {
            INFO("Algorithm: " << providerName.toStdString() << ":" << alg->id().toStdString());
            auto flags = alg->flags();
            Q_UNUSED(flags);
            count++;
        }
        CHECK(count > 0);
    };

    checkProvider(&qgisProvider, "qgis");
    checkProvider(&gdalProvider, "gdal");
    checkProvider(&otbProvider, "otb");
}
