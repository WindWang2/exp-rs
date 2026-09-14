// feature_matcher.h — D14 Package D: feature matching with Lowe's ratio test
// and RANSAC homography filtering (ADR 0159).
//
// Two entry levels:
//   - estimateHomographyRansac: deterministic (mt19937 seed 42) robust
//     homography from raw correspondences.
//   - matchDescriptors: L2 nearest-neighbour matching with Lowe's ratio test,
//     followed by the RANSAC filter and inlier RMSE reporting.
//   - matchImages: grid keypoint sampling over the image buffers with a
//     simplified gradient-histogram (Sift) or intensity-patch (Orb)
//     descriptor, feeding matchDescriptors. This is deliberately *not* a
//     full scale-space SIFT implementation — it is the dependency-free
//     in-repo matcher used by the workbench and the agent tool.
#pragma once

#include <array>
#include <vector>

namespace rs::algorithms {

enum class FeatureDetectorType { Sift, Orb };

struct FeatureMatchOptions {
    FeatureDetectorType detector{FeatureDetectorType::Sift};
    int maxFeatures{2000};
    double loweRatioThreshold{0.75};   // Lowe's ratio test d1 / d2 < threshold
    double ransacReprojThreshold{3.0}; // inlier threshold in pixels
    int ransacMaxIters{2000};
    double ransacConfidence{0.99};
};

struct KeyPoint2D {
    double x{0.0};
    double y{0.0};
    double size{0.0};
    double angle{0.0};
    double response{0.0};
    int octave{0};
};

struct MatchPair {
    KeyPoint2D srcPt;
    KeyPoint2D dstPt;
    double distance{0.0};
    bool inlier{false};
};

struct FeatureMatchReport {
    std::vector<MatchPair> matches;
    int totalCandidates{0};
    int inlierCount{0};
    double inlierRatio{0.0};
    std::array<double, 9> homographyMatrix{}; // 3x3 DLT H, row-major, h33 = 1
    double inlierRmse{0.0};
};

class FeatureMatcher {
  public:
    FeatureMatcher() = default;
    ~FeatureMatcher() = default;

    /// End-to-end matching from row-major float image buffers.
    FeatureMatchReport matchImages(const float* srcData, int srcWidth, int srcHeight,
                                   const float* dstData, int dstWidth, int dstHeight,
                                   const FeatureMatchOptions& options = {});

    /// Descriptor matching seam (unit-test isolation). Descriptor vectors are
    /// L2-normalized internally before the ratio test.
    FeatureMatchReport matchDescriptors(const std::vector<KeyPoint2D>& srcKps,
                                        const std::vector<std::vector<float>>& srcDesc,
                                        const std::vector<KeyPoint2D>& dstKps,
                                        const std::vector<std::vector<float>>& dstDesc,
                                        const FeatureMatchOptions& options = {});

    /// Standalone RANSAC homography. Fewer than 4 correspondences yield the
    /// identity matrix and an all-false mask. Deterministic (seed 42).
    static std::pair<std::array<double, 9>, std::vector<bool>>
    estimateHomographyRansac(const std::vector<std::pair<double, double>>& srcPts,
                             const std::vector<std::pair<double, double>>& dstPts,
                             double reprojThreshold,
                             int maxIters,
                             double confidence);

  private:
    struct DescriptorHit {
        int best{-1};
        double bestDist{0.0};
        double secondDist{0.0};
    };
    static DescriptorHit ratioMatch(const std::vector<float>& query,
                                    const std::vector<std::vector<float>>& trainSet);
};

} // namespace rs::algorithms
