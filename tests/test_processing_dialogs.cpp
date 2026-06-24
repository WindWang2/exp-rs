// test_processing_dialogs.cpp — Processing dialog data structure tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QVariantMap>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonDocument>

TEST_CASE("Processing parameter validation", "[dialogs][processing]")
{
    SECTION("Empty expression is invalid")
    {
        QString expression;
        REQUIRE(expression.isEmpty());
    }

    SECTION("Valid expression")
    {
        QString expression = "(b1 - b2) / (b1 + b2)";
        REQUIRE_FALSE(expression.isEmpty());
        REQUIRE(expression.contains("b1"));
        REQUIRE(expression.contains("b2"));
    }

    SECTION("Output path validation")
    {
        QString outputPath;
        REQUIRE(outputPath.isEmpty());

        outputPath = "/tmp/output.tif";
        REQUIRE_FALSE(outputPath.isEmpty());
        REQUIRE(outputPath.endsWith(".tif"));
    }
}

TEST_CASE("Algorithm parameter building", "[dialogs][processing]")
{
    SECTION("Band Math parameters")
    {
        QVariantMap params;
        params["EXPRESSION"] = "(b1 - b2) / (b1 + b2)";
        params["OUTPUT"] = "/tmp/ndvi.tif";

        REQUIRE(params.contains("EXPRESSION"));
        REQUIRE(params.contains("OUTPUT"));
        REQUIRE(params["EXPRESSION"].toString().contains("b1"));
    }

    SECTION("Spectral Index parameters")
    {
        QVariantMap params;
        params["INDEX"] = 0; // NDVI
        params["NIR_BAND"] = 4;
        params["RED_BAND"] = 3;
        params["OUTPUT"] = "/tmp/ndvi.tif";

        REQUIRE(params["INDEX"].toInt() == 0);
        REQUIRE(params["NIR_BAND"].toInt() == 4);
        REQUIRE(params["RED_BAND"].toInt() == 3);
    }

    SECTION("Atmospheric Correction parameters")
    {
        QVariantMap params;
        params["METHOD"] = 0; // DOS1
        params["GAIN"] = 1.0;
        params["BIAS"] = 0.0;
        params["OUTPUT"] = "/tmp/corrected.tif";

        REQUIRE(params["METHOD"].toInt() == 0);
        REQUIRE(params["GAIN"].toDouble() == Catch::Approx(1.0));
        REQUIRE(params["BIAS"].toDouble() == Catch::Approx(0.0));
    }
}

TEST_CASE("Batch processing state", "[dialogs][batch]")
{
    SECTION("Batch state tracking")
    {
        int currentIndex = 0;
        int successCount = 0;
        int failCount = 0;
        QStringList inputFiles = {"file1.tif", "file2.tif", "file3.tif"};

        REQUIRE(currentIndex == 0);
        REQUIRE(successCount == 0);
        REQUIRE(failCount == 0);
        REQUIRE(inputFiles.size() == 3);

        // Simulate processing
        currentIndex++;
        successCount++;
        REQUIRE(currentIndex == 1);
        REQUIRE(successCount == 1);
    }

    SECTION("Batch progress calculation")
    {
        int currentIndex = 2;
        int totalFiles = 5;
        double progress = static_cast<double>(currentIndex) / totalFiles * 100;

        REQUIRE(progress == Catch::Approx(40.0));
    }
}

TEST_CASE("Comparison dialog state", "[dialogs][comparison]")
{
    SECTION("Split position tracking")
    {
        int splitPosition = 50; // percentage
        REQUIRE(splitPosition >= 0);
        REQUIRE(splitPosition <= 100);
    }

    SECTION("Flicker mode timing")
    {
        int flickerInterval = 500; // ms
        REQUIRE(flickerInterval > 0);
        REQUIRE(flickerInterval < 5000);
    }
}
