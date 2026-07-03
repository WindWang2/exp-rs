// image_fusion.h — Phase 11.1: Image fusion / pan-sharpening algorithms.
//
// Fusion methods for merging high-resolution panchromatic with
// low-resolution multispectral imagery:
//   - Linear weighted fusion (simple weighted average)
//   - Brovey transform (ratio-based)
//   - PCA fusion (principal component substitution)
//   - IHS fusion (intensity-hue-saturation substitution)
//
// All functions operate on float arrays with nodata support.
#pragma once

#include <QString>
#include <QVector>

class ImageFusion
{
  public:
    /// Linear weighted fusion: simplest fusion method
    ///   fused[i] = msWeight[i] * ms[i] + panWeight * pan
    /// msWeights: per-band mixing weight (default 0.5 for each)
    /// panWeight: weight for panchromatic (default 0.5)
    /// All weights should sum to 1.0 per output band.
    /// Returns fused bands (same count as msBands).
    static QVector<QVector<float>> linearWeighted(
        const QVector<const float *> &msBands, int nBands,
        const float *panBand, int width, int height, float nodata,
        const QVector<float> &msWeights = QVector<float>(),
        float panWeight = 0.5f );

    /// Brovey fusion: for each multispectral band,
    ///   fused[i] = (ms[i] / sum(ms)) * pan
    /// msBands: list of multispectral band data (each width*height).
    /// panBand: panchromatic band data (width*height).
    /// All bands must be co-registered and same dimensions.
    /// Returns fused bands (same count as msBands).
    static QVector<QVector<float>> brovey(
        const QVector<const float *> &msBands, int nBands,
        const float *panBand, int width, int height, float nodata );

    /// PCA fusion:
    ///   1. Forward PCA on multispectral bands
    ///   2. Replace PC1 with histogram-matched panchromatic
    ///   3. Inverse PCA to get fused bands
    /// Returns fused bands (same count as msBands).
    static QVector<QVector<float>> pcaFusion(
        const QVector<const float *> &msBands, int nBands,
        const float *panBand, int width, int height, float nodata );

    /// IHS fusion:
    ///   1. Convert first 3 MS bands (as RGB) to IHS
    ///   2. Replace Intensity with histogram-matched panchromatic
    ///   3. Convert IHS back to RGB
    /// Returns 3 fused bands (R, G, B).
    static QVector<QVector<float>> ihsFusion(
        const float *msR, const float *msG, const float *msB,
        const float *panBand, int width, int height, float nodata );

    struct NativeFusionParams {
        QString method;
        float panWeight = 0.5f;
        QVector<float> msWeights;
        int redIdx = 0;
        int greenIdx = 1;
        int blueIdx = 2;
    };

    /**
     * Read pan/MS rasters, run a native fusion algorithm, write GeoTIFF output.
     * Supported methods: linear, brovey, ihs, pca.
     */
    static bool processNativeFusion(const QString &panPath, const QString &msPath,
                                    const QString &outputPath,
                                    const NativeFusionParams &params,
                                    QString *errorMessage = nullptr);

  private:
    /// Histogram-match src to ref (match mean and stddev).
    static void histogramMatch( float *data, int n,
                                const float *ref, int refN, float nodata );
};
