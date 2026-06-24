// test_histogram_widget.cpp — HistogramWidget data structure tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QVector>
#include <QString>

TEST_CASE("Histogram data structures", "[widget][histogram]")
{
    SECTION("Histogram bin storage")
    {
        QVector<double> histogram(256, 0.0);
        REQUIRE(histogram.size() == 256);

        // Simulate histogram data
        histogram[0] = 100.0;
        histogram[128] = 500.0;
        histogram[255] = 50.0;

        REQUIRE(histogram[0] == 100.0);
        REQUIRE(histogram[128] == 500.0);
        REQUIRE(histogram[255] == 50.0);
    }

    SECTION("Band statistics structure")
    {
        struct BandStats {
            double min = 0.0;
            double max = 0.0;
            double mean = 0.0;
            double stddev = 0.0;
            bool valid = false;
        };

        BandStats stats;
        REQUIRE(stats.min == 0.0);
        REQUIRE(stats.max == 0.0);
        REQUIRE(stats.mean == 0.0);
        REQUIRE(stats.stddev == 0.0);
        REQUIRE(stats.valid == false);

        stats.min = 10.0;
        stats.max = 200.0;
        stats.mean = 100.0;
        stats.stddev = 25.0;
        stats.valid = true;

        REQUIRE(stats.valid);
        REQUIRE(stats.max > stats.min);
    }
}

TEST_CASE("Histogram normalization", "[widget][histogram]")
{
    SECTION("Find max frequency")
    {
        QVector<double> histogram = {10.0, 50.0, 100.0, 200.0, 50.0};
        double maxFreq = 0.0;
        for (double v : histogram) {
            if (v > maxFreq) maxFreq = v;
        }
        REQUIRE(maxFreq == 200.0);
    }

    SECTION("Normalize to 0-1 range")
    {
        QVector<double> histogram = {10.0, 50.0, 100.0, 200.0, 50.0};
        double maxFreq = 200.0;
        QVector<double> normalized(histogram.size());
        for (int i = 0; i < histogram.size(); i++) {
            normalized[i] = histogram[i] / maxFreq;
        }
        REQUIRE(normalized[0] == Catch::Approx(0.05));
        REQUIRE(normalized[3] == Catch::Approx(1.0));
    }
}

TEST_CASE("Histogram bin range calculation", "[widget][histogram]")
{
    SECTION("Calculate bin width")
    {
        double minVal = 0.0;
        double maxVal = 255.0;
        int binCount = 256;
        double binWidth = (maxVal - minVal) / binCount;
        // binWidth = 255/256 ≈ 0.996
        REQUIRE(binWidth > 0.99);
        REQUIRE(binWidth < 1.01);
    }

    SECTION("Calculate bin index")
    {
        double minVal = 0.0;
        double binWidth = 255.0 / 256.0;
        double value = 128.0;
        int binIndex = static_cast<int>((value - minVal) / binWidth);
        REQUIRE(binIndex >= 127);
        REQUIRE(binIndex <= 129);
    }

    SECTION("Clamp bin index")
    {
        int binCount = 256;
        int binIndex = 300; // Out of range
        binIndex = std::clamp(binIndex, 0, binCount - 1);
        REQUIRE(binIndex == 255);
    }
}
