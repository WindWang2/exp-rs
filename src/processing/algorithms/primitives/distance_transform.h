// primitives/distance_transform.h — exact Euclidean distance transform over
// 0/1/255 byte masks (Foundation 5.0, Milestone A).
//
// Contract: for every cell, the Euclidean distance (in pixels) to the nearest
// source cell. Sources are the mask's foreground cells (value == 1); 0 and
// 255 (NoData) are both non-sources — callers layer NoData semantics on top
// (e.g. rs:proximity refuses all-NoData masks rather than reading infinities
// here). Output is float pixels; source cells are exactly 0.
//
// Algorithm: separable squared-distance transform (Felzenszwalb & Huttenlocher
// 2012 lower-envelope 1D passes, row then column) — exact for unweighted
// Euclidean distance, O(N) per pass, full-frame (2 × width×height doubles
// scratch, caller-provided or heap-internal).
//
// When the mask contains no foreground cell the output is all +infinity
// (float); callers are expected to refuse that case before calling.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sicnu::rs::primitives
{

/// Computes the exact Euclidean distance-to-nearest-foreground transform of
/// @a mask (@p width × @p height bytes) into @a out (@p width × @p height
/// floats). Returns false only on null/bad-size arguments.
bool distanceToForeground( const uint8_t *mask, int width, int height, float *out );

} // namespace sicnu::rs::primitives
