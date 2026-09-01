/***************************************************************************
 * opencv_filter_operators.h  —  OpenCV filter operators
 ***************************************************************************/
#pragma once

#include "opencv_operator_base.h"

namespace sicnu::operators::opencv {

class OpenCvGaussianBlurOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:gaussian_blur"; }
    std::string displayName() const override { return "Gaussian Blur"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Gaussian blur using OpenCV."; }
    // FullRaster on purpose: the masked-normalized kernel cannot be tiled
    // bit-exactly (mask reflection at raster edges); median/sobel/laplacian
    // stream instead.
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::FullRaster;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    int neighborhoodRadius(const Json::Value& params) const override;
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

/**
 * Box / mean filter via cv::blur (normalized rectangular kernel).
 */
class OpenCvMeanBlurOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:mean_blur"; }
    std::string displayName() const override { return "Mean Filter"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override {
        return "Apply a box (mean) filter using OpenCV cv::blur.";
    }
    // FullRaster on purpose: see OpenCvGaussianBlurOperator (masked-normalized
    // kernels do not tile bit-exactly).
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::FullRaster;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    int neighborhoodRadius(const Json::Value& params) const override;
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvMedianBlurOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:median_blur"; }
    std::string displayName() const override { return "Median Blur"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply median blur using OpenCV."; }
    // Windowed kernel: streamed tile-by-tile through the base (O(tile) memory).
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        // FullRaster on purpose: median's NaN ordering is view-dependent (see
        // neighborhoodRadius); Sobel/Laplacian stream.
        return RSOperatorMemoryPolicy::FullRaster;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    int neighborhoodRadius(const Json::Value& params) const override;
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvSobelOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:sobel"; }
    std::string displayName() const override { return "Sobel Edge Detector"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Sobel edge detection using OpenCV."; }
    // Windowed kernel: streamed tile-by-tile through the base (O(tile) memory).
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    int neighborhoodRadius(const Json::Value& params) const override;
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvLaplacianOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:laplacian"; }
    std::string displayName() const override { return "Laplacian Edge Detector"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Laplacian edge detection using OpenCV."; }
    // Windowed kernel: streamed tile-by-tile through the base (O(tile) memory).
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    int neighborhoodRadius(const Json::Value& params) const override;
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

/**
 * Canny keeps the full-frame path (default FullRaster policy): the operator
 * normalizes the band to 8-bit with the global min/max (cv::minMaxLoc over
 * the whole image) before edge detection, so no per-tile halo can reproduce
 * the full-frame result.
 */
class OpenCvCannyOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:canny"; }
    std::string displayName() const override { return "Canny Edge Detector"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Canny edge detection using OpenCV."; }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

} // namespace sicnu::operators::opencv
