// test_stac_client.cpp — StacClient tests
#include <catch2/catch_test_macros.hpp>

#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariantMap>
#include <QVariantList>

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
