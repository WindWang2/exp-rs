// src/processing/algorithms/spectral_roi.h — ROI mean spectrum
#pragma once

#include <QPolygonF>
#include <QString>

#include <cstddef>
#include <vector>

/// Mean-spectrum extraction over a polygon ROI — the input side of the
/// spectral-analysis workbench (pixel spectrum -> ROI mean spectrum ->
/// library matching).
namespace SpectralRoiProfile
{
    /// Mean / stddev spectrum over the ROI pixels, per band.
    struct RoiProfileResult
    {
        std::vector<float> mean;        // per band (NaN when no pixel covered)
        std::vector<float> stddev;      // per band (NaN when no pixel covered)
        std::vector<float> wavelengths; // band center wavelengths (nm) from band
                                        // WAVELENGTH metadata (0 when absent)
        size_t pixelCount = 0;          // pixels inside the ROI
    };

    /**
     * Compute the mean and stddev spectrum over the pixels of a polygon ROI.
     *
     * Membership is center-of-pixel: a pixel belongs to the ROI when its
     * center (col+0.5, row+0.5) mapped to map coordinates lies inside
     * @p polygon. The polygon is in the raster's map CRS; conversion uses the
     * raster geotransform. Only the polygon's bounding box is scanned.
     *
     * @param rasterPath raster to sample
     * @param polygon    ROI polygon in map coordinates (may be degenerate)
     * @param result     [out] per-band mean/stddev + wavelengths + pixel count
     * @param errorMessage optional error sink
     * @return true on success; false for unreadable rasters or invalid geometry
     */
    bool meanSpectrum( const QString &rasterPath, const QPolygonF &polygon,
                       RoiProfileResult *result, QString *errorMessage = nullptr );
} // namespace SpectralRoiProfile
