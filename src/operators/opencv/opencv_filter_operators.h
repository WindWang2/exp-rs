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
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
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
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvMedianBlurOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:median_blur"; }
    std::string displayName() const override { return "Median Blur"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply median blur using OpenCV."; }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvSobelOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:sobel"; }
    std::string displayName() const override { return "Sobel Edge Detector"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Sobel edge detection using OpenCV."; }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

class OpenCvLaplacianOperator : public OpenCvOperatorBase {
public:
    std::string name() const override { return "opencv:laplacian"; }
    std::string displayName() const override { return "Laplacian Edge Detector"; }
    std::string group() const override { return "opencv-filter"; }
    std::string description() const override { return "Apply Laplacian edge detection using OpenCV."; }
    Json::Value schema() const override;
    Json::Value metadata() const override;

protected:
    void applyFilter(cv::Mat& srcDst, const Json::Value& params) const override;
    Json::Value operatorSchemaProperties() const override;
};

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
