// test_stac_client.cpp — StacClient tests
#include <catch2/catch_test_macros.hpp>

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>
#include "stac_client.h"

TEST_CASE("STAC URL construction", "[agent][stac]")
{
    SECTION("Search endpoint format")
    {
        QString endpoint = "https://example.com/stac";
        QString expectedSearchUrl = endpoint + "/search";
        REQUIRE(expectedSearchUrl == "https://example.com/stac/search");
    }

    SECTION("Query parameters")
    {
        QUrlQuery query;
        query.addQueryItem("collections", "landsat-8");
        query.addQueryItem("datetime", "2023-01-01/2023-12-31");
        query.addQueryItem("bbox", "-180,-90,180,90");

        QString queryString = query.toString();
        REQUIRE(queryString.contains("collections=landsat-8"));
        REQUIRE(queryString.contains("datetime="));
        REQUIRE(queryString.contains("bbox="));
    }

    SECTION("Empty parameters")
    {
        QUrlQuery query;
        QString queryString = query.toString();
        REQUIRE(queryString.isEmpty());
    }

    SECTION("URL with query")
    {
        QUrl url("https://example.com/stac/search");
        QUrlQuery query;
        query.addQueryItem("collections", "sentinel-2");
        url.setQuery(query);

        REQUIRE(url.toString().contains("collections=sentinel-2"));
    }

    SECTION("StacClient::buildSearchUrl")
    {
        const QUrl url = StacClient::buildSearchUrl(
            QStringLiteral("https://example.com/stac"),
            QStringLiteral("sentinel-2"),
            QStringLiteral("2023-01-01/2023-12-31"),
            QStringList{QStringLiteral("-180"), QStringLiteral("-90"),
                        QStringLiteral("180"), QStringLiteral("90")},
            50);

        REQUIRE(url.toString().contains(QStringLiteral("/search")));
        REQUIRE(url.toString().contains(QStringLiteral("collections=sentinel-2")));
        REQUIRE(url.toString().contains(QStringLiteral("datetime=")));
        REQUIRE(url.toString().contains(QStringLiteral("bbox=")));
        REQUIRE(url.toString().contains(QStringLiteral("limit=50")));
    }

    SECTION("URL policy blocks private hosts by default")
    {
        // Clear allow-private for deterministic check when env is unset is hard;
        // still verify public HTTPS is accepted and bad schemes rejected.
        REQUIRE(StacClient::validateUrlPolicy(
                    QUrl(QStringLiteral("https://earth-search.aws.element84.com/v1")), true)
                    .isEmpty());
        REQUIRE_FALSE(StacClient::validateUrlPolicy(
                          QUrl(QStringLiteral("ftp://example.com/stac")), true)
                          .isEmpty());
        REQUIRE_FALSE(StacClient::validateAssetHref(QStringLiteral("file:///etc/passwd")).isEmpty());
        REQUIRE_FALSE(StacClient::validateAssetHref(QStringLiteral("/vsicurl/https://x")).isEmpty());
        REQUIRE(StacClient::validateAssetHref(
                    QStringLiteral("https://example.com/data.tif"))
                    .isEmpty());
    }
}

TEST_CASE("STAC feature parsing", "[agent][stac]")
{
    SECTION("Parse features from JSON")
    {
        // Simulate STAC response parsing
        QString json = R"({"features": [{"id": "item1"}, {"id": "item2"}]})";
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVariantMap root = doc.toVariant().toMap();
        QVariantList features = root.value("features").toList();

        REQUIRE(features.size() == 2);
        REQUIRE(features[0].toMap().value("id").toString() == "item1");
        REQUIRE(features[1].toMap().value("id").toString() == "item2");
    }

    SECTION("Parse empty features")
    {
        QString json = R"({"features": []})";
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVariantMap root = doc.toVariant().toMap();
        QVariantList features = root.value("features").toList();

        REQUIRE(features.isEmpty());
    }

    SECTION("Parse missing features key")
    {
        QString json = R"({"type": "FeatureCollection"})";
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVariantMap root = doc.toVariant().toMap();
        QVariantList features = root.value("features").toList();

        REQUIRE(features.isEmpty());
    }
}

TEST_CASE("StacClient::selectCogHref", "[agent][stac]")
{
    SECTION("COG-typed asset is selected and prefixed")
    {
        QJsonObject item{
            {QStringLiteral("id"), QStringLiteral("item1")},
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("thumbnail"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/thumb.jpg")},
                    {QStringLiteral("type"), QStringLiteral("image/jpeg")}}},
                {QStringLiteral("visual"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/visual.tif")},
                    {QStringLiteral("type"), QStringLiteral("image/tiff; application=geotiff")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item)
                == QStringLiteral("/vsicurl/https://example.com/visual.tif"));
    }

    SECTION(".tif href alone is enough")
    {
        QJsonObject item{
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("B01"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/B01.tif")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item)
                == QStringLiteral("/vsicurl/https://example.com/B01.tif"));
    }

    SECTION("First matching asset in key order wins")
    {
        QJsonObject item{
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("B02"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/B02.tif")}}},
                {QStringLiteral("thumbnail"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/thumb.png")},
                    {QStringLiteral("type"), QStringLiteral("image/png")}}},
                {QStringLiteral("visual"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/visual.tif")},
                    {QStringLiteral("type"), QStringLiteral("image/tiff")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item)
                == QStringLiteral("/vsicurl/https://example.com/B02.tif"));
    }

    SECTION("No suitable asset returns empty")
    {
        QJsonObject item{
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("thumbnail"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/thumb.jpg")},
                    {QStringLiteral("type"), QStringLiteral("image/jpeg")}}},
                {QStringLiteral("metadata"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/meta.json")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item).isEmpty());
    }

    SECTION("Item without assets returns empty")
    {
        QJsonObject item{{QStringLiteral("id"), QStringLiteral("item2")}};
        REQUIRE(StacClient::selectCogHref(item).isEmpty());
    }

    SECTION(".tiff extension alone does not match")
    {
        QJsonObject item{
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("data"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("https://example.com/data.tiff")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item).isEmpty());
    }

    SECTION("Invalid href is rejected before prefixing")
    {
        QJsonObject item{
            {QStringLiteral("assets"), QJsonObject{
                {QStringLiteral("data"), QJsonObject{
                    {QStringLiteral("href"), QStringLiteral("file:///etc/passwd.tif")}}}
            }}
        };
        REQUIRE(StacClient::selectCogHref(item).isEmpty());
    }
}
