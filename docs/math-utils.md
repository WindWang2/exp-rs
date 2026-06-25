# MathUtils Reference

Shared math utilities for processing algorithms. All functions handle NaN and nodata values correctly.

## Namespace

```cpp
#include "processing/algorithms/math_utils.h"
using namespace MathUtils;
```

## Types

### Stats

Statistics computed over a float array.

```cpp
struct Stats {
    size_t count = 0;      // Total values (including NaN/nodata)
    size_t validCount = 0; // Valid (non-NaN, non-nodata) values
    float min = 0.0f;
    float max = 0.0f;
    float mean = 0.0f;
    float stddev = 0.0f;  // Population stddev (N denominator)
};
```

### AccumulatorStats

Pre-accumulated sums for streaming/segment-based statistics.

```cpp
struct AccumulatorStats {
    size_t count = 0;
    double sum = 0.0;
    double sumSq = 0.0;
    float min = 0.0f;
    float max = 0.0f;
};
```

## Functions

### safeDiv

```cpp
float safeDiv(float numerator, float denominator);
```

Returns NaN when denominator is zero. Used for band math and spectral indices where NaN signals "no data".

**Example:**
```cpp
float ndvi = MathUtils::safeDiv(nir - red, nir + red);
// If nir + red == 0, ndvi is NaN (masked pixel)
```

### safeDivDouble

```cpp
double safeDivDouble(double numerator, double denominator);
```

Returns 0.0 when denominator is zero. Used for accuracy assessment and terrain analysis where 0.0 is the expected fallback.

**Note:** Unlike `safeDiv`, this returns 0.0 (not NaN) because callers in accuracy assessment expect 0.0 as the fallback value for guarded divisions.

**Example:**
```cpp
double accuracy = MathUtils::safeDivDouble(diag, total);
// If total == 0, accuracy is 0.0
```

### computeStats

```cpp
Stats computeStats(const float *data, size_t count);
```

Compute min, max, mean, population stddev over a float array. NaN values are skipped.

**Example:**
```cpp
std::vector<float> pixels = {1.0f, 2.0f, NaN, 4.0f, 5.0f};
MathUtils::Stats s = MathUtils::computeStats(pixels.data(), pixels.size());
// s.validCount == 4, s.mean == 3.0f, s.min == 1.0f, s.max == 5.0f
```

### computeStatsWithNodata

```cpp
Stats computeStatsWithNodata(const float *data, size_t count, float nodata);
```

Same as `computeStats` but also skips a specific nodata value (in addition to NaN).

**Example:**
```cpp
float nodata = -9999.0f;
MathUtils::Stats s = MathUtils::computeStatsWithNodata(dem.data(), dem.size(), nodata);
```

### computeStatsFromAccumulators

```cpp
Stats computeStatsFromAccumulators(const AccumulatorStats &acc);
```

Compute statistics from pre-accumulated sums. Uses population stddev (N denominator). Useful for per-segment statistics where the full pixel array is not available.

**Example:**
```cpp
MathUtils::AccumulatorStats acc;
acc.count = 100;
acc.sum = 5000.0;
acc.sumSq = 260000.0;
acc.min = 10.0f;
acc.max = 90.0f;
MathUtils::Stats s = MathUtils::computeStatsFromAccumulators(acc);
// s.mean == 50.0f
```

### normalizedDifference

```cpp
bool normalizedDifference(const float *a, const float *b, float *out, size_t count);
```

Compute (a - b) / (a + b) element-wise. Result is NaN when (a + b) == 0. Returns false on null pointers or zero count.

**Example:**
```cpp
// Compute NDVI
MathUtils::normalizedDifference(nirBand, redBand, ndviOut, pixelCount);
```

## Stddev Convention

All functions use **population stddev** (N denominator), matching the convention in remote sensing image processing. This is consistent across `computeStats`, `computeStatsWithNodata`, and `computeStatsFromAccumulators`.
