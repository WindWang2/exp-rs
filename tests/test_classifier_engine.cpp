// tests/test_classifier_engine.cpp — D15 Package C.
//
// Ground-truth policy: three-Gaussian synthetic set with known means
// ([-5,-5], [0,5], [5,-5], sigma=0.5) — distribution centres and the
// separability floor are analytic facts of the fixture, not outputs of the
// code under test.  Probability vectors must sum to exactly 1 (closed-form
// property of the contract).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

#include "processing/algorithms/classifier_engine.h"

using Catch::Matchers::WithinAbs;
using rs::processing::ClassifierAlgorithm;
using rs::processing::ClassifierEngine;
using rs::processing::ClassifierHyperparameters;
using rs::processing::IClassifierModel;

namespace
{
  // Deterministic gaussian fixture generator (Box-Muller over an LCG).
  class GaussianMixture
  {
    public:
      explicit GaussianMixture( uint64_t seed ) : m_state( seed ) {}

      std::vector<float> sample( size_t perClass, const double means[][2], size_t numClasses, double sigma )
      {
        std::vector<float> x;
        x.reserve( perClass * numClasses * 2 );
        for ( size_t c = 0; c < numClasses; ++c )
          for ( size_t i = 0; i < perClass; ++i )
          {
            x.push_back( static_cast<float>( means[c][0] + sigma * normal() ) );
            x.push_back( static_cast<float>( means[c][1] + sigma * normal() ) );
          }
        return x;
      }

    private:
      double normal()
      {
        if ( m_hasSpare )
        {
          m_hasSpare = false;
          return m_spare;
        }
        double u = 0.0, v = 0.0, s = 0.0;
        do
        {
          u = uniform() * 2.0 - 1.0;
          v = uniform() * 2.0 - 1.0;
          s = u * u + v * v;
        } while ( s >= 1.0 || s == 0.0 );
        const double r = std::sqrt( -2.0 * std::log( s ) / s );
        m_spare = v * r;
        m_hasSpare = true;
        return u * r;
      }
      double uniform()
      {
        m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>( ( m_state >> 33 ) & 0xFFFFFF ) / 16777216.0;
      }
      uint64_t m_state;
      double m_spare = 0.0;
      bool m_hasSpare = false;
  };

  constexpr double kMeans3[3][2] = { { -5.0, -5.0 }, { 0.0, 5.0 }, { 5.0, -5.0 } };

  struct Dataset
  {
    std::vector<float> trainX, testX;
    std::vector<int> trainY, testY;
  };

  // Deterministic round-robin-by-3 split: sample i goes to train unless
  // (i % 10) >= 7  => 70/30 by construction, classes balanced in both.
  Dataset makeDataset( uint64_t seed, size_t perClass = 100 )
  {
    GaussianMixture gen( seed );
    std::vector<float> allX = gen.sample( perClass, kMeans3, 3, 0.5 );
    std::vector<int> allY;
    for ( size_t c = 0; c < 3; ++c )
      allY.insert( allY.end(), perClass, static_cast<int>( c ) );

    Dataset d;
    for ( size_t i = 0; i < allY.size(); ++i )
    {
      const bool toTest = ( i % 10 ) >= 7;
      ( toTest ? d.testX : d.trainX ).insert( ( toTest ? d.testX : d.trainX ).end(),
                                              { allX[2 * i], allX[2 * i + 1] } );
      ( toTest ? d.testY : d.trainY ).push_back( allY[i] );
    }
    return d;
  }

  double accuracy( const IClassifierModel &m, const std::vector<float> &x, const std::vector<int> &y )
  {
    size_t hit = 0;
    for ( size_t i = 0; i < y.size(); ++i )
      hit += ( m.predictOne( std::span( x ).subspan( 2 * i, 2 ) ) == y[i] ) ? 1 : 0;
    return static_cast<double>( hit ) / static_cast<double>( y.size() );
  }
} // namespace

TEST_CASE( "Engine factory and untrained-model guards", "[d15][classifier]" )
{
  ClassifierHyperparameters p;
  for ( const auto algo : { ClassifierAlgorithm::RandomForest, ClassifierAlgorithm::SupportVectorMachine,
                            ClassifierAlgorithm::KMeans, ClassifierAlgorithm::Isodata,
                            ClassifierAlgorithm::NormalBayes } )
  {
    auto m = ClassifierEngine::create( algo, p );
    REQUIRE( m != nullptr );
    REQUIRE_FALSE( m->isTrained() );
    const std::vector<float> probe = { 0.0f, 0.0f };
    REQUIRE( m->predictOne( probe ) == -1 );
    REQUIRE( m->predictProbabilities( probe ).empty() );
    // Empty fit keeps the model untrained rather than crashing.
    m->fit( {}, {}, 0, 2 );
    REQUIRE_FALSE( m->isTrained() );
  }
  REQUIRE( ClassifierEngine::create( static_cast<ClassifierAlgorithm>( 99 ), p ) == nullptr );
}

TEST_CASE( "KMeans recovers the three analytic distribution centres", "[d15][classifier]" )
{
  const Dataset d = makeDataset( 20260914ULL );
  ClassifierHyperparameters p;
  p.kClusters = 3;
  p.maxIterations = 100;
  auto km = ClassifierEngine::create( ClassifierAlgorithm::KMeans, p );
  km->fit( d.trainX, {}, d.trainY.size(), 2 );
  REQUIRE( km->isTrained() );

  const auto centroids = km->clusterCentroids();
  REQUIRE( centroids.size() == 3 );
  for ( const auto &center : kMeans3 )
  {
    double best = std::numeric_limits<double>::infinity();
    for ( const auto &c : centroids )
      best = std::min( best, std::hypot( c[0] - center[0], c[1] - center[1] ) );
    // Sampling-error bound: with 70 train samples per class and sigma=0.5
    // the per-dimension centroid error is 0.5/sqrt(70) ~ 0.06, so the 2-D
    // distance to the analytic centre is bounded by 3 * 0.06 * sqrt(2)
    // ~ 0.25 at three sigma (this seed realizes ~0.12).
    REQUIRE( best < 0.25 );
  }
  // Unsupervised hard assignment still yields a partition consistent with
  // the true labels up to permutation (accuracy vs best permutation >= 0.97).
  double bestAcc = 0.0;
  std::vector<int> perm = { 0, 1, 2 };
  do
  {
    size_t hit = 0;
    for ( size_t i = 0; i < d.trainY.size(); ++i )
    {
      const int cluster = km->predictOne( std::span( d.trainX ).subspan( 2 * i, 2 ) );
      hit += ( perm[static_cast<size_t>( cluster )] == d.trainY[i] ) ? 1 : 0;
    }
    bestAcc = std::max( bestAcc, static_cast<double>( hit ) / d.trainY.size() );
  } while ( std::next_permutation( perm.begin(), perm.end() ) );
  REQUIRE( bestAcc >= 0.97 );
}

TEST_CASE( "Random forest reaches the separability floor with conserved posteriors", "[d15][classifier]" )
{
  const Dataset d = makeDataset( 1234567ULL );
  ClassifierHyperparameters p;
  p.rfNumTrees = 60;
  auto rf = ClassifierEngine::create( ClassifierAlgorithm::RandomForest, p );
  rf->fit( d.trainX, d.trainY, d.trainY.size(), 2 );
  REQUIRE( rf->isTrained() );
  REQUIRE( accuracy( *rf, d.testX, d.testY ) >= 0.98 );

  auto probs = rf->predictProbabilities( std::span( d.testX ).subspan( 0, 2 ) );
  REQUIRE( probs.size() == 3 );
  // float32 storage: per-entry rounding ~1e-7, so the sum holds to 1e-6.
  REQUIRE_THAT( std::accumulate( probs.begin(), probs.end(), 0.0 ), WithinAbs( 1.0, 1e-6 ) );
  REQUIRE( rf->clusterCentroids().empty() );
}

TEST_CASE( "NormalBayes reaches the separability floor with true posteriors", "[d15][classifier]" )
{
  const Dataset d = makeDataset( 7654321ULL );
  ClassifierHyperparameters p;
  auto nb = ClassifierEngine::create( ClassifierAlgorithm::NormalBayes, p );
  nb->fit( d.trainX, d.trainY, d.trainY.size(), 2 );
  REQUIRE( nb->isTrained() );
  REQUIRE( accuracy( *nb, d.testX, d.testY ) >= 0.98 );

  auto probs = nb->predictProbabilities( std::span( d.testX ).subspan( 0, 2 ) );
  REQUIRE( probs.size() == 3 );
  REQUIRE_THAT( std::accumulate( probs.begin(), probs.end(), 0.0 ), WithinAbs( 1.0, 1e-6 ) );

  // Batch prediction agrees with single-sample prediction (same kernel).
  std::vector<int> batch( d.testY.size(), -1 );
  nb->predictBatch( d.testX, batch, d.testY.size(), 2 );
  for ( size_t i = 0; i < d.testY.size(); ++i )
    REQUIRE( batch[i] == nb->predictOne( std::span( d.testX ).subspan( 2 * i, 2 ) ) );
}

TEST_CASE( "RBF SVM separates the three blobs and predicts 1-hot", "[d15][classifier]" )
{
  const Dataset d = makeDataset( 24680ULL );
  ClassifierHyperparameters p;
  p.svmC = 10.0;
  p.svmGamma = 0.1;
  auto svm = ClassifierEngine::create( ClassifierAlgorithm::SupportVectorMachine, p );
  svm->fit( d.trainX, d.trainY, d.trainY.size(), 2 );
  REQUIRE( svm->isTrained() );
  REQUIRE( accuracy( *svm, d.testX, d.testY ) >= 0.98 );

  const auto probs = svm->predictProbabilities( std::span( d.testX ).subspan( 0, 2 ) );
  REQUIRE( probs.size() == 3 );
  const double sum = std::accumulate( probs.begin(), probs.end(), 0.0 );
  REQUIRE_THAT( sum, WithinAbs( 1.0, 1e-6 ) );
  const int hot = static_cast<int>( std::distance( probs.begin(), std::max_element( probs.begin(), probs.end() ) ) );
  REQUIRE( svm->predictOne( std::span( d.testX ).subspan( 0, 2 ) ) == hot );
}

TEST_CASE( "ISODATA splits a fused bimodal cluster once", "[d15][classifier]" )
{
  // Two tight groups 10 apart presented as ONE class; sigma along the
  // separating axis (~5) far exceeds theta_S=1 => the canonical split rule
  // must fire, and theta_C=2 must not re-merge the 10-apart result.
  GaussianMixture gen( 99ULL );
  std::vector<float> x;
  const double groupMeans[2][2] = { { 0.0, 0.0 }, { 10.0, 0.0 } };
  x = gen.sample( 100, groupMeans, 2, 0.5 );

  ClassifierHyperparameters p;
  p.kClusters = 1;
  p.isodataMinClusterSize = 10;
  p.isodataMaxStdDev = 1.0;
  p.isodataMinClusterDist = 2.0;
  p.maxIterations = 50;
  auto iso = ClassifierEngine::create( ClassifierAlgorithm::Isodata, p );
  iso->fit( x, {}, 200, 2 );
  REQUIRE( iso->isTrained() );

  const auto centroids = iso->clusterCentroids();
  REQUIRE( centroids.size() == 2 );
  for ( const auto &g : groupMeans )
  {
    double best = std::numeric_limits<double>::infinity();
    for ( const auto &c : centroids )
      best = std::min( best, std::hypot( c[0] - g[0], c[1] - g[1] ) );
    REQUIRE( best < 0.3 );
  }
}

TEST_CASE( "NormalBayes posterior matches the closed form for correlated covariances",
           "[d15][classifier]" )
{
  // Hand-crafted 3-sample classes realize the EXACT sample covariance
  // [[1,-0.5],[-0.5,1]] (off-diagonal mass forces a non-diagonal Cholesky
  // factor, so a wrong triangular back-substitution order is observable):
  //   offsets (1,0), (-1,1), (0,-1): sum 0, sum ddT/2 = [[1,-0.5],[-0.5,1]].
  // Class 0 at the origin, class 1 shifted by (2,2); equal priors.
  // For x = (2,2) = mu1: q1 = 0, q0 = (2,2) Sigma^-1 (2,2)' with
  // Sigma^-1 = (1/0.75)[[1,0.5],[0.5,1]] -> q0 = 16.
  // g0 = -0.5*ln(0.75) - 8, g1 = -0.5*ln(0.75); posterior
  // p1 = 1 / (1 + e^8) = 0.99966458...
  // (an ascending-order back substitution yields p1 ~ 0.997 — flagged).
  std::vector<float> x = {
    1.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f,          // class 0 offsets
    3.0f, 2.0f, 1.0f, 3.0f, 2.0f, 1.0f,            // class 1 = same + (2,2)
  };
  std::vector<int> y = { 0, 0, 0, 1, 1, 1 };
  ClassifierHyperparameters p;
  auto nb = ClassifierEngine::create( ClassifierAlgorithm::NormalBayes, p );
  nb->fit( x, y, 6, 2 );
  REQUIRE( nb->isTrained() );

  const std::vector<float> atClass1Mean = { 2.0f, 2.0f };
  const auto probs = nb->predictProbabilities( atClass1Mean );
  REQUIRE( probs.size() == 2 );
  REQUIRE_THAT( probs[1], WithinAbs( 1.0 / ( 1.0 + std::exp( -8.0 ) ), 1e-6 ) );
  REQUIRE( nb->predictOne( atClass1Mean ) == 1 );
}

TEST_CASE( "NormalBayes ridge survives a singular class covariance", "[d15][classifier]" )
{
  // Class 0: 30 identical samples (zero covariance); class 1: tight noise.
  std::vector<float> x( 60 * 2 );
  std::vector<int> y( 60 );
  GaussianMixture gen( 555ULL );
  for ( int i = 0; i < 30; ++i )
  {
    x[2 * i] = 0.0f;
    x[2 * i + 1] = 0.0f;
    y[static_cast<size_t>( i )] = 0;
  }
  for ( int i = 30; i < 60; ++i )
  {
    x[2 * i] = static_cast<float>( 4.0 + 0.1 * gen.sample( 1, kMeans3, 1, 1.0 )[0] );
    x[2 * i + 1] = static_cast<float>( 4.0 + 0.1 * gen.sample( 1, kMeans3, 1, 1.0 )[0] );
    y[static_cast<size_t>( i )] = 1;
  }
  ClassifierHyperparameters p;
  auto nb = ClassifierEngine::create( ClassifierAlgorithm::NormalBayes, p );
  nb->fit( x, y, 60, 2 );
  const std::vector<float> atOrigin = { 0.0f, 0.0f };
  const std::vector<float> atFour = { 4.0f, 4.0f };
  REQUIRE( nb->isTrained() );
  REQUIRE( nb->predictOne( atOrigin ) == 0 );
  REQUIRE( nb->predictOne( atFour ) == 1 );
}
