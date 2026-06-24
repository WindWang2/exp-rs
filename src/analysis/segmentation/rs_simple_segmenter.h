// rs_simple_segmenter.h — Phase 10B Task 10B.3: fallback segmenter when OTB
// is not available.
//
// Simple segmentation pipeline: Gaussian smoothing → intensity quantization →
// connected component labeling (8-connected). Produces a label image suitable
// for RsSegmentFeatures extraction.
//
// This is NOT a replacement for OTB MeanShift — it's a teaching-quality
// fallback for environments without OTB installed.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

class QGIS_ANALYSIS_EXPORT RsSimpleSegmenter
{
  public:
    struct Params
    {
        int smoothKernel = 5;     // Gaussian kernel size (odd, >= 3)
        int quantizeBins = 32;    // Number of quantization levels per band
        int minRegionSize = 50;   // Merge regions smaller than this
    };

    /// Segment a single-band image.
    /// Returns a label map (0 = nodata if input has nodata pixels).
    static RsSegmentMap segment( const float *data, int width, int height,
                                 float nodata, const Params &params );

    /// Segment a multi-band image by first computing a scalar
    /// (mean of selected bands) then running single-band segmentation.
    static RsSegmentMap segmentMultiBand( const float *const *bandData,
                                          int nBands, int width, int height,
                                          float nodata, const Params &params );

  private:
    /// Gaussian smoothing (separable, in-place).
    static void gaussianSmooth( QVector<float> &data, int w, int h, int kernelSize );

    /// Quantize float data to integer bins.
    static QVector<int> quantize( const float *data, size_t n, int bins, float nodata );

    /// 8-connected component labeling on quantized integer grid.
    /// Returns label map (1-based; 0 = background/nodata).
    static QVector<quint32> connectedComponents( const QVector<int> &quantized,
                                                  int w, int h, int nodataBin );

    /// Merge small regions by assigning them to their most frequent neighbor.
    static void mergeSmallRegions( QVector<quint32> &labels, int w, int h,
                                   int minSize );
};
