#pragma once

#include <vector>
#include <cstddef>

class ImageEnhancement
{
public:
    static void linearStretch(const float *input, float *output, size_t count,
                              float minVal, float maxVal, float nodata = -9999.0f);

    static void percentClipStretch(const float *input, float *output, size_t count,
                                   float pct = 2.0f, float nodata = -9999.0f);

    static void stddevStretch(const float *input, float *output, size_t count,
                              float k = 2.0f, float nodata = -9999.0f);

    static void histogramEqualize(const float *input, float *output, size_t count,
                                  int bins = 256, float nodata = -9999.0f);

private:
    static void computeStats(const float *data, size_t count, float nodata,
                             float &min, float &max, float &mean, float &stddev);
};
