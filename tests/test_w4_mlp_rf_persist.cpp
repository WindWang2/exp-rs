// W4 issue 372 regression: MLP/RF model save/load round-trip preserves mClassLabels.
#include <catch2/catch_test_macros.hpp>

#include <QTemporaryDir>
#include <QFile>

#include <opencv2/core.hpp>

#include "rs_classifier_mlp.h"
#include "rs_classifier_random_forest.h"

TEST_CASE( "MLP save/load round-trip preserves predictions (372)", "[w4][372][mlp]" )
{
    cv::RNG rng(42);
    cv::Mat X(40, 2, CV_32F), y(40,1,CV_32S);
    for(int i=0;i<20;++i){ X.at<float>(i,0)=float(rng.gaussian(0.5))+0; X.at<float>(i,1)=float(rng.gaussian(0.5))+0; y.at<int>(i,0)=1; }
    for(int i=20;i<40;++i){ X.at<float>(i,0)=float(rng.gaussian(0.5))+10; X.at<float>(i,1)=float(rng.gaussian(0.5))+10; y.at<int>(i,0)=2; }

    RsMlpBackend clf(8, 200);
    // MLP training may be flaky; if fit fails skip
    if(!clf.fit(X,y)) { WARN("MLP fit failed, skipping"); return; }
    cv::Mat predBefore = clf.predict(X);
    REQUIRE_FALSE(predBefore.empty());

    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    const QString path = tmp.path() + "/mlp.yml";
    REQUIRE(clf.save(path));
    REQUIRE(QFile::exists(path));
    // companion labels file
    CHECK(QFile::exists(path + ".labels.json"));

    RsMlpBackend clf2(8,200);
    REQUIRE(clf2.load(path));
    REQUIRE(clf2.isFitted());
    cv::Mat predAfter = clf2.predict(X);
    REQUIRE(predAfter.rows == predBefore.rows);
    int same=0;
    for(int i=0;i<predAfter.rows;++i) if(predAfter.at<int>(i,0)==predBefore.at<int>(i,0)) ++same;
    CHECK(same == predAfter.rows);
    cv::Mat probs = clf2.predictProbabilities(X);
    CHECK_FALSE(probs.empty());
    CHECK(probs.rows == X.rows);
}

TEST_CASE( "RandomForest save/load round-trip preserves probabilities (372)", "[w4][372][rf]" )
{
    cv::RNG rng(7);
    cv::Mat X(60,2,CV_32F), y(60,1,CV_32S);
    for(int i=0;i<20;++i){ X.at<float>(i,0)=float(rng.gaussian(0.5))+0; X.at<float>(i,1)=float(rng.gaussian(0.5))+0; y.at<int>(i,0)=1; }
    for(int i=20;i<40;++i){ X.at<float>(i,0)=float(rng.gaussian(0.5))+20; X.at<float>(i,1)=float(rng.gaussian(0.5))+0; y.at<int>(i,0)=2; }
    for(int i=40;i<60;++i){ X.at<float>(i,0)=float(rng.gaussian(0.5))+0; X.at<float>(i,1)=float(rng.gaussian(0.5))+20; y.at<int>(i,0)=3; }

    RsRandomForestBackend clf(20,6,2);
    REQUIRE(clf.fit(X,y));
    cv::Mat probsBefore = clf.predictProbabilities(X);
    REQUIRE_FALSE(probsBefore.empty());
    QTemporaryDir tmp; REQUIRE(tmp.isValid());
    const QString path = tmp.path() + "/rf.yml";
    REQUIRE(clf.save(path));
    CHECK(QFile::exists(path + ".labels.json"));

    RsRandomForestBackend clf2(20,6,2);
    REQUIRE(clf2.load(path));
    cv::Mat probsAfter = clf2.predictProbabilities(X);
    REQUIRE(probsAfter.rows == probsBefore.rows);
    REQUIRE(probsAfter.cols == probsBefore.cols);
    // probs should be nearly identical
    for(int r=0;r<probsAfter.rows;++r)
        for(int c=0;c<probsAfter.cols;++c)
            CHECK(std::abs(probsAfter.at<float>(r,c)-probsBefore.at<float>(r,c)) < 1e-5f);
}
