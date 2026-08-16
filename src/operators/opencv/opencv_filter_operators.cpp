/***************************************************************************
 * opencv_filter_operators.cpp  —  OpenCV filter operator implementations
 ***************************************************************************/
#include "opencv_filter_operators.h"
#include "opencv_utils.h"

#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <opencv2/imgproc.hpp>

namespace sicnu::operators::opencv {

using namespace schema;
namespace {

void applyNormalizedFilter(cv::Mat& srcDst, const std::function<void(const cv::Mat&, cv::Mat&)>& filterFn) {
    cv::Mat mask;
    cv::compare(srcDst, srcDst, mask, cv::CMP_EQ); // 255 for non-NaN, 0 for NaN
    const int nonZero = cv::countNonZero(mask);
    if (nonZero == mask.rows * mask.cols) {
        cv::Mat out;
        filterFn(srcDst, out);
        out.copyTo(srcDst);
        return;
    }
    if (nonZero == 0) {
        srcDst.setTo(std::numeric_limits<float>::quiet_NaN());
        return;
    }

    cv::Mat cleanSrc = srcDst.clone();
    cleanSrc.setTo(0.0f, ~mask);
    cv::Mat maskF, filteredData, filteredMask;
    mask.convertTo(maskF, CV_32F, 1.0 / 255.0);
    filterFn(cleanSrc, filteredData);
    filterFn(maskF, filteredMask);
    cv::Mat validMask = (filteredMask > 1e-4f);
    cv::divide(filteredData, filteredMask, filteredData);
    filteredData.setTo(std::numeric_limits<float>::quiet_NaN(), ~validMask);
    filteredData.copyTo(srcDst);
}

} // namespace

// ---------- Gaussian Blur ----------

Json::Value OpenCvGaussianBlurOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvGaussianBlurOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("filter");
    meta["tags"].append("blur");
    meta["purpose"] = "Smooth image using a Gaussian kernel";
    meta["useCases"].append("Noise reduction before edge detection");
    meta["limitations"].append("Kernel size must be a positive odd integer");
    return meta;
}

Json::Value OpenCvGaussianBlurOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["kernelSize"] = makeIntegerParam("kernelSize", "Gaussian kernel size (odd)", 5);
    props["sigma"] = makeNumberParam("sigma", "Gaussian sigma", 1.0);
    return props;
}

void OpenCvGaussianBlurOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const int kernelSize = getInt(params, "kernelSize", 5);
    const double sigma = getDouble(params, "sigma", 1.0);
    if (!isValidKernelSize(kernelSize)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernelSize must be a positive odd integer");
    }
    applyNormalizedFilter(srcDst, [&](const cv::Mat& in, cv::Mat& out) {
        cv::GaussianBlur(in, out, cv::Size(kernelSize, kernelSize), sigma);
    });
}

// ---------- Mean (box) Blur ----------

Json::Value OpenCvMeanBlurOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvMeanBlurOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("filter");
    meta["tags"].append("mean");
    meta["tags"].append("box");
    meta["purpose"] = "Smooth image with a uniform (mean) kernel";
    meta["useCases"].append("Simple noise reduction before classification");
    return meta;
}

Json::Value OpenCvMeanBlurOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["kernelSize"] = makeIntegerParam("kernelSize", "Box kernel size (odd positive integer)", 3);
    return props;
}

void OpenCvMeanBlurOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const int kernelSize = getInt(params, "kernelSize", 3);
    if (!isValidKernelSize(kernelSize)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernelSize must be a positive odd integer");
    }
    applyNormalizedFilter(srcDst, [&](const cv::Mat& in, cv::Mat& out) {
        cv::blur(in, out, cv::Size(kernelSize, kernelSize));
    });
}

// ---------- Median Blur ----------

Json::Value OpenCvMedianBlurOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvMedianBlurOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("filter");
    meta["tags"].append("median");
    meta["purpose"] = "Remove salt-and-pepper noise using median filtering";
    return meta;
}

Json::Value OpenCvMedianBlurOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["kernelSize"] = makeIntegerParam("kernelSize", "Median kernel size (odd)", 3);
    return props;
}

void OpenCvMedianBlurOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const int kernelSize = getInt(params, "kernelSize", 3);
    if (!isValidKernelSize(kernelSize)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernelSize must be a positive odd integer");
    }
    cv::Mat mask;
    cv::compare(srcDst, srcDst, mask, cv::CMP_EQ);
    if (cv::countNonZero(mask) < mask.rows * mask.cols) {
        cv::Mat clean = srcDst.clone();
        clean.setTo(0.0f, ~mask);
        cv::medianBlur(clean, srcDst, kernelSize);
        srcDst.setTo(std::numeric_limits<float>::quiet_NaN(), ~mask);
    } else {
        cv::medianBlur(srcDst, srcDst, kernelSize);
    }
}

// ---------- Sobel ----------

Json::Value OpenCvSobelOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvSobelOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("edge");
    meta["tags"].append("sobel");
    meta["purpose"] = "Compute image gradients using the Sobel operator";
    return meta;
}

Json::Value OpenCvSobelOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["kernelSize"] = makeIntegerParam("kernelSize", "Sobel kernel size (1, 3, 5, or 7)", 3);
    props["dx"] = makeIntegerParam("dx", "Order of derivative in X direction", 1);
    props["dy"] = makeIntegerParam("dy", "Order of derivative in Y direction", 0);
    return props;
}

void OpenCvSobelOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const int kernelSize = getInt(params, "kernelSize", 3);
    const int dx = getInt(params, "dx", 1);
    const int dy = getInt(params, "dy", 0);
    if (kernelSize != 1 && kernelSize != 3 && kernelSize != 5 && kernelSize != 7) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Sobel kernelSize must be 1, 3, 5, or 7");
    }
    if (dx < 0 || dy < 0 || dx + dy <= 0) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Sobel requires dx + dy > 0 and non-negative orders");
    }

    cv::Mat mask;
    cv::compare(srcDst, srcDst, mask, cv::CMP_EQ);
    if (cv::countNonZero(mask) < mask.rows * mask.cols) {
        cv::Mat clean = srcDst.clone();
        clean.setTo(0.0f, ~mask);
        cv::Mat grad;
        cv::Sobel(clean, grad, CV_32F, dx, dy, kernelSize);
        grad.setTo(std::numeric_limits<float>::quiet_NaN(), ~mask);
        grad.copyTo(srcDst);
    } else {
        cv::Mat grad;
        cv::Sobel(srcDst, grad, CV_32F, dx, dy, kernelSize);
        grad.copyTo(srcDst);
    }
}

// ---------- Laplacian ----------

Json::Value OpenCvLaplacianOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvLaplacianOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("edge");
    meta["tags"].append("laplacian");
    meta["purpose"] = "Compute Laplacian second-derivative edges";
    return meta;
}

Json::Value OpenCvLaplacianOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["kernelSize"] = makeIntegerParam("kernelSize", "Laplacian kernel size (odd)", 3);
    return props;
}

void OpenCvLaplacianOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const int kernelSize = getInt(params, "kernelSize", 3);
    if (!isValidKernelSize(kernelSize)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "kernelSize must be a positive odd integer");
    }
    cv::Mat mask;
    cv::compare(srcDst, srcDst, mask, cv::CMP_EQ);
    if (cv::countNonZero(mask) < mask.rows * mask.cols) {
        cv::Mat clean = srcDst.clone();
        clean.setTo(0.0f, ~mask);
        cv::Mat lap;
        cv::Laplacian(clean, lap, CV_32F, kernelSize);
        lap.setTo(std::numeric_limits<float>::quiet_NaN(), ~mask);
        lap.copyTo(srcDst);
    } else {
        cv::Mat lap;
        cv::Laplacian(srcDst, lap, CV_32F, kernelSize);
        lap.copyTo(srcDst);
    }
}

// ---------- Canny ----------

Json::Value OpenCvCannyOperator::schema() const {
    return buildSchema(displayName(), description());
}

Json::Value OpenCvCannyOperator::metadata() const {
    Json::Value meta = RSOperator::metadata();
    meta["tags"].append("opencv");
    meta["tags"].append("edge");
    meta["tags"].append("canny");
    meta["purpose"] = "Detect edges using the Canny algorithm";
    return meta;
}

Json::Value OpenCvCannyOperator::operatorSchemaProperties() const {
    Json::Value props(Json::objectValue);
    props["threshold1"] = makeNumberParam("threshold1", "First threshold for hysteresis", 50.0);
    props["threshold2"] = makeNumberParam("threshold2", "Second threshold for hysteresis", 150.0);
    props["apertureSize"] = makeIntegerParam("apertureSize", "Sobel aperture size (odd)", 3);
    return props;
}

void OpenCvCannyOperator::applyFilter(cv::Mat& srcDst, const Json::Value& params) const {
    const double threshold1 = getDouble(params, "threshold1", 50.0);
    const double threshold2 = getDouble(params, "threshold2", 150.0);
    const int apertureSize = getInt(params, "apertureSize", 3);
    if (!isValidKernelSize(apertureSize)) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "apertureSize must be a positive odd integer");
    }

    cv::Mat gray;
    if (srcDst.channels() > 1) {
        cv::cvtColor(srcDst, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = srcDst;
    }

    cv::Mat gray8u;
    double minVal = 0.0, maxVal = 0.0;
    cv::minMaxLoc(gray, &minVal, &maxVal);
    if (maxVal > minVal) {
        gray.convertTo(gray8u, CV_8U, 255.0 / (maxVal - minVal),
                       -minVal * 255.0 / (maxVal - minVal));
    } else {
        gray.convertTo(gray8u, CV_8U);
    }

    cv::Mat edges;
    cv::Canny(gray8u, edges, threshold1, threshold2, apertureSize);
    edges.convertTo(srcDst, CV_32F);
}

} // namespace sicnu::operators::opencv
