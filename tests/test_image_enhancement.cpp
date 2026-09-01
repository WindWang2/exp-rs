#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/chunked_processor.h"
#include <QThread>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>

using namespace Catch;

TEST_CASE("Linear min-max stretch", "[enhancement]") {
    std::vector<float> input = {0, 25, 50, 75, 100};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 0.0f, 100.0f);
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[2] == Approx(127.5f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Percentage clip stretch", "[enhancement]") {
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++) input[i] = static_cast<float>(i);
    std::vector<float> output(100);
    ImageEnhancement::percentClipStretch(input.data(), output.data(), 100, 5.0f);
    REQUIRE(output[5] == Approx(0.0f).margin(1.0f));
    REQUIRE(output[94] == Approx(255.0f).margin(1.0f));
}

TEST_CASE("Standard deviation stretch", "[enhancement]") {
    std::vector<float> input = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<float> output(10);
    ImageEnhancement::stddevStretch(input.data(), output.data(), 10, 2.0f);
    REQUIRE(output[0] >= 0.0f);
    REQUIRE(output[9] <= 255.0f);
}

TEST_CASE("Histogram equalization", "[enhancement]") {
    std::vector<float> input = {1, 1, 1, 1, 1, 2, 2, 3, 5, 10};
    std::vector<float> output(10);
    ImageEnhancement::histogramEqualize(input.data(), output.data(), 10, 256);
    REQUIRE(output[0] < output[9]);
    REQUIRE(output[0] == output[1]);
    REQUIRE(output[1] == output[4]);
}

TEST_CASE("Contrast stretch preserves nodata", "[enhancement]") {
    float nodata = -9999.0f;
    std::vector<float> input = {10, 20, -9999, 30, 40};
    std::vector<float> output(5);
    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 10.0f, 40.0f, nodata);
    REQUIRE(output[2] == Approx(nodata));
    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Lee filter excludes +-Inf pixels from local statistics", "[enhancement]") {
    // A single +Inf must not poison the summed-area table: before the
    // isfinite() guard every window whose rectangle contained the cell
    // produced Inf/NaN local statistics (#634).
    constexpr int W = 10, H = 10;
    std::vector<float> input(W * H, 1.0f);
    input[5 * W + 5] = std::numeric_limits<float>::infinity();
    std::vector<float> output(W * H, 0.0f);
    ImageEnhancement::leeFilter(input.data(), output.data(), W, H, 3, 0.5f);
    for (float v : output)
        REQUIRE(std::isfinite(v));
}

TEST_CASE("Speckle filters reject rasters beyond the integral-image limit", "[enhancement][integral-image]") {
    // #691: width*height > INT32_MAX used to overflow the all-int32 index
    // math and the per-pixel valid-count vector. The guard must fail loudly
    // (log + untouched output) BEFORE touching the buffers or allocating, so
    // 1-element buffers are enough to exercise it.
    constexpr int W = 50000;
    constexpr int H = 50000; // 2.5e9 pixels > INT32_MAX
    float input = 1.0f;

    float output = -777.0f;
    ImageEnhancement::leeFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::enhancedLeeFilter(&input, &output, W, H, 3, 0.5f, 1.0f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::frostFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::kuanFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);

    output = -777.0f;
    ImageEnhancement::gammaMapFilter(&input, &output, W, H, 3, 0.5f);
    REQUIRE(output == -777.0f);
}

TEST_CASE("ChunkedProcessor respects the maxThreads cap", "[enhancement][chunked]") {
    // #692: process() fans out on a dedicated (non-global) pool bounded by the
    // nested-parallelism token. maxThreads=1 must execute all chunks on a
    // single thread instead of one thread per core.
    constexpr int W = 8;
    constexpr int H = 600; // 3 chunks at the default 256-row chunk height
    ChunkedProcessor processor(W, H, 0);
    REQUIRE(processor.chunkCount() > 1);

    std::mutex threadsMutex;
    std::set<const QThread *> threads;
    std::atomic<int> executed{0};
    auto recordThread = [&](const ChunkedProcessor::Chunk &chunk) {
        {
            std::lock_guard<std::mutex> lock(threadsMutex);
            threads.insert(QThread::currentThread());
        }
        ++executed;
        return chunk.endRow > chunk.startRow;
    };

    REQUIRE(processor.process(recordThread, nullptr, 1));
    REQUIRE(executed.load() == processor.chunkCount());
    REQUIRE(threads.size() == 1);

    // Default token: all chunks still complete; observed concurrency never
    // exceeds the documented auto budget (cores / 4, at least 1).
    executed.store(0);
    threads.clear();
    REQUIRE(processor.process(recordThread));
    REQUIRE(executed.load() == processor.chunkCount());
    REQUIRE(threads.size() >= 1);
    REQUIRE(threads.size() <= static_cast<size_t>(ChunkedProcessor::defaultMaxThreads()));
    REQUIRE(ChunkedProcessor::defaultMaxThreads() >= 1);
}
