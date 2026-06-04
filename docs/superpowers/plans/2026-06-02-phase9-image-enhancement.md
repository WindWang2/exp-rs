# Phase 9: Image Enhancement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement image enhancement algorithms (contrast stretching, spatial filtering, PCA, band ratio) — the most critical gap for undergraduate RS education.

**Architecture:** Pure C++ algorithms in `src/processing/algorithms/` using GDAL block I/O. Each algorithm operates on float arrays (same pattern as existing `spectral_indices.cpp`, `band_math.cpp`). GUI dialogs in `src/app/dialogs/` following the same pattern as `BandMathDialog`. Menu wiring in `main_window.cpp` under Raster > Enhancement.

**Tech Stack:** GDAL C API (block read/write), Qt6 (dialogs), Catch2 (tests), existing `GdalDatasetWrapper`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/processing/algorithms/image_enhancement.h` | Create | ImageEnhancement class declaration |
| `src/processing/algorithms/image_enhancement.cpp` | Create | Contrast stretching, spatial filtering, PCA, band ratio implementations |
| `src/app/dialogs/contrast_stretch_dialog.h/.cpp` | Create | Contrast stretch GUI |
| `src/app/dialogs/spatial_filter_dialog.h/.cpp` | Create | Spatial filter GUI |
| `src/app/dialogs/pca_dialog.h/.cpp` | Create | PCA GUI |
| `src/app/dialogs/band_ratio_dialog.h/.cpp` | Create | Band ratio / IHS GUI |
| `src/app/main_window.cpp` | Modify | Add Raster > Enhancement menu items |
| `src/app/CMakeLists.txt` | Modify | Add new source files |
| `tests/test_image_enhancement.cpp` | Create | Unit tests for all enhancement algorithms |
| `tests/test_spatial_filter.cpp` | Create | Unit tests for convolution |
| `tests/test_pca.cpp` | Create | Unit tests for PCA |

---

### Task 1: Contrast Stretching

**Goal:** Linear and histogram-based contrast enhancement.

**Algorithms:**
- Linear min-max stretch: `output = (input - min) / (max - min) * 255`
- Percentage clip stretch: clip N% tails, then linear stretch
- Standard deviation stretch: stretch to mean ± K*stddev
- Histogram equalization: cumulative distribution function mapping

**Files:**
- Create: `src/processing/algorithms/image_enhancement.h/.cpp`
- Modify: `src/app/CMakeLists.txt`
- Create: `tests/test_image_enhancement.cpp`

- [ ] **Step 1: Write failing tests**

Create `tests/test_image_enhancement.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>
#include <cmath>
#include <algorithm>

using namespace Catch;

TEST_CASE("Linear min-max stretch", "[enhancement]") {
    // Input: values 0-100, output should be 0-255
    std::vector<float> input = {0, 25, 50, 75, 100};
    std::vector<float> output(5);

    ImageEnhancement::linearStretch(input.data(), output.data(), 5, 0.0f, 100.0f);

    REQUIRE(output[0] == Approx(0.0f));
    REQUIRE(output[2] == Approx(127.5f));
    REQUIRE(output[4] == Approx(255.0f));
}

TEST_CASE("Percentage clip stretch", "[enhancement]") {
    // 100 values 0-99, clip 5% each end
    std::vector<float> input(100);
    for (int i = 0; i < 100; i++) input[i] = static_cast<float>(i);
    std::vector<float> output(100);

    ImageEnhancement::percentClipStretch(input.data(), output.data(), 100, 5.0f);

    // After clipping 5%, range becomes 5-94
    // Value at index 5 should map to ~0, value at index 94 should map to ~255
    REQUIRE(output[5] == Approx(0.0f).margin(1.0f));
    REQUIRE(output[94] == Approx(255.0f).margin(1.0f));
}

TEST_CASE("Standard deviation stretch", "[enhancement]") {
    // Data with known mean and stddev
    std::vector<float> input = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    std::vector<float> output(10);

    ImageEnhancement::stddevStretch(input.data(), output.data(), 10, 2.0f);

    // Mean=55, stddev~=28.72, range = [55-57.44, 55+57.44] = [-2.44, 112.44]
    // Values should be stretched to 0-255
    REQUIRE(output[0] >= 0.0f);
    REQUIRE(output[9] <= 255.0f);
}

TEST_CASE("Histogram equalization", "[enhancement]") {
    // Highly skewed data (most values low)
    std::vector<float> input = {1, 1, 1, 1, 1, 2, 2, 3, 5, 10};
    std::vector<float> output(10);

    ImageEnhancement::histogramEqualize(input.data(), output.data(), 10, 256);

    // After equalization, values should be more spread out
    REQUIRE(output[0] < output[9]);
    // The five "1" values should map to the same output
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
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cd build && make test_image_enhancement && ./tests/test_image_enhancement
```

Expected: FAIL — ImageEnhancement not defined

- [ ] **Step 3: Implement ImageEnhancement class**

Create `src/processing/algorithms/image_enhancement.h`:

```cpp
#ifndef IMAGE_ENHANCEMENT_H
#define IMAGE_ENHANCEMENT_H

#include <vector>
#include <cstddef>

class ImageEnhancement
{
public:
    // Linear min-max stretch: maps [minVal, maxVal] → [0, 255]
    static void linearStretch(const float *input, float *output, size_t count,
                              float minVal, float maxVal, float nodata = -9999.0f);

    // Percentage clip stretch: clips pct% from each tail, then linear stretch
    static void percentClipStretch(const float *input, float *output, size_t count,
                                   float pct = 2.0f, float nodata = -9999.0f);

    // Standard deviation stretch: maps [mean - k*stddev, mean + k*stddev] → [0, 255]
    static void stddevStretch(const float *input, float *output, size_t count,
                              float k = 2.0f, float nodata = -9999.0f);

    // Histogram equalization using CDF mapping
    static void histogramEqualize(const float *input, float *output, size_t count,
                                  int bins = 256, float nodata = -9999.0f);

private:
    static void computeStats(const float *data, size_t count, float nodata,
                             float &min, float &max, float &mean, float &stddev);
};

#endif // IMAGE_ENHANCEMENT_H
```

Create `src/processing/algorithms/image_enhancement.cpp`:

```cpp
#include "image_enhancement.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

void ImageEnhancement::computeStats(const float *data, size_t count, float nodata,
                                    float &min, float &max, float &mean, float &stddev)
{
    min = 1e30f;
    max = -1e30f;
    double sum = 0;
    size_t validCount = 0;

    for (size_t i = 0; i < count; i++) {
        if (data[i] == nodata || std::isnan(data[i])) continue;
        min = std::min(min, data[i]);
        max = std::max(max, data[i]);
        sum += data[i];
        validCount++;
    }

    if (validCount == 0) {
        mean = 0;
        stddev = 0;
        return;
    }

    mean = static_cast<float>(sum / validCount);

    double sqSum = 0;
    for (size_t i = 0; i < count; i++) {
        if (data[i] == nodata || std::isnan(data[i])) continue;
        double diff = data[i] - mean;
        sqSum += diff * diff;
    }
    stddev = static_cast<float>(std::sqrt(sqSum / validCount));
}

void ImageEnhancement::linearStretch(const float *input, float *output, size_t count,
                                     float minVal, float maxVal, float nodata)
{
    float range = maxVal - minVal;
    if (range == 0) range = 1.0f;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) {
            output[i] = nodata;
            continue;
        }
        float normalized = (input[i] - minVal) / range;
        output[i] = std::clamp(normalized * 255.0f, 0.0f, 255.0f);
    }
}

void ImageEnhancement::percentClipStretch(const float *input, float *output, size_t count,
                                          float pct, float nodata)
{
    // Collect valid values and sort
    std::vector<float> valid;
    valid.reserve(count);
    for (size_t i = 0; i < count; i++) {
        if (input[i] != nodata && !std::isnan(input[i]))
            valid.push_back(input[i]);
    }

    if (valid.empty()) {
        for (size_t i = 0; i < count; i++) output[i] = nodata;
        return;
    }

    std::sort(valid.begin(), valid.end());
    size_t lo = static_cast<size_t>(valid.size() * pct / 100.0f);
    size_t hi = static_cast<size_t>(valid.size() * (100.0f - pct) / 100.0f);
    if (hi >= valid.size()) hi = valid.size() - 1;

    linearStretch(input, output, count, valid[lo], valid[hi], nodata);
}

void ImageEnhancement::stddevStretch(const float *input, float *output, size_t count,
                                     float k, float nodata)
{
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    float lo = mean - k * stddev;
    float hi = mean + k * stddev;

    linearStretch(input, output, count, lo, hi, nodata);
}

void ImageEnhancement::histogramEqualize(const float *input, float *output, size_t count,
                                         int bins, float nodata)
{
    // Find valid range
    float min, max, mean, stddev;
    computeStats(input, count, nodata, min, max, mean, stddev);

    if (min == max) {
        for (size_t i = 0; i < count; i++)
            output[i] = (input[i] == nodata) ? nodata : 128.0f;
        return;
    }

    // Build histogram
    std::vector<int> hist(bins, 0);
    float binWidth = (max - min) / bins;

    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) continue;
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        hist[bin]++;
    }

    // Compute CDF
    std::vector<float> cdf(bins);
    size_t validCount = 0;
    for (int i = 0; i < bins; i++) validCount += hist[i];

    cdf[0] = static_cast<float>(hist[0]) / validCount;
    for (int i = 1; i < bins; i++)
        cdf[i] = cdf[i - 1] + static_cast<float>(hist[i]) / validCount;

    // Apply CDF mapping
    for (size_t i = 0; i < count; i++) {
        if (input[i] == nodata || std::isnan(input[i])) {
            output[i] = nodata;
            continue;
        }
        int bin = static_cast<int>((input[i] - min) / binWidth);
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        output[i] = cdf[bin] * 255.0f;
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cd build && make test_image_enhancement && ./tests/test_image_enhancement
```

Expected: All 5 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/processing/algorithms/image_enhancement.h src/processing/algorithms/image_enhancement.cpp tests/test_image_enhancement.cpp
git commit -m "feat(enhancement): add contrast stretching algorithms (linear, clip, stddev, histogram EQ)"
```

---

### Task 2: Spatial Filtering

**Goal:** Convolution-based spatial filtering.

**Algorithms:**
- Mean filter (3x3, 5x5)
- Gaussian filter (3x3, 5x5)
- Median filter (3x3, 5x5)
- Sobel edge detection (horizontal + vertical)
- Laplacian edge detection
- Custom kernel support

**Files:**
- Modify: `src/processing/algorithms/image_enhancement.h/.cpp`
- Create: `tests/test_spatial_filter.cpp`

- [ ] **Step 1: Write failing tests**

Create `tests/test_spatial_filter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/image_enhancement.h"
#include <vector>

using namespace Catch;

TEST_CASE("Mean filter 3x3 on uniform image", "[spatial]") {
    // 5x5 image, all values 100
    std::vector<float> input(25, 100.0f);
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::meanFilter(input.data(), output.data(), 5, 5, 3);

    // Interior pixels should remain 100
    REQUIRE(output[6] == Approx(100.0f));  // (1,1)
    REQUIRE(output[12] == Approx(100.0f)); // (2,2)
}

TEST_CASE("Mean filter 3x3 on step edge", "[spatial]") {
    // 5x5 image: left half = 0, right half = 100
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (x < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::meanFilter(input.data(), output.data(), 5, 5, 3);

    // At the edge (x=2), should be averaged
    REQUIRE(output[2] > 0.0f);
    REQUIRE(output[2] < 100.0f);
}

TEST_CASE("Median filter removes salt-and-pepper noise", "[spatial]") {
    // 5x5 image with one outlier
    std::vector<float> input(25, 50.0f);
    input[12] = 999.0f; // center pixel is outlier
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::medianFilter(input.data(), output.data(), 5, 5, 3);

    // Median should be 50, not 999
    REQUIRE(output[12] == Approx(50.0f));
}

TEST_CASE("Sobel filter detects horizontal edge", "[spatial]") {
    // 5x5 image: top half = 0, bottom half = 100
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (y < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::sobelFilter(input.data(), output.data(), 5, 5);

    // At the horizontal edge (y=2), should have strong response
    REQUIRE(output[2 * 5 + 2] > 0.0f);
    // Top and bottom should be near zero
    REQUIRE(std::abs(output[0]) < 10.0f);
}

TEST_CASE("Laplacian filter detects edges", "[spatial]") {
    // 5x5 image with a vertical edge
    std::vector<float> input(25);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            input[y * 5 + x] = (x < 2) ? 0.0f : 100.0f;
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::laplacianFilter(input.data(), output.data(), 5, 5);

    // At the edge, Laplacian should have non-zero response
    REQUIRE(std::abs(output[2]) > 0.0f);
}

TEST_CASE("Gaussian filter smooths noise", "[spatial]") {
    std::vector<float> input(25, 50.0f);
    input[12] = 200.0f; // spike
    std::vector<float> output(25, 0.0f);

    ImageEnhancement::gaussianFilter(input.data(), output.data(), 5, 5, 3, 1.0f);

    // Output at center should be smoothed (less than 200)
    REQUIRE(output[12] < 200.0f);
    REQUIRE(output[12] > 50.0f);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Expected: FAIL — meanFilter/medianFilter/sobelFilter not defined

- [ ] **Step 3: Implement spatial filtering**

Add to `image_enhancement.h`:

```cpp
    // Spatial filters (in-place safe: output can alias input)
    static void meanFilter(const float *input, float *output, int width, int height, int kernelSize = 3);
    static void gaussianFilter(const float *input, float *output, int width, int height, int kernelSize = 3, float sigma = 1.0f);
    static void medianFilter(const float *input, float *output, int width, int height, int kernelSize = 3);
    static void sobelFilter(const float *input, float *output, int width, int height);
    static void laplacianFilter(const float *input, float *output, int width, int height);

private:
    static void convolve(const float *input, float *output, int width, int height,
                         const float *kernel, int kernelSize);
    static void generateGaussianKernel(float *kernel, int size, float sigma);
```

Implementation uses standard 2D convolution. Median filter uses `std::nth_element` for O(n) median.

- [ ] **Step 4: Run tests**

Expected: All 6 tests PASS.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(enhancement): add spatial filtering (mean, Gaussian, median, Sobel, Laplacian)"
```

---

### Task 3: PCA (Principal Component Analysis)

**Goal:** Dimensionality reduction via eigen decomposition of covariance matrix.

**Files:**
- Modify: `src/processing/algorithms/image_enhancement.h/.cpp`
- Create: `tests/test_pca.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("PCA on 2-band correlated data", "[pca]") {
    // 2 bands, 100 pixels. Band2 = 2*Band1 + noise
    size_t n = 100;
    size_t bands = 2;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n));
    for (size_t i = 0; i < n; i++) {
        input[0][i] = static_cast<float>(i);
        input[1][i] = 2.0f * i + (i % 3 - 1) * 0.1f;
    }

    auto result = ImageEnhancement::pca(input, 2);

    // First component should capture most variance
    REQUIRE(result.explainedVariance[0] > 0.99f);
    // Second component should capture very little
    REQUIRE(result.explainedVariance[1] < 0.01f);
}

TEST_CASE("PCA output dimensions", "[pca]") {
    size_t n = 50;
    size_t bands = 3;
    std::vector<std::vector<float>> input(bands, std::vector<float>(n, 1.0f));

    auto result = ImageEnhancement::pca(input, 2);

    // Output should have 2 components
    REQUIRE(result.output.size() == 2);
    REQUIRE(result.output[0].size() == n);
    REQUIRE(result.output[1].size() == n);
}
```

- [ ] **Step 2: Run tests to verify they fail**
- [ ] **Step 3: Implement PCA**

PCA implementation: compute covariance matrix → Jacobi eigenvalue algorithm → project data onto principal components. No external linear algebra library needed (3x3 to 10x10 matrices typical for RS).

- [ ] **Step 4: Run tests**
- [ ] **Step 5: Commit**

```bash
git commit -m "feat(enhancement): add PCA (Principal Component Analysis)"
```

---

### Task 4: Dialogs and Menu Wiring

**Goal:** Create GUI dialogs for each enhancement algorithm and wire to Raster menu.

**Files:**
- Create: `src/app/dialogs/contrast_stretch_dialog.h/.cpp`
- Create: `src/app/dialogs/spatial_filter_dialog.h/.cpp`
- Create: `src/app/dialogs/pca_dialog.h/.cpp`
- Create: `src/app/dialogs/band_ratio_dialog.h/.cpp`
- Modify: `src/app/main_window.cpp`
- Modify: `src/app/CMakeLists.txt`

- [ ] **Step 1: ContrastStretchDialog**

Dialog with: input layer selector, method combo (Linear/Clip%/StdDev/HistogramEQ), parameters (clip %, stddev k), output path, OK/Cancel.

Pattern: follow `SpectralIndexDialog` — `QDialog` with `QFormLayout`, connect to processing algorithm on accept.

- [ ] **Step 2: SpatialFilterDialog**

Dialog with: input layer, filter type combo (Mean/Gaussian/Median/Sobel/Laplacian), kernel size combo (3x3/5x5), Apply button.

- [ ] **Step 3: PcaDialog**

Dialog with: input raster layer (multi-band), number of components spinner, output path.

- [ ] **Step 4: Wire to Raster menu**

In `main_window.cpp`, add to existing Raster menu:
```cpp
auto *enhanceMenu = rasterMenu->addMenu(tr("Enhancement"));
enhanceMenu->addAction(tr("Contrast Stretch..."), this, &QgisDesktopWindow::openContrastStretch);
enhanceMenu->addAction(tr("Spatial Filter..."), this, &QgisDesktopWindow::openSpatialFilter);
enhanceMenu->addAction(tr("PCA..."), this, &QgisDesktopWindow::openPca);
```

- [ ] **Step 5: Build and run full test suite**

```bash
cd build && cmake --build . -j$(nproc) && ctest --output-on-failure
```

Expected: All existing 191 tests + new enhancement tests pass.

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(enhancement): add enhancement dialogs and Raster > Enhancement menu"
```

---

### Task 5: Band Ratio and IHS Transform

**Goal:** Band ratio computation and IHS (Intensity-Hue-Saturation) forward/inverse transform.

**Files:**
- Modify: `src/processing/algorithms/image_enhancement.h/.cpp`
- Create: `tests/test_band_ratio.cpp`

- [ ] **Step 1: Write failing tests**

```cpp
TEST_CASE("Band ratio calculation", "[enhancement]") {
    std::vector<float> band1 = {10, 20, 30};
    std::vector<float> band2 = {5, 10, 15};
    std::vector<float> output(3);

    ImageEnhancement::bandRatio(band1.data(), band2.data(), output.data(), 3);

    REQUIRE(output[0] == Approx(2.0f));
    REQUIRE(output[1] == Approx(2.0f));
    REQUIRE(output[2] == Approx(2.0f));
}

TEST_CASE("IHS forward and inverse transform", "[enhancement]") {
    // RGB values
    float r = 200, g = 100, b = 50;
    float i, h, s;

    ImageEnhancement::rgbToIhs(r, g, b, i, h, s);

    // Intensity should be average
    REQUIRE(i == Approx((200 + 100 + 50) / 3.0f));

    // Convert back
    float r2, g2, b2;
    ImageEnhancement::ihsToRgb(i, h, s, r2, g2, b2);

    REQUIRE(r2 == Approx(r).margin(1.0f));
    REQUIRE(g2 == Approx(g).margin(1.0f));
    REQUIRE(b2 == Approx(b).margin(1.0f));
}
```

- [ ] **Step 2-5:** Implement, test, commit

```bash
git commit -m "feat(enhancement): add band ratio and IHS transform"
```

---

*Plan created: 2026-06-02*
