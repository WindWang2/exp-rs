#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <opencv2/core.hpp>
#include "rs_cross_validation.h"
#include "rs_classifier_normalbayes.h"

using Catch::Approx;

namespace {
void makeGaussianData(cv::Mat &X, cv::Mat &y, int perClass = 200, int seed = 42) {
    cv::RNG rng(seed);
    const int total = perClass * 3;
    X.create(total, 2, CV_32F);
    y.create(total, 1, CV_32S);
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(i, 1) = float(rng.gaussian(2.0)) + 5.0f;
        y.at<int>(i, 0) = 1;
    }
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(perClass + i, 0) = float(rng.gaussian(2.0)) + 20.0f;
        X.at<float>(perClass + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(perClass + i, 0) = 2;
    }
    for (int i = 0; i < perClass; ++i) {
        X.at<float>(2*perClass + i, 0) = float(rng.gaussian(2.0)) + 5.0f;
        X.at<float>(2*perClass + i, 1) = float(rng.gaussian(2.0)) + 20.0f;
        y.at<int>(2*perClass + i, 0) = 3;
    }
}
}

TEST_CASE("CV: NormalBayes 5-fold on 3 Gaussians yields mean > 0.85", "[classify][cv]") {
    cv::Mat X, y;
    makeGaussianData(X, y);
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.ok());
    REQUIRE(r.foldAccuracies.size() == 5);
    REQUIRE(r.meanAccuracy > 0.85);
}

TEST_CASE("CV: std accuracy non-negative and bounded", "[classify][cv]") {
    cv::Mat X, y;
    makeGaussianData(X, y);
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.stdAccuracy >= 0.0);
    REQUIRE(r.stdAccuracy <= 0.5);
}

TEST_CASE("CV: empty data returns error", "[classify][cv]") {
    cv::Mat X, y;
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE_FALSE(r.ok());
}

TEST_CASE("CV: class with < k samples folded to train only", "[classify][cv]") {
    cv::Mat X(13, 2, CV_32F);
    cv::Mat y(13, 1, CV_32S);
    cv::RNG rng(7);
    for (int i = 0; i < 10; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(1.0));
        X.at<float>(i, 1) = float(rng.gaussian(1.0));
        y.at<int>(i, 0) = 1;
    }
    for (int i = 10; i < 13; ++i) {
        X.at<float>(i, 0) = float(rng.gaussian(1.0)) + 10.0f;
        X.at<float>(i, 1) = float(rng.gaussian(1.0)) + 10.0f;
        y.at<int>(i, 0) = 2;
    }
    auto factory = []() -> std::unique_ptr<RsClassifierBackend> {
        return std::make_unique<RsClassifierNormalBayes>();
    };
    auto r = RsCrossValidation::kFold(X, y, factory, 5);
    REQUIRE(r.ok());
    REQUIRE(r.foldAccuracies.size() == 5);
}
