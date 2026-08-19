// test_w5b_numerics.cpp — W5b regression for 353/369/395/297 residuals
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "processing/algorithms/terrain_analysis.h"
#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/image_enhancement.h"
#include "processing/algorithms/change_detection.h"
#include "processing/algorithms/spectral_classification.h"
#include "processing/algorithms/band_math.h"
#include "processing/algorithms/endmember_extraction.h"
#include <cmath>
#include <vector>
#include <limits>

using Catch::Approx;
static const float NODATA = -9999.0f;
static const float NaN = std::numeric_limits<float>::quiet_NaN();

// 353 R9: hillshade NaN neighbor falls back rather than NaN-poisoning
TEST_CASE("W5b hillshade NaN neighbor fallback", "[w5b][terrain]") {
    const int W=5,H=5;
    std::vector<float> dem(W*H, 100.0f);
    dem[1*W+2] = NaN; // north neighbor NaN
    dem[2*W+2] = 100.0f;
    std::vector<float> out(W*H, 0);
    REQUIRE(TerrainAnalysis::hillshade(dem.data(), out.data(), W,H,1.0f,NODATA,315,45));
    float v = out[2*W+2];
    REQUIRE(std::isfinite(v));
    REQUIRE(v >= 0.0f); REQUIRE(v <= 1.0f);
    // NaN center -> nodata
    dem[2*W+2]=NaN;
    REQUIRE(TerrainAnalysis::hillshade(dem.data(), out.data(), W,H,1.0f,NODATA,315,45));
    REQUIRE(out[2*W+2]==NODATA);
}

// 353 R16: math_utils excludes inf
TEST_CASE("W5b MathUtils inf excluded", "[w5b][math]") {
    float data[] = {1.0f, 2.0f, std::numeric_limits<float>::infinity(), 3.0f};
    auto s = MathUtils::computeStats(data,4);
    REQUIRE(s.validCount==3);
    REQUIRE(s.max==Approx(3.0f));
    REQUIRE(std::isfinite(s.mean));
    REQUIRE(std::isfinite(s.stddev));
    float data2[] = {1.0f, NODATA, std::numeric_limits<float>::infinity(), 2.0f};
    auto s2 = MathUtils::computeStatsWithNodata(data2,4,NODATA);
    REQUIRE(s2.validCount==2);
    REQUIRE(s2.max==Approx(2.0f));
}

// 353 R17: histogram equalize darkest maps to 0
TEST_CASE("W5b histogram equalize cdf_min", "[w5b][histeq]") {
    std::vector<float> in;
    for(int i=0;i<50;i++) in.push_back(10.0f);
    for(int i=0;i<5;i++) in.push_back(200.0f);
    std::vector<float> out(in.size());
    ImageEnhancement::histogramEqualize(in.data(), out.data(), in.size(), 256, NODATA);
    // darkest populated bin should map to 0 (not ~13)
    float minOut = 255; for(float v: out) if(std::isfinite(v)) minOut = std::min(minOut,v);
    REQUIRE(minOut == Approx(0.0f).margin(1.0f));
    float maxOut = 0; for(float v: out) if(std::isfinite(v)) maxOut = std::max(maxOut,v);
    REQUIRE(maxOut == Approx(255.0f).margin(1.0f));
}

// 353 R18: Otsu threshold uses (bins-1) grid
TEST_CASE("W5b Otsu threshold bin center", "[w5b][otsu]") {
    // Two wide clusters with a gap: 0.2-0.3 and 0.7-0.8 -> Otsu threshold in the gap.
    // (Delta-spike inputs are degenerate: every split between spikes has equal
    // between-class variance, so they cannot pin the threshold position.)
    std::vector<float> vals;
    for(int i=0;i<100;i++) vals.push_back(0.20f + 0.001f*i);
    for(int i=0;i<100;i++) vals.push_back(0.70f + 0.001f*i);
    float thr=0; REQUIRE(ChangeDetection::otsuThreshold(vals.data(), vals.size(), &thr, 256));
    // Any split inside the (0.3, 0.7) gap is an Otsu optimum; ties resolve to
    // the first bin after the low cluster. The R18 contract is that the direct
    // and histogram paths agree on the (bins-1) grid.
    REQUIRE(thr > 0.29f); REQUIRE(thr < 0.71f);
    // direct histogram path over the same data range: (bins-1) fill + threshold
    double minV=0.2,maxV=0.799; int bins=256;
    std::vector<double> hist(bins,0); for(float v: vals){int b=int((v-minV)/(maxV-minV)*(bins-1)); b=std::clamp(b,0,bins-1); hist[b]+=1;}
    float thr2=0; REQUIRE(ChangeDetection::otsuThresholdFromHistogram(minV,maxV,hist,200,&thr2));
    // thr and thr2 should be equal (same path)
    REQUIRE(thr == Approx(thr2).margin(1e-5));
}

// 395 N1: filters preserve NoData holes and convolve renormalizes
TEST_CASE("W5b filters preserve NaN center", "[w5b][filters]") {
    const int W=3,H=3;
    std::vector<float> in(W*H, 1.0f); in[1*W+1]=NaN;
    std::vector<float> out(W*H,0);
    ImageEnhancement::meanFilter(in.data(), out.data(), W,H,3);
    REQUIRE(std::isnan(out[1*W+1]));
    ImageEnhancement::medianFilter(in.data(), out.data(), W,H,3);
    REQUIRE(std::isnan(out[1*W+1]));
    // convolve with uniform kernel 3x3(1/9) on [NaN,1,1; ...]: the NaN center
    // stays NaN (hole preserved), and its neighbor renormalizes to ~1 not 0.66
    float kernel[9]; for(int i=0;i<9;i++) kernel[i]=1.0f/9.0f;
    std::vector<float> in2 = {NaN,1,1, 1,1,1, 1,1,1};
    std::vector<float> out2(9,0);
    ImageEnhancement::convolve(in2.data(), out2.data(), 3,3, kernel,3);
    REQUIRE(std::isnan(out2[0]));
    REQUIRE(out2[1] == Approx(1.0f).margin(0.1f));
}

// 395 N2: continuum removal uses wavelengths not indices
TEST_CASE("W5b continuum removal wavelengths", "[w5b][continuum]") {
    // Non-flat hull (endpoints differ) on a non-uniform wavelength grid:
    // wl [420,650,680,2100] spectrum [0.5,0.2,0.1,0.8].
    // Index hull at x=2: 0.5+2*(0.3/3)=0.7 -> CR=0.1/0.7=0.1429
    // Wavelength hull at 680: 0.5+(680-420)*(0.3/1680)=0.5464 -> CR=0.1/0.5464=0.1830
    float wl[4]={420,650,680,2100};
    float spec[4]={0.5f,0.2f,0.1f,0.8f};
    float outIdx[4], outWl[4];
    REQUIRE(SpectralClassification::continuumRemoval(spec, outIdx, 4, NODATA));
    REQUIRE(SpectralClassification::continuumRemoval(spec, wl, outWl, 4, NODATA));
    REQUIRE(outIdx[2] == Approx(0.1429f).margin(0.01f));
    REQUIRE(outWl[2] == Approx(0.1830f).margin(0.01f));
    REQUIRE(std::abs(outIdx[2]-outWl[2]) > 0.01f);
}

// 395 N3: band_math NaN propagation
TEST_CASE("W5b BandMath NaN propagation", "[w5b][bandmath]") {
    BandMath::BandData bands;
    bands[1] = std::vector<float>{NaN, 0.6f, 0.4f};
    std::vector<float> out(3);
    REQUIRE(BandMath::evaluate("b1 > 0.5 ? 1 : 0", bands, out.data(), 3));
    REQUIRE(std::isnan(out[0]));
    REQUIRE(out[1]==Approx(1.0f));
    REQUIRE(out[2]==Approx(0.0f));
    // comparison NaN -> NaN via > operator alone
    BandMath::BandData bands2;
    bands2[1]=std::vector<float>{NaN, 1.0f};
    bands2[2]=std::vector<float>{1.0f, NaN};
    std::vector<float> out2(2);
    REQUIRE(BandMath::evaluate("b1 < b2", bands2, out2.data(), 2));
    REQUIRE(std::isnan(out2[0]));
    REQUIRE(std::isnan(out2[1]));
    // logical NaN -> NaN
    REQUIRE(BandMath::evaluate("b1 && b2", bands2, out2.data(), 2));
    REQUIRE(std::isnan(out2[0]));
}

// 395 N4: PPI tail excludes invalid pixels
TEST_CASE("W5b PPI tail valid only", "[w5b][ppi]") {
    int bands=3, count=10, nEnd=8;
    std::vector<float> pix(count*bands, 1.0f);
    for(int p=0;p<4;p++) for(int b=0;b<bands;b++) pix[p*bands+b]=NaN; // first 4 invalid
    // make last 6 have varying extremes so extremeCounts >0 for some
    for(int p=4;p<count;p++) for(int b=0;b<bands;b++) pix[p*bands+b]=float(p*10+b);
    EndmemberExtraction::EndmemberResult res;
    QString err;
    REQUIRE(EndmemberExtraction::pixelPurityIndex(pix.data(), count, bands, nEnd, 16, &res, &err));
    // should have truncated to 6 valid (not 8) and no invalid indices
    REQUIRE((int)res.endmemberIndices.size() <= 6);
    for(int idx: res.endmemberIndices) REQUIRE(idx >=4);
    for(float v: res.endmembers) REQUIRE(std::isfinite(v));
}
