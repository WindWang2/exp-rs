#pragma once

#include <QString>
#include <vector>
#include <cstddef>

class ImageEnhancement
{
public:
    struct PcaResult {
        std::vector<std::vector<float>> output;       // output[component][pixel]
        std::vector<float> explainedVariance;          // fraction of variance per component
        std::vector<std::vector<float>> eigenvectors;  // eigenvectors[component][band]
    };

    static PcaResult pca(const std::vector<std::vector<float>> &input, int numComponents);

    /**
     * Run PCA on a multi-band GeoTIFF and write component bands to output.
     */
    static bool processPcaFile(const QString &sourcePath, const QString &outputPath,
                               int numComponents, QString *errorMessage = nullptr);

    static void linearStretch(const float *input, float *output, size_t count,
                              float minVal, float maxVal, float nodata = -9999.0f);

    static void percentClipStretch(const float *input, float *output, size_t count,
                                   float pct = 2.0f, float nodata = -9999.0f);

    static void stddevStretch(const float *input, float *output, size_t count,
                              float k = 2.0f, float nodata = -9999.0f);

    static void histogramEqualize(const float *input, float *output, size_t count,
                                  int bins = 256, float nodata = -9999.0f);

    // Band ratio
    static void bandRatio(const float *band1, const float *band2, float *output, size_t count);

    // IHS transform (single pixel)
    static void rgbToIhs(float r, float g, float b, float &i, float &h, float &s);
    static void ihsToRgb(float i, float h, float s, float &r, float &g, float &b);

    // Spatial filters
    static void meanFilter(const float *input, float *output, int width, int height, int kernelSize = 3);
    static void gaussianFilter(const float *input, float *output, int width, int height, int kernelSize = 3, float sigma = 1.0f);
    static void medianFilter(const float *input, float *output, int width, int height, int kernelSize = 3);
    static void sobelFilter(const float *input, float *output, int width, int height);
    static void laplacianFilter(const float *input, float *output, int width, int height);

    // SAR speckle filters
    static void leeFilter(const float *input, float *output, int width, int height,
                          int kernelSize = 5, float noiseVariance = 1.0f);
    static void frostFilter(const float *input, float *output, int width, int height,
                            int kernelSize = 5, float damping = 2.0f);
    static void kuanFilter(const float *input, float *output, int width, int height,
                           int kernelSize = 5, float noiseVariance = 1.0f);
    static void gammaMapFilter(const float *input, float *output, int width, int height,
                               int kernelSize = 5, float noiseVariance = 1.0f);

    /** Generic convolution with user-defined kernel */
    static void convolve(const float *input, float *output, int width, int height,
                         const float *kernel, int kernelSize);

private:
    static void generateGaussianKernel(float *kernel, int size, float sigma);

    // PCA helpers
    static void computeCovarianceMatrix(const std::vector<std::vector<float>> &centered,
                                         int bands, size_t n,
                                         std::vector<std::vector<float>> &cov);
    static void jacobiEigen(std::vector<std::vector<float>> &A, int n,
                            std::vector<float> &eigenvalues,
                            std::vector<std::vector<float>> &eigenvectors);
};
