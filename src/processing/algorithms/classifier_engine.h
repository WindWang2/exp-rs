// src/processing/algorithms/classifier_engine.h — D15 Package C public seam.
//
// Self-contained supervised/unsupervised classification kernels
// (KMeans, ISODATA, Random Forest, RBF-SVM, NormalBayes) behind one small
// factory seam.  Feature matrices are row-major float, labels are
// non-negative ints; no OpenCV/QGIS dependency so the seam stays portable
// and header-stable (the OpenCV-backed GUI stack lives untouched in
// src/analysis/classification).
//
// Contracts:
//   * fit() on empty input leaves the model untrained; predictOne() on an
//     untrained model returns -1; predictProbabilities() returns {}.
//   * predictProbabilities() returns one posterior per ascending distinct
//     training label, summing to 1 for RandomForest / NormalBayes; KMeans /
//     ISODATA / SVM emit a 1-hot distribution of the hard decision.
//   * predictBatch() requires outLabels.size() >= numSamples; results are
//     written positionally.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rs::processing
{

enum class ClassifierAlgorithm
{
    RandomForest,
    SupportVectorMachine,
    KMeans,
    Isodata,
    NormalBayes,
};

struct ClassifierHyperparameters
{
    // Random forest
    int rfNumTrees{ 100 };
    int rfMaxDepth{ 15 };
    int rfMinSamplesSplit{ 2 };
    // SVM (RBF kernel, C-SVC style box constraint)
    double svmC{ 1.0 };
    double svmGamma{ 0.1 };
    // KMeans / ISODATA
    int kClusters{ 5 };
    int maxIterations{ 100 };
    double convergenceEpsilon{ 1e-4 };
    // ISODATA
    int isodataMinClusterSize{ 10 };
    double isodataMaxStdDev{ 1.0 };
    double isodataMinClusterDist{ 2.0 };
    // Determinism seed for bootstrap / seeding randomness (testability).
    uint32_t randomSeed{ 42 };
};

class IClassifierModel
{
  public:
    virtual ~IClassifierModel() = default;

    virtual void fit( std::span<const float> featureMatrix,
                      std::span<const int> labels,
                      size_t numSamples,
                      size_t numFeatures ) = 0;
    virtual int predictOne( std::span<const float> sampleFeatures ) const = 0;
    virtual void predictBatch( std::span<const float> inFeatures,
                               std::span<int> outLabels,
                               size_t numSamples,
                               size_t numFeatures ) const = 0;
    virtual std::vector<float> predictProbabilities( std::span<const float> sampleFeatures ) const = 0;

    /// Unsupervised models expose their centroids after fit(); supervised
    /// models return {} (never null, never throws).
    virtual std::vector<std::vector<float>> clusterCentroids() const { return {}; }
    /// False until a successful fit() with at least one sample.
    virtual bool isTrained() const { return false; }
};

class ClassifierEngine
{
  public:
    /// Factory; returns nullptr for an unknown algorithm enum value.
    static std::unique_ptr<IClassifierModel> create( ClassifierAlgorithm algo,
                                                     const ClassifierHyperparameters &params );
};

} // namespace rs::processing
