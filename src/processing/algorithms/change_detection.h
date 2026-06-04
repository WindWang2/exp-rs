// src/processing/algorithms/change_detection.h
#pragma once

#include <cstddef>
#include <cstdint>

namespace ChangeDetection
{

struct ChangeStats {
    size_t count = 0;
    float mean = 0.0f;
    float min = 0.0f;
    float max = 0.0f;
    float stddev = 0.0f;
};

bool difference(const float *before, const float *after, float *out, size_t count);
bool normalizedDifference(const float *before, const float *after, float *out, size_t count);
bool changeMask(const float *diff, uint8_t *mask, size_t count, float threshold);
ChangeStats statistics(const float *diff, size_t count);

}
