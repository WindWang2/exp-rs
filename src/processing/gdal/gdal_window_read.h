// src/processing/gdal/gdal_window_read.h
// Single-band, edge-replicated window reads (GdalBlockStream's border
// semantics, exposed as a direct RasterIO helper for multi-dataset lockstep
// streaming where one GdalBlockStream::forEach cannot express the join).
#pragma once

#include <vector>

class GdalDatasetWrapper;

namespace sicnu::processing
{

/// Reads the window (x0, y0, w, h) of @a band expanded by @a halo pixels on
/// every side, with raster-edge replication (positions outside the raster
/// repeat the nearest valid row/column), into @a out. Buffer layout is
/// row-major with stride (w + 2*halo); @a out is resized to
/// (w+2h)*(h+2h) floats. Returns false when the clamped inner window is empty
/// or the read fails.
bool readClampedWindow( const GdalDatasetWrapper &ds, int band, int x0, int y0,
                        int w, int h, int halo, std::vector<float> &out );

} // namespace sicnu::processing
