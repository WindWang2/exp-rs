// rs_hungarian_assignment.cpp — Phase 10A.1.1.
//
// Classic Munkres O(n^3) implementation on a square cost matrix using
// dual potentials u[i]/v[j] and augmenting paths. 1-based internal
// indexing converted at the boundary for clarity vs. the textbook
// presentation. Rectangular inputs are padded to square with a large
// finite pad cost, then assignments outside the original column range
// are reported as -1.

#include "rs_hungarian_assignment.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
  QVector<int> solveImplFromSquare( const cv::Mat &square )
  {
    const int n = square.rows;
    if ( n == 0 || square.cols != n )
      return {};

    cv::Mat tmp;
    square.convertTo( tmp, CV_64F );
    std::vector<std::vector<double>> in( n, std::vector<double>( n ) );
    for ( int i = 0; i < n; ++i )
      for ( int j = 0; j < n; ++j )
        in[i][j] = tmp.at<double>( i, j );

    // u[i], v[j] are dual potentials; p[j] is the row assigned to column j
    // (0 = unassigned). way[j] is part of the augmenting-path reconstruction.
    std::vector<double> u( n + 1, 0.0 ), v( n + 1, 0.0 );
    std::vector<int> p( n + 1, 0 ), way( n + 1, 0 );

    for ( int i = 1; i <= n; ++i )
    {
      p[0] = i;
      int j0 = 0;
      std::vector<double> minv( n + 1, std::numeric_limits<double>::infinity() );
      std::vector<char> used( n + 1, false );
      do
      {
        used[j0] = true;
        const int i0 = p[j0];
        double delta = std::numeric_limits<double>::infinity();
        int j1 = 0;
        for ( int j = 1; j <= n; ++j )
        {
          if ( used[j] )
            continue;
          const double cur = in[i0 - 1][j - 1] - u[i0] - v[j];
          if ( cur < minv[j] )
          {
            minv[j] = cur;
            way[j] = j0;
          }
          if ( minv[j] < delta )
          {
            delta = minv[j];
            j1 = j;
          }
        }
        for ( int j = 0; j <= n; ++j )
        {
          if ( used[j] )
          {
            u[p[j]] += delta;
            v[j] -= delta;
          }
          else
          {
            minv[j] -= delta;
          }
        }
        j0 = j1;
      }
      while ( p[j0] != 0 );

      // Trace augmenting path back to row i.
      do
      {
        const int j1 = way[j0];
        p[j0] = p[j1];
        j0 = j1;
      }
      while ( j0 != 0 );
    }

    QVector<int> result( n, -1 );
    for ( int j = 1; j <= n; ++j )
    {
      if ( p[j] != 0 )
        result[p[j] - 1] = j - 1;
    }
    return result;
  }
} // namespace

QVector<int> RsHungarianAssignment::solve( const cv::Mat &cost )
{
  if ( cost.empty() )
    return {};
  const int n = cost.rows;
  const int m = cost.cols;
  const int sz = std::max( n, m );
  constexpr double kPad = 1e9;
  cv::Mat square( sz, sz, CV_64F, cv::Scalar( kPad ) );
  cv::Mat tmp;
  cost.convertTo( tmp, CV_64F );
  tmp.copyTo( square( cv::Rect( 0, 0, m, n ) ) );
  QVector<int> full = solveImplFromSquare( square );
  QVector<int> out( n, -1 );
  for ( int i = 0; i < n; ++i )
  {
    const int col = full[i];
    if ( col >= 0 && col < m )
      out[i] = col;
  }
  return out;
}
