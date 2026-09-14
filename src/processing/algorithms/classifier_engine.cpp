// src/processing/algorithms/classifier_engine.cpp — D15 Package C kernels.
//
// Self-contained implementations (no OpenCV): KMeans (k-means++), ISODATA
// (split/merge), Random Forest (CART + bagging + sqrt(d) subspace),
// C-soft-margin RBF SVM (simplified SMO, one-vs-one) and NormalBayes
// (full-covariance MAP with ridge regularisation).  All randomness flows
// through one seeded LCG so fits are reproducible.
#include "classifier_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace rs::processing
{
namespace
{
  constexpr double kTiny = 1e-12;

  class Lcg
  {
    public:
      explicit Lcg( uint64_t seed )
          : m_state( seed ? seed : 42ULL )
      {
      }
      double nextUnit()
      {
        m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>( ( m_state >> 33 ) & 0xFFFFFF ) / 16777216.0;
      }
      size_t nextIndex( size_t bound )
      {
        return bound == 0 ? 0 : static_cast<size_t>( static_cast<uint64_t>( nextUnit() * 16777216.0 ) % bound );
      }

    private:
      uint64_t m_state;
  };

  bool validMatrix( std::span<const float> x, size_t n, size_t d )
  {
    return n > 0 && d > 0 && x.size() >= n * d;
  }

  bool validLabels( std::span<const int> labels, size_t n )
  {
    if ( labels.size() < n )
      return false;
    for ( size_t i = 0; i < n; ++i )
      if ( labels[i] < 0 )
        return false;
    return true;
  }

  std::vector<int> distinctAscending( std::span<const int> labels, size_t n )
  {
    std::vector<int> classes( labels.begin(), labels.begin() + std::min( labels.size(), n ) );
    std::sort( classes.begin(), classes.end() );
    classes.erase( std::unique( classes.begin(), classes.end() ), classes.end() );
    return classes;
  }

  std::vector<double> pointToDouble( std::span<const float> x, size_t i, size_t d )
  {
    return std::vector<double>( x.begin() + static_cast<std::ptrdiff_t>( i * d ),
                                x.begin() + static_cast<std::ptrdiff_t>( ( i + 1 ) * d ) );
  }

  double squaredDistanceTo( std::span<const float> x, size_t i, size_t d, const std::vector<double> &center )
  {
    double sum = 0.0;
    for ( size_t f = 0; f < d; ++f )
    {
      const double diff = x[i * d + f] - center[f];
      sum += diff * diff;
    }
    return sum;
  }

  // ------------------------------------------------------------------
  // KMeans / ISODATA shared core.
  // ------------------------------------------------------------------
  std::vector<std::vector<double>> kmeansPlusPlus( std::span<const float> x, size_t n, size_t d,
                                                   int k, Lcg &rng )
  {
    std::vector<std::vector<double>> centers;
    centers.reserve( static_cast<size_t>( k ) );
    centers.push_back( pointToDouble( x, rng.nextIndex( n ), d ) );
    std::vector<double> minDist( n, std::numeric_limits<double>::infinity() );
    while ( static_cast<int>( centers.size() ) < k )
    {
      const std::vector<double> &latest = centers.back();
      double sum = 0.0;
      for ( size_t i = 0; i < n; ++i )
      {
        minDist[i] = std::min( minDist[i], squaredDistanceTo( x, i, d, latest ) );
        sum += minDist[i];
      }
      if ( sum <= kTiny )
      {
        centers.push_back( pointToDouble( x, rng.nextIndex( n ), d ) );
        continue;
      }
      const double target = rng.nextUnit() * sum;
      double acc = 0.0;
      size_t chosen = n - 1;
      for ( size_t i = 0; i < n; ++i )
      {
        acc += minDist[i];
        if ( acc >= target )
        {
          chosen = i;
          break;
        }
      }
      centers.push_back( pointToDouble( x, chosen, d ) );
    }
    return centers;
  }

  std::vector<size_t> assignPoints( std::span<const float> x, size_t n, size_t d,
                                    const std::vector<std::vector<double>> &centers )
  {
    std::vector<size_t> assignment( n, 0 );
    for ( size_t i = 0; i < n; ++i )
    {
      double best = std::numeric_limits<double>::infinity();
      size_t bestK = 0;
      for ( size_t c = 0; c < centers.size(); ++c )
      {
        const double dist = squaredDistanceTo( x, i, d, centers[c] );
        if ( dist < best )
        {
          best = dist;
          bestK = c;
        }
      }
      assignment[i] = bestK;
    }
    return assignment;
  }

  class ClusteringModelBase : public IClassifierModel
  {
    public:
      bool isTrained() const override { return m_trained; }
      std::vector<std::vector<float>> clusterCentroids() const override
      {
        std::vector<std::vector<float>> out;
        out.reserve( m_centers.size() );
        for ( const auto &c : m_centers )
          out.emplace_back( c.begin(), c.end() );
        return out;
      }
      int predictOne( std::span<const float> sample ) const override
      {
        if ( !m_trained || sample.size() != m_dim )
          return -1;
        double best = std::numeric_limits<double>::infinity();
        size_t bestK = 0;
        for ( size_t c = 0; c < m_centers.size(); ++c )
        {
          double dist = 0.0;
          for ( size_t f = 0; f < m_dim; ++f )
          {
            const double diff = sample[f] - m_centers[c][f];
            dist += diff * diff;
          }
          if ( dist < best )
          {
            best = dist;
            bestK = c;
          }
        }
        return static_cast<int>( bestK );
      }
      void predictBatch( std::span<const float> in, std::span<int> out, size_t n, size_t d ) const override
      {
        if ( out.size() < n || d != m_dim )
          return;
        for ( size_t i = 0; i < n; ++i )
          out[i] = predictOne( in.subspan( i * d, d ) );
      }
      std::vector<float> predictProbabilities( std::span<const float> sample ) const override
      {
        const int k = predictOne( sample );
        if ( k < 0 )
          return {};
        std::vector<float> probs( m_centers.size(), 0.0f );
        probs[static_cast<size_t>( k )] = 1.0f;
        return probs;
      }

    protected:
      std::vector<std::vector<double>> m_centers;
      size_t m_dim = 0;
      bool m_trained = false;
  };

  class KMeansModel final : public ClusteringModelBase
  {
    public:
      explicit KMeansModel( const ClassifierHyperparameters &p )
          : m_p( p )
      {
      }

      void fit( std::span<const float> x, std::span<const int>, size_t n, size_t d ) override
      {
        m_trained = false;
        if ( !validMatrix( x, n, d ) )
          return;
        Lcg rng( m_p.randomSeed );
        const int k = std::clamp( m_p.kClusters, 1, static_cast<int>( n ) );
        m_centers = kmeansPlusPlus( x, n, d, k, rng );
        m_dim = d;

        for ( int iter = 0; iter < std::max( 1, m_p.maxIterations ); ++iter )
        {
          const auto assignment = assignPoints( x, n, d, m_centers );
          std::vector<size_t> counts( m_centers.size(), 0 );
          double maxShift = 0.0;
          for ( size_t c = 0; c < m_centers.size(); ++c )
          {
            std::vector<double> mean( d, 0.0 );
            for ( size_t i = 0; i < n; ++i )
            {
              if ( assignment[i] != c )
                continue;
              for ( size_t f = 0; f < d; ++f )
                mean[f] += x[i * d + f];
              ++counts[c];
            }
            if ( counts[c] == 0 )
              continue;
            for ( size_t f = 0; f < d; ++f )
              mean[f] /= static_cast<double>( counts[c] );
            double shift = 0.0;
            for ( size_t f = 0; f < d; ++f )
              shift += ( mean[f] - m_centers[c][f] ) * ( mean[f] - m_centers[c][f] );
            maxShift = std::max( maxShift, std::sqrt( shift ) );
            m_centers[c] = std::move( mean );
          }
          // Reseed empty clusters at the farthest points; the convergence
          // check is skipped for this iteration so reseeded centres get a
          // full Lloyd pass before the loop may exit.
          bool reseeded = false;
          for ( size_t c = 0; c < m_centers.size(); ++c )
          {
            if ( counts[c] > 0 )
              continue;
            reseeded = true;
            size_t far = 0;
            double farDist = -1.0;
            for ( size_t i = 0; i < n; ++i )
            {
              double nearest = std::numeric_limits<double>::infinity();
              for ( const auto &center : m_centers )
                nearest = std::min( nearest, squaredDistanceTo( x, i, d, center ) );
              if ( nearest > farDist )
              {
                farDist = nearest;
                far = i;
              }
            }
            m_centers[c] = pointToDouble( x, far, d );
          }
          if ( !reseeded && maxShift < m_p.convergenceEpsilon )
            break;
        }
        m_trained = true;
      }

    private:
      ClassifierHyperparameters m_p;
  };

  class IsodataModel final : public ClusteringModelBase
  {
    public:
      explicit IsodataModel( const ClassifierHyperparameters &p )
          : m_p( p )
      {
      }

      void fit( std::span<const float> x, std::span<const int>, size_t n, size_t d ) override
      {
        m_trained = false;
        if ( !validMatrix( x, n, d ) )
          return;
        Lcg rng( m_p.randomSeed );
        const int k = std::clamp( m_p.kClusters, 1, static_cast<int>( n ) );
        m_centers = kmeansPlusPlus( x, n, d, k, rng );
        m_dim = d;

        constexpr size_t kMaxClusters = 32;
        const int minSize = std::max( 1, m_p.isodataMinClusterSize );
        for ( int iter = 0; iter < std::max( 1, m_p.maxIterations ); ++iter )
        {
          const auto assignment = assignPoints( x, n, d, m_centers );

          std::vector<size_t> counts( m_centers.size(), 0 );
          std::vector<std::vector<double>> means( m_centers.size(), std::vector<double>( d, 0.0 ) );
          for ( size_t i = 0; i < n; ++i )
          {
            const size_t c = assignment[i];
            ++counts[c];
            for ( size_t f = 0; f < d; ++f )
              means[c][f] += x[i * d + f];
          }
          for ( size_t c = 0; c < m_centers.size(); ++c )
            if ( counts[c] > 0 )
              for ( size_t f = 0; f < d; ++f )
                means[c][f] /= static_cast<double>( counts[c] );

          // Discard undersized clusters (never the last one).
          std::vector<std::vector<double>> kept;
          for ( size_t c = 0; c < m_centers.size(); ++c )
            if ( counts[c] >= static_cast<size_t>( minSize ) || m_centers.size() == 1 )
              kept.push_back( means[c] );
          if ( kept.empty() )
          {
            // Every cluster undersized: fall back to the global mean rather
            // than a zero vector accumulated from no points.
            std::vector<double> global( d, 0.0 );
            for ( size_t i = 0; i < n; ++i )
              for ( size_t f = 0; f < d; ++f )
                global[f] += x[i * d + f];
            for ( size_t f = 0; f < d; ++f )
              global[f] /= static_cast<double>( n );
            kept.push_back( std::move( global ) );
          }
          if ( kept.size() != m_centers.size() )
          {
            m_centers = std::move( kept );
            continue; // re-assign orphaned points before split/merge
          }

          // Split: max per-dimension sigma above theta_S with the canonical
          // minimum cluster guard |S_k| >= 2(N_min + 1), capped at
          // kMaxClusters total.
          bool didSplit = false;
          for ( size_t c = 0; c < m_centers.size() && !didSplit; ++c )
          {
            if ( m_centers.size() >= kMaxClusters )
              break;
            if ( counts[c] < static_cast<size_t>( 2 * ( minSize + 1 ) ) )
              continue;
            std::vector<double> sigma( d, 0.0 );
            for ( size_t i = 0; i < n; ++i )
            {
              if ( assignment[i] != c )
                continue;
              for ( size_t f = 0; f < d; ++f )
              {
                const double diff = x[i * d + f] - means[c][f];
                sigma[f] += diff * diff;
              }
            }
            size_t worstF = 0;
            double worstSigma = 0.0;
            for ( size_t f = 0; f < d; ++f )
            {
              sigma[f] = std::sqrt( sigma[f] / static_cast<double>( counts[c] ) );
              if ( sigma[f] > worstSigma )
              {
                worstSigma = sigma[f];
                worstF = f;
              }
            }
            if ( worstSigma > m_p.isodataMaxStdDev )
            {
              std::vector<double> left = means[c];
              std::vector<double> right = means[c];
              left[worstF] -= 0.5 * worstSigma;
              right[worstF] += 0.5 * worstSigma;
              m_centers[c] = std::move( left );
              m_centers.push_back( std::move( right ) );
              didSplit = true;
            }
          }
          if ( didSplit )
            continue;

          // Merge the closest pair when it falls below theta_C
          // (size-weighted centroid average).
          double bestDist = std::numeric_limits<double>::infinity();
          size_t bestA = 0, bestB = 0;
          for ( size_t a = 0; a < m_centers.size(); ++a )
            for ( size_t b = a + 1; b < m_centers.size(); ++b )
            {
              double dist = 0.0;
              for ( size_t f = 0; f < d; ++f )
              {
                const double diff = m_centers[a][f] - m_centers[b][f];
                dist += diff * diff;
              }
              if ( dist < bestDist )
              {
                bestDist = dist;
                bestA = a;
                bestB = b;
              }
            }
          if ( std::sqrt( bestDist ) < m_p.isodataMinClusterDist && m_centers.size() > 1 )
          {
            const double na = static_cast<double>( counts[bestA] );
            const double nb = static_cast<double>( counts[bestB] );
            std::vector<double> merged( d, 0.0 );
            for ( size_t f = 0; f < d; ++f )
              merged[f] = ( na * m_centers[bestA][f] + nb * m_centers[bestB][f] ) / ( na + nb + kTiny );
            m_centers[bestA] = std::move( merged );
            m_centers.erase( m_centers.begin() + static_cast<std::ptrdiff_t>( bestB ) );
            continue;
          }

          // Convergence: centers stable after a re-assignment pass.
          const auto reassigned = assignPoints( x, n, d, m_centers );
          std::vector<std::vector<double>> refined( m_centers.size(), std::vector<double>( d, 0.0 ) );
          std::vector<size_t> refinedCounts( m_centers.size(), 0 );
          for ( size_t i = 0; i < n; ++i )
          {
            const size_t c = reassigned[i];
            ++refinedCounts[c];
            for ( size_t f = 0; f < d; ++f )
              refined[c][f] += x[i * d + f];
          }
          double maxShift = 0.0;
          for ( size_t c = 0; c < m_centers.size(); ++c )
          {
            if ( refinedCounts[c] == 0 )
              continue;
            double shift = 0.0;
            for ( size_t f = 0; f < d; ++f )
            {
              refined[c][f] /= static_cast<double>( refinedCounts[c] );
              const double diff = refined[c][f] - m_centers[c][f];
              shift += diff * diff;
            }
            maxShift = std::max( maxShift, std::sqrt( shift ) );
          }
          for ( size_t c = 0; c < m_centers.size(); ++c )
            if ( refinedCounts[c] > 0 )
              m_centers[c] = std::move( refined[c] );
          if ( maxShift < m_p.convergenceEpsilon )
            break;
        }
        m_trained = true;
      }

    private:
      ClassifierHyperparameters m_p;
  };

  // ------------------------------------------------------------------
  // Random forest: CART with gini impurity, bootstrap aggregation and a
  // sqrt(d) feature subspace per split.
  // ------------------------------------------------------------------
  class RandomForestModel final : public IClassifierModel
  {
    public:
      explicit RandomForestModel( const ClassifierHyperparameters &p )
          : m_p( p )
      {
      }

      bool isTrained() const override { return m_trained; }

      void fit( std::span<const float> x, std::span<const int> labels, size_t n, size_t d ) override
      {
        m_trained = false;
        if ( !validMatrix( x, n, d ) || !validLabels( labels, n ) )
          return;
        m_classes = distinctAscending( labels, n );
        m_dim = d;
        if ( m_classes.empty() )
          return;

        Lcg rng( m_p.randomSeed );
        const int numTrees = std::max( 1, m_p.rfNumTrees );
        m_trees.resize( static_cast<size_t>( numTrees ) );
        std::vector<size_t> bootstrap( n );
        for ( int t = 0; t < numTrees; ++t )
        {
          for ( size_t i = 0; i < n; ++i )
            bootstrap[i] = rng.nextIndex( n );
          Tree tree;
          tree.emplace_back();
          buildNode( tree, 0, x, labels, bootstrap, 0, rng );
          m_trees[static_cast<size_t>( t )] = std::move( tree );
        }
        m_trained = true;
      }

      int predictOne( std::span<const float> sample ) const override
      {
        const std::vector<float> probs = predictProbabilities( sample );
        if ( probs.empty() )
          return -1;
        return m_classes[static_cast<size_t>( std::max_element( probs.begin(), probs.end() ) - probs.begin() )];
      }

      void predictBatch( std::span<const float> in, std::span<int> out, size_t n, size_t d ) const override
      {
        if ( out.size() < n || d != m_dim )
          return;
        for ( size_t i = 0; i < n; ++i )
          out[i] = predictOne( in.subspan( i * d, d ) );
      }

      std::vector<float> predictProbabilities( std::span<const float> sample ) const override
      {
        if ( !m_trained || sample.size() != m_dim )
          return {};
        std::vector<double> sum( m_classes.size(), 0.0 );
        for ( const Tree &tree : m_trees )
        {
          int node = 0;
          while ( tree[static_cast<size_t>( node )].feature >= 0 )
          {
            const Node &n = tree[static_cast<size_t>( node )];
            node = sample[static_cast<size_t>( n.feature )] <= n.threshold ? n.left : n.right;
          }
          const std::vector<double> &hist = tree[static_cast<size_t>( node )].hist;
          const double total = std::accumulate( hist.begin(), hist.end(), 0.0 );
          if ( total <= 0.0 )
            continue;
          for ( size_t c = 0; c < hist.size(); ++c )
            sum[c] += hist[c] / total;
        }
        std::vector<float> probs( sum.size(), 0.0f );
        for ( size_t c = 0; c < sum.size(); ++c )
          probs[c] = static_cast<float>( sum[c] / static_cast<double>( m_trees.size() ) );
        return probs;
      }

    private:
      struct Node
      {
        int feature = -1;
        float threshold = 0.0f;
        int left = -1;
        int right = -1;
        std::vector<double> hist;
      };
      using Tree = std::vector<Node>;

      static double gini( const std::vector<double> &counts, double total )
      {
        if ( total <= 0.0 )
          return 0.0;
        double sum = 0.0;
        for ( const double c : counts )
          sum += ( c / total ) * ( c / total );
        return 1.0 - sum;
      }

      void buildNode( Tree &tree, int nodeId, std::span<const float> x, std::span<const int> labels,
                      const std::vector<size_t> &idx, int depth, Lcg &rng )
      {
        if ( idx.empty() )
          return;
        std::vector<double> hist( m_classes.size(), 0.0 );
        for ( const size_t i : idx )
          hist[static_cast<size_t>( std::lower_bound( m_classes.begin(), m_classes.end(), labels[i] )
                                    - m_classes.begin() )] += 1.0;
        tree[static_cast<size_t>( nodeId )].hist = hist;

        const bool pure = std::any_of( hist.begin(), hist.end(),
                                       [&]( double c )
                                       { return c >= static_cast<double>( idx.size() ) - kTiny; } );
        if ( pure || depth >= std::max( 1, m_p.rfMaxDepth )
             || static_cast<int>( idx.size() ) < std::max( 2, m_p.rfMinSamplesSplit ) )
          return;

        const size_t d = m_dim;
        const int mtry = std::max( 1, static_cast<int>( std::lround( std::sqrt( static_cast<double>( d ) ) ) ) );
        std::vector<size_t> featurePool( d );
        std::iota( featurePool.begin(), featurePool.end(), size_t { 0 } );
        for ( int f = 0; f < mtry && f < static_cast<int>( featurePool.size() ); ++f )
        {
          const size_t swapWith = static_cast<size_t>( f ) + rng.nextIndex( featurePool.size() - static_cast<size_t>( f ) );
          std::swap( featurePool[static_cast<size_t>( f )], featurePool[swapWith] );
        }

        const double parentImpurity = gini( hist, static_cast<double>( idx.size() ) );
        double bestGain = 0.0;
        int bestFeature = -1;
        float bestThreshold = 0.0f;

        std::vector<std::pair<float, int>> ordered;
        std::vector<double> leftCounts( m_classes.size(), 0.0 );
        for ( int f = 0; f < mtry && f < static_cast<int>( featurePool.size() ); ++f )
        {
          const size_t feature = featurePool[static_cast<size_t>( f )];
          ordered.clear();
          ordered.reserve( idx.size() );
          for ( const size_t i : idx )
            ordered.emplace_back( x[i * d + feature], labels[i] );
          std::sort( ordered.begin(), ordered.end(),
                     []( const auto &a, const auto &b ) { return a.first < b.first; } );

          std::fill( leftCounts.begin(), leftCounts.end(), 0.0 );
          size_t leftCount = 0;
          for ( size_t s = 0; s + 1 < ordered.size(); ++s )
          {
            leftCounts[static_cast<size_t>( std::lower_bound( m_classes.begin(), m_classes.end(),
                                                              ordered[s].second )
                                            - m_classes.begin() )] += 1.0;
            ++leftCount;
            if ( ordered[s].first == ordered[s + 1].first )
              continue; // split points only between distinct values
            const double rightCount = static_cast<double>( idx.size() - leftCount );
            std::vector<double> rightCounts( m_classes.size(), 0.0 );
            for ( size_t c = 0; c < m_classes.size(); ++c )
              rightCounts[c] = hist[c] - leftCounts[c];
            const double gain = parentImpurity
                                - ( static_cast<double>( leftCount ) / idx.size() )
                                    * gini( leftCounts, static_cast<double>( leftCount ) )
                                - ( rightCount / idx.size() ) * gini( rightCounts, rightCount );
            if ( gain > bestGain + kTiny )
            {
              bestGain = gain;
              bestFeature = static_cast<int>( feature );
              bestThreshold = 0.5f * ( ordered[s].first + ordered[s + 1].first );
            }
          }
        }

        if ( bestFeature < 0 )
          return;

        std::vector<size_t> leftIdx, rightIdx;
        leftIdx.reserve( idx.size() );
        for ( const size_t i : idx )
          ( x[i * d + static_cast<size_t>( bestFeature )] <= bestThreshold ? leftIdx : rightIdx ).push_back( i );
        if ( leftIdx.empty() || rightIdx.empty() )
          return;

        tree[static_cast<size_t>( nodeId )].feature = bestFeature;
        tree[static_cast<size_t>( nodeId )].threshold = bestThreshold;
        const int leftId = static_cast<int>( tree.size() );
        tree.emplace_back();
        const int rightId = static_cast<int>( tree.size() );
        tree.emplace_back();
        tree[static_cast<size_t>( nodeId )].left = leftId;
        tree[static_cast<size_t>( nodeId )].right = rightId;
        buildNode( tree, leftId, x, labels, leftIdx, depth + 1, rng );
        buildNode( tree, rightId, x, labels, rightIdx, depth + 1, rng );
      }

      ClassifierHyperparameters m_p;
      std::vector<int> m_classes;
      std::vector<Tree> m_trees;
      size_t m_dim = 0;
      bool m_trained = false;
  };

  // ------------------------------------------------------------------
  // RBF-kernel C-SVM via simplified SMO, one-vs-one multiclass voting.
  // ------------------------------------------------------------------
  class SvmModel final : public IClassifierModel
  {
    public:
      explicit SvmModel( const ClassifierHyperparameters &p )
          : m_p( p )
      {
      }

      bool isTrained() const override { return m_trained; }

      void fit( std::span<const float> x, std::span<const int> labels, size_t n, size_t d ) override
      {
        m_trained = false;
        if ( !validMatrix( x, n, d ) || !validLabels( labels, n ) )
          return;
        m_classes = distinctAscending( labels, n );
        m_dim = d;
        m_x.assign( x.begin(), x.begin() + static_cast<std::ptrdiff_t>( n * d ) );
        m_labels.assign( labels.begin(), labels.begin() + static_cast<std::ptrdiff_t>( n ) );
        m_machines.clear();
        if ( m_classes.size() < 2 )
        {
          m_trained = true; // degenerate: always predicts the single class
          return;
        }

        for ( size_t a = 0; a + 1 < m_classes.size(); ++a )
          for ( size_t b = a + 1; b < m_classes.size(); ++b )
            trainMachine( m_classes[a], m_classes[b] );
        m_trained = true;
      }

      int predictOne( std::span<const float> sample ) const override
      {
        if ( !m_trained || sample.size() != m_dim )
          return -1;
        if ( m_machines.empty() )
          return m_classes.empty() ? -1 : m_classes.front();
        std::vector<int> votes( m_classes.size(), 0 );
        for ( const Machine &machine : m_machines )
        {
          const double f = decisionValue( machine, sample );
          const int winnerClass = f >= 0.0 ? machine.classPositive : machine.classNegative;
          ++votes[static_cast<size_t>( std::lower_bound( m_classes.begin(), m_classes.end(), winnerClass )
                                       - m_classes.begin() )];
        }
        return m_classes[static_cast<size_t>( std::max_element( votes.begin(), votes.end() ) - votes.begin() )];
      }

      void predictBatch( std::span<const float> in, std::span<int> out, size_t n, size_t d ) const override
      {
        if ( out.size() < n || d != m_dim )
          return;
        for ( size_t i = 0; i < n; ++i )
          out[i] = predictOne( in.subspan( i * d, d ) );
      }

      std::vector<float> predictProbabilities( std::span<const float> sample ) const override
      {
        const int predicted = predictOne( sample );
        if ( predicted < 0 )
          return {};
        std::vector<float> probs( m_classes.size(), 0.0f );
        probs[static_cast<size_t>( std::lower_bound( m_classes.begin(), m_classes.end(), predicted )
                                   - m_classes.begin() )] = 1.0f;
        return probs;
      }

    private:
      struct Machine
      {
        std::vector<size_t> rows;      // indices into m_x
        std::vector<double> y;         // +1 / -1
        std::vector<double> gram;      // m x m row-major kernel matrix
        std::vector<double> alpha;
        double bias = 0.0;
        int classPositive = 0;
        int classNegative = 0;
      };

      double kernel( const float *a, const float *b ) const
      {
        double sum = 0.0;
        for ( size_t f = 0; f < m_dim; ++f )
        {
          const double diff = a[f] - b[f];
          sum += diff * diff;
        }
        return std::exp( -m_p.svmGamma * sum );
      }

      double decisionValue( const Machine &machine, std::span<const float> sample ) const
      {
        double f = machine.bias;
        for ( size_t i = 0; i < machine.rows.size(); ++i )
        {
          if ( machine.alpha[i] <= 0.0 )
            continue;
          const double k = kernel( m_x.data() + machine.rows[i] * m_dim, sample.data() );
          f += machine.alpha[i] * machine.y[i] * k;
        }
        return f;
      }

      void trainMachine( int classPositive, int classNegative )
      {
        Machine machine;
        machine.classPositive = classPositive;
        machine.classNegative = classNegative;
        for ( size_t i = 0; i < m_x.size() / m_dim; ++i )
        {
          const int label = labelAt( i );
          if ( label != classPositive && label != classNegative )
            continue;
          machine.rows.push_back( i );
          machine.y.push_back( label == classPositive ? 1.0 : -1.0 );
        }
        const size_t m = machine.rows.size();
        machine.gram.assign( m * m, 0.0 );
        for ( size_t i = 0; i < m; ++i )
          for ( size_t j = i; j < m; ++j )
          {
            const double k = kernel( m_x.data() + machine.rows[i] * m_dim, m_x.data() + machine.rows[j] * m_dim );
            machine.gram[i * m + j] = k;
            machine.gram[j * m + i] = k;
          }

        const double c = m_p.svmC;
        const double tol = 1e-3;
        const double alphaEps = 1e-5;
        machine.alpha.assign( m, 0.0 );
        machine.bias = 0.0;
        std::vector<double> f( m, 0.0 ); // decision values f_i (E_i = f_i - y_i)
        std::vector<double> error( m );
        for ( size_t i = 0; i < m; ++i )
          error[i] = -machine.y[i];

        int passes = 0;
        const int maxPasses = 3;
        const int passCap = 400; // hard bound on SMO sweeps
        for ( int sweep = 0; sweep < passCap && passes < maxPasses; ++sweep )
        {
          int changed = 0;
          for ( size_t i = 0; i < m; ++i )
          {
            const double yi = machine.y[i];
            if ( !( ( yi * error[i] < -tol && machine.alpha[i] < c )
                    || ( yi * error[i] > tol && machine.alpha[i] > 0.0 ) ) )
              continue;

            // Second choice: maximum |Ei - Ej| (deterministic).
            size_t j = i < m - 1 ? i + 1 : 0;
            double bestDiff = -1.0;
            for ( size_t cand = 0; cand < m; ++cand )
            {
              if ( cand == i )
                continue;
              const double diff = std::abs( error[i] - error[cand] );
              if ( diff > bestDiff )
              {
                bestDiff = diff;
                j = cand;
              }
            }

            const double yj = machine.y[j];
            const double ai = machine.alpha[i];
            const double aj = machine.alpha[j];
            double l = 0.0, h = 0.0;
            if ( yi != yj )
            {
              l = std::max( 0.0, aj - ai );
              h = std::min( c, c + aj - ai );
            }
            else
            {
              l = std::max( 0.0, ai + aj - c );
              h = std::min( c, ai + aj );
            }
            if ( l >= h )
              continue;
            const double eta = 2.0 * machine.gram[i * m + j] - machine.gram[i * m + i] - machine.gram[j * m + j];
            if ( eta >= -kTiny )
              continue;
            double ajNew = aj - yj * ( error[i] - error[j] ) / eta;
            ajNew = std::clamp( ajNew, l, h );
            if ( std::abs( ajNew - aj ) < alphaEps )
              continue;
            const double aiNew = ai + yi * yj * ( aj - ajNew );

            const double bOld = machine.bias;
            const double b1 = machine.bias - error[i] - yi * ( aiNew - ai ) * machine.gram[i * m + i]
                              - yj * ( ajNew - aj ) * machine.gram[i * m + j];
            const double b2 = machine.bias - error[j] - yi * ( aiNew - ai ) * machine.gram[i * m + j]
                              - yj * ( ajNew - aj ) * machine.gram[j * m + j];
            machine.bias = ( aiNew > kTiny && aiNew < c ) ? b1
                           : ( ajNew > kTiny && ajNew < c )              ? b2
                                                                          : 0.5 * ( b1 + b2 );

            for ( size_t k = 0; k < m; ++k )
            {
              f[k] += yi * ( aiNew - ai ) * machine.gram[i * m + k]
                      + yj * ( ajNew - aj ) * machine.gram[j * m + k]
                      + ( machine.bias - bOld );
              error[k] = f[k] - machine.y[k];
            }
            machine.alpha[i] = aiNew;
            machine.alpha[j] = ajNew;
            ++changed;
          }
          passes = changed == 0 ? passes + 1 : 0;
        }
        m_machines.push_back( std::move( machine ) );
      }

      int labelAt( size_t row ) const
      {
        // m_labels kept during fit for machine assembly.
        return m_labels[row];
      }

      ClassifierHyperparameters m_p;
      std::vector<float> m_x;   // n x d row-major
      std::vector<int> m_labels;
      std::vector<int> m_classes;
      std::vector<Machine> m_machines;
      size_t m_dim = 0;
      bool m_trained = false;
  };

  // ------------------------------------------------------------------
  // NormalBayes: full-covariance Gaussian MAP with ridge regularisation.
  // ------------------------------------------------------------------
  class NormalBayesModel final : public IClassifierModel
  {
    public:
      explicit NormalBayesModel( const ClassifierHyperparameters &p )
          : m_p( p )
      {
      }

      bool isTrained() const override { return m_trained; }

      void fit( std::span<const float> x, std::span<const int> labels, size_t n, size_t d ) override
      {
        m_trained = false;
        if ( !validMatrix( x, n, d ) || !validLabels( labels, n ) )
          return;
        m_classes = distinctAscending( labels, n );
        m_dim = d;
        const size_t k = m_classes.size();
        if ( k == 0 )
          return;
        m_gaussians.assign( k, Gaussian( d ) );

        std::vector<size_t> counts( k, 0 );
        std::vector<std::vector<double>> means( k, std::vector<double>( d, 0.0 ) );
        for ( size_t i = 0; i < n; ++i )
        {
          const size_t c = classSlot( labels[i] );
          ++counts[c];
          for ( size_t f = 0; f < d; ++f )
            means[c][f] += x[i * d + f];
        }
        for ( size_t c = 0; c < k; ++c )
          if ( counts[c] > 0 )
            for ( size_t f = 0; f < d; ++f )
              means[c][f] /= static_cast<double>( counts[c] );

        for ( size_t c = 0; c < k; ++c )
        {
          Gaussian &g = m_gaussians[c];
          g.mean = means[c];
          if ( counts[c] == 0 )
            continue;
          std::vector<double> cov( static_cast<size_t>( d ) * d, 0.0 );
          for ( size_t i = 0; i < n; ++i )
          {
            if ( classSlot( labels[i] ) != c )
              continue;
            std::vector<double> diff( d );
            for ( size_t f = 0; f < d; ++f )
              diff[f] = x[i * d + f] - means[c][f];
            for ( size_t a = 0; a < d; ++a )
              for ( size_t b = a; b < d; ++b )
              {
                cov[a * d + b] += diff[a] * diff[b];
                cov[b * d + a] = cov[a * d + b];
              }
          }
          const double denom = counts[c] > 1 ? static_cast<double>( counts[c] - 1 ) : 1.0;
          for ( double &v : cov )
            v /= denom;

          // Ridge: scale with the average variance, then escape singularities.
          double trace = 0.0;
          for ( size_t f = 0; f < d; ++f )
            trace += cov[f * d + f];
          double ridge = std::max( 1e-6 * trace / d, 1e-9 );
          bool inverted = false;
          for ( int attempt = 0; attempt < 6 && !inverted; ++attempt )
          {
            std::vector<double> regularized( cov );
            for ( size_t f = 0; f < d; ++f )
              regularized[f * d + f] += ridge;
            inverted = choleskyInvert( regularized, d, g.covInverse, g.logDeterminant );
            if ( !inverted )
              ridge *= 10.0;
          }
          if ( !inverted )
          {
            // Diagonal fallback: variances only.
            g.covInverse.assign( static_cast<size_t>( d ) * d, 0.0 );
            g.logDeterminant = 0.0;
            for ( size_t f = 0; f < d; ++f )
            {
              const double var = std::max( cov[f * d + f] + ridge, 1e-12 );
              g.covInverse[f * d + f] = 1.0 / var;
              g.logDeterminant += std::log( var );
            }
          }
          g.logPrior = std::log( std::max<double>( static_cast<double>( counts[c] ) / n, 1e-300 ) );
        }
        m_trained = true;
      }

      int predictOne( std::span<const float> sample ) const override
      {
        const std::vector<float> probs = predictProbabilities( sample );
        if ( probs.empty() )
          return -1;
        return m_classes[static_cast<size_t>( std::max_element( probs.begin(), probs.end() ) - probs.begin() )];
      }

      void predictBatch( std::span<const float> in, std::span<int> out, size_t n, size_t d ) const override
      {
        if ( out.size() < n || d != m_dim )
          return;
        for ( size_t i = 0; i < n; ++i )
          out[i] = predictOne( in.subspan( i * d, d ) );
      }

      std::vector<float> predictProbabilities( std::span<const float> sample ) const override
      {
        if ( !m_trained || sample.size() != m_dim || m_classes.empty() )
          return {};
        std::vector<double> g( m_classes.size(), 0.0 );
        double best = -std::numeric_limits<double>::infinity();
        for ( size_t c = 0; c < m_classes.size(); ++c )
        {
          const Gaussian &gaussian = m_gaussians[c];
          std::vector<double> diff( m_dim, 0.0 );
          for ( size_t f = 0; f < m_dim; ++f )
            diff[f] = sample[f] - gaussian.mean[f];
          double quad = 0.0;
          for ( size_t a = 0; a < m_dim; ++a )
          {
            double rowDot = 0.0;
            for ( size_t b = 0; b < m_dim; ++b )
              rowDot += gaussian.covInverse[a * m_dim + b] * diff[b];
            quad += diff[a] * rowDot;
          }
          g[c] = -0.5 * gaussian.logDeterminant - 0.5 * quad + gaussian.logPrior;
          best = std::max( best, g[c] );
        }
        double sum = 0.0;
        std::vector<float> probs( m_classes.size(), 0.0f );
        for ( size_t c = 0; c < m_classes.size(); ++c )
        {
          probs[c] = static_cast<float>( std::exp( g[c] - best ) );
          sum += probs[c];
        }
        if ( sum > 0.0 )
          for ( float &p : probs )
            p /= sum;
        return probs;
      }

    private:
      struct Gaussian
      {
        explicit Gaussian( size_t d )
            : mean( d, 0.0 )
            , covInverse( static_cast<size_t>( d ) * d, 0.0 )
        {
        }
        std::vector<double> mean;
        std::vector<double> covInverse;
        double logDeterminant = 0.0;
        double logPrior = 0.0;
      };

      size_t classSlot( int label ) const
      {
        return static_cast<size_t>( std::lower_bound( m_classes.begin(), m_classes.end(), label )
                                    - m_classes.begin() );
      }

      // Symmetric positive-definite Cholesky solve: cov = L L^T; produces
      // the explicit inverse and log|cov|.  Returns false when not SPD.
      static bool choleskyInvert( std::vector<double> cov, size_t d,
                                  std::vector<double> &inverse, double &logDeterminant )
      {
        std::vector<double> l( static_cast<size_t>( d ) * d, 0.0 );
        for ( size_t i = 0; i < d; ++i )
        {
          for ( size_t j = 0; j <= i; ++j )
          {
            double sum = cov[i * d + j];
            for ( size_t k = 0; k < j; ++k )
              sum -= l[i * d + k] * l[j * d + k];
            if ( i == j )
            {
              if ( sum <= 1e-300 )
                return false;
              l[i * d + j] = std::sqrt( sum );
            }
            else
            {
              l[i * d + j] = sum / l[j * d + j];
            }
          }
        }

        // Forward/back substitution against the identity, column by column.
        inverse.assign( static_cast<size_t>( d ) * d, 0.0 );
        std::vector<double> col( d, 0.0 ), tmp( d, 0.0 );
        for ( size_t c = 0; c < d; ++c )
        {
          std::fill( col.begin(), col.end(), 0.0 );
          col[c] = 1.0;
          for ( size_t i = 0; i < d; ++i )
          {
            double sum = col[i];
            for ( size_t k = 0; k < i; ++k )
              sum -= l[i * d + k] * tmp[k];
            tmp[i] = sum / l[i * d + i];
          }
          // L^T X = Y needs back-substitution: x_i depends on x_k for
          // k > i, so the row loop must run in DESCENDING order (ascending
          // reads not-yet-computed entries and silently returns L^-1).
          for ( size_t ii = d; ii-- > 0; )
          {
            const size_t i = ii;
            double sum = tmp[i];
            for ( size_t k = i + 1; k < d; ++k )
              sum -= l[k * d + i] * inverse[k * d + c];
            inverse[i * d + c] = sum / l[i * d + i];
          }
        }

        logDeterminant = 0.0;
        for ( size_t i = 0; i < d; ++i )
          logDeterminant += 2.0 * std::log( l[i * d + i] );
        return true;
      }

      ClassifierHyperparameters m_p;
      std::vector<int> m_classes;
      std::vector<Gaussian> m_gaussians;
      size_t m_dim = 0;
      bool m_trained = false;
  };

} // namespace

std::unique_ptr<IClassifierModel> ClassifierEngine::create( ClassifierAlgorithm algo,
                                                            const ClassifierHyperparameters &params )
{
  switch ( algo )
  {
    case ClassifierAlgorithm::RandomForest:
      return std::make_unique<RandomForestModel>( params );
    case ClassifierAlgorithm::SupportVectorMachine:
      return std::make_unique<SvmModel>( params );
    case ClassifierAlgorithm::KMeans:
      return std::make_unique<KMeansModel>( params );
    case ClassifierAlgorithm::Isodata:
      return std::make_unique<IsodataModel>( params );
    case ClassifierAlgorithm::NormalBayes:
      return std::make_unique<NormalBayesModel>( params );
  }
  return nullptr;
}

} // namespace rs::processing
