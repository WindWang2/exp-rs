// src/processing/algorithms/mosaic.h
#pragma once

#include <cstddef>
#include <limits>

namespace Mosaic
{

struct MosaicSource {
    const float *data;
    size_t width;
    size_t height;
    size_t offsetX;
    size_t offsetY;
    float nodata = std::numeric_limits<float>::quiet_NaN();
};

bool merge(const MosaicSource *sources, size_t sourceCount,
           float *out, size_t outWidth, size_t outHeight);

}
