// src/processing/algorithms/mosaic.cpp — Mosaic/merge algorithm
#include "mosaic.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Mosaic
{

static bool isValid(float value, float nodata)
{
    if (std::isnan(nodata))
        return !std::isnan(value);
    return value != nodata;
}

bool merge(const MosaicSource *sources, size_t sourceCount,
           float *out, size_t outWidth, size_t outHeight)
{
    if (!sources || sourceCount == 0 || !out || outWidth == 0 || outHeight == 0)
        return false;

    // Initialize output to NaN (no data)
    for (size_t i = 0; i < outWidth * outHeight; ++i)
        out[i] = std::numeric_limits<float>::quiet_NaN();

    // Paint each source in order (later sources overwrite earlier)
    for (size_t s = 0; s < sourceCount; ++s) {
        const MosaicSource &src = sources[s];
        if (!src.data || src.width == 0 || src.height == 0)
            continue;

        for (size_t row = 0; row < src.height; ++row) {
            size_t outRow = src.offsetY + row;
            if (outRow >= outHeight)
                continue;

            for (size_t col = 0; col < src.width; ++col) {
                size_t outCol = src.offsetX + col;
                if (outCol >= outWidth)
                    continue;

                float val = src.data[row * src.width + col];
                if (isValid(val, src.nodata))
                    out[outRow * outWidth + outCol] = val;
            }
        }
    }

    return true;
}

}
