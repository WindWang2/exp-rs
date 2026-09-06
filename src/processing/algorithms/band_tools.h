// band_tools.h — File-level orchestration for the small band tools
// (band ratio / IHS, band extraction, contrast stretch).
//
// Desktop Workbench UX 4.0 (thin-client law): these orchestrators used to be
// inline `callable:gdal_task` lambdas inside src/app dialogs. Moving them into
// the processing layer lets `rs:band_ratio`, `rs:extract_bands` and
// `rs:contrast_stretch` operators own the compute while the GUI keeps only
// parameter collection — one execution path for GUI/CLI/MCP.
//
// Numerical contract: every pixel formula delegates to the shared
// ImageEnhancement / ImageEnhancementStreaming kernels the dialogs already
// used, so outputs match the legacy dialog paths (IHS additionally masks
// NoData/sentinel pixels to NaN — see processRgbToIhsFile).
#pragma once

#include "processing/algorithms/image_enhancement_streaming.h"

#include <QString>
#include <QVector>

#include <utility>
#include <vector>

class BandTools
{
  public:
    /// Contrast-stretch configuration (kind + per-kind parameters).
    struct StretchSpec
    {
        ImageEnhancementStreaming::StretchKind kind =
            ImageEnhancementStreaming::StretchKind::Linear;
        float clipPercent = 2.0f;                              ///< PercentClip
        float stddevK = 2.0f;                                  ///< StdDev
        std::vector<std::pair<float, float>> piecewisePoints;  ///< Piecewise
    };

    /// Numerator/denominator band ratio -> single-band Float32 raster.
    /// Bands are 1-based. Fails when the bands are equal or out of range.
    static bool processBandRatioFile( const QString &sourcePath, const QString &outputPath,
                                      int numeratorBand, int denominatorBand,
                                      QString *errorMessage = nullptr );

    /// RGB -> IHS decomposition -> three-band Float32 raster (I, H, S).
    /// A NaN source pixel or a pixel equal to its band's declared sentinel
    /// yields NaN in all three components (panel #380 semantics).
    static bool processRgbToIhsFile( const QString &sourcePath, const QString &outputPath,
                                     int redBand, int greenBand, int blueBand,
                                     QString *errorMessage = nullptr );

    /// Extract the listed (1-based, in-range, non-empty) bands into a
    /// multi-band Float32 raster with copied georeference. Order is preserved.
    static bool processExtractBandsFile( const QString &sourcePath, const QString &outputPath,
                                         const QVector<int> &bands,
                                         QString *errorMessage = nullptr );

    /// Streaming two-pass contrast stretch of every band (per-band declared
    /// NoData is masked; undeclared bands use NaN). O(tile) memory.
    static bool processContrastStretchFile( const QString &sourcePath, const QString &outputPath,
                                            const StretchSpec &spec,
                                            QString *errorMessage = nullptr );
  private:
    /// Resolves band @a b's declared NoData (float-cast; NaN when undeclared).
    static float bandNodata( class GdalDatasetWrapper &src, int b );
};
