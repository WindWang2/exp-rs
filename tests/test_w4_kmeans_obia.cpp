// W4 issue 371 regression: OBIA KMeans cluster->class remap via Hungarian.
#include <catch2/catch_test_macros.hpp>

#include <QMap>
#include <QVector>

#include <opencv2/core.hpp>

#include "rs_classifier_kmeans.h"
#include "rs_object_classify.h"

TEST_CASE( "RsObjectClassify KMeans remaps cluster ids to true labels (371)", "[w4][371][kmeans][obia]" )
{
    // 6 segments: 3 of class 1 (mean 10), 3 of class 2 (mean 100), 2 bands.
    cv::Mat X(6,2,CV_32F);
    QVector<quint32> segIds = {1,2,3,4,5,6};
    QMap<quint32,int> training;
    // class 1 segments 1,2
    X.at<float>(0,0)=10; X.at<float>(0,1)=10;
    X.at<float>(1,0)=11; X.at<float>(1,1)=9;
    X.at<float>(2,0)=9;  X.at<float>(2,1)=10; // to predict
    // class 2 segments 4,5
    X.at<float>(3,0)=100; X.at<float>(3,1)=100;
    X.at<float>(4,0)=101; X.at<float>(4,1)=99;
    X.at<float>(5,0)=99;  X.at<float>(5,1)=101; // to predict

    training[1]=1;
    training[2]=1;
    training[4]=2;
    training[5]=2;

    RsClassifierKMeans kmeans(2);
    auto res = RsObjectClassify::classify(X, segIds, training, kmeans, false);
    INFO(res.errorMessage.toStdString());
    REQUIRE(res.ok);
    // Trained segments should map to true labels
    CHECK(res.segmentClasses[1]==1);
    CHECK(res.segmentClasses[2]==1);
    CHECK(res.segmentClasses[4]==2);
    CHECK(res.segmentClasses[5]==2);
    // Unlabeled segments 3,6 must also be remapped to 1/2 respectively (not raw 1..K scrambled)
    CHECK(res.segmentClasses[3]==1);
    CHECK(res.segmentClasses[6]==2);
    // needsLabelRemap true for this training set
    CHECK(kmeans.needsLabelRemap()==true);
}

TEST_CASE( "RsObjectClassify predictWithProbabilities does not double-infer (387)", "[w4][387][obia]" )
{
    // Simple check that combined path via object classify does not crash
    cv::Mat X(4,2,CV_32F);
    QVector<quint32> segIds = {10,20,30,40};
    QMap<quint32,int> training;
    X.at<float>(0,0)=0; X.at<float>(0,1)=0; training[10]=1;
    X.at<float>(1,0)=0; X.at<float>(1,1)=1; training[20]=1;
    X.at<float>(2,0)=10; X.at<float>(2,1)=10; training[30]=2;
    X.at<float>(3,0)=10; X.at<float>(3,1)=11;
    // Use NormalBayes backend which now has predictWithProbabilities
    class DummyNB : public RsClassifierBackend {
    public:
        bool fit(const cv::Mat&, const cv::Mat&) override { return true; }
        cv::Mat predict(const cv::Mat& X) const override { cv::Mat o(X.rows,1,CV_32S); for(int i=0;i<X.rows;++i) o.at<int>(i,0)= (X.at<float>(i,0)>5 ? 2:1); return o; }
        cv::Mat predictProbabilities(const cv::Mat& X) const override { cv::Mat p(X.rows,2,CV_32F, cv::Scalar(0.5f)); return p; }
        bool predictWithProbabilities(const cv::Mat& X, cv::Mat& ol, cv::Mat& op) const override { ol=predict(X); op=predictProbabilities(X); return true; }
        bool supportsProbabilities() const override { return true; }
        QString name() const override { return "dummy"; }
        bool isFitted() const override { return true; }
    } dummy;
    auto res = RsObjectClassify::classify(X, segIds, training, dummy, false);
    REQUIRE(res.ok);
    CHECK(res.segmentClasses.size()==4);
}
