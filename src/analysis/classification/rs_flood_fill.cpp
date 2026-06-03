// Phase 10A Task 10.7 — RsFloodFill implementation.
#include "rs_flood_fill.h"

#include <cmath>
#include <queue>
#include <utility>
#include <vector>

QSet<quint64> RsFloodFill::run( const cv::Mat &img, int sr, int sc, double tol )
{
  QSet<quint64> out;
  if ( img.empty() )
    return out;
  const int H = img.rows;
  const int W = img.cols;
  const int B = img.channels();
  if ( sr < 0 || sr >= H || sc < 0 || sc >= W )
    return out;
  if ( img.depth() != CV_32F )
    return out;

  std::vector<float> seed( B );
  {
    auto rowPtr = reinterpret_cast<const float *>( img.ptr( sr ) );
    for ( int b = 0; b < B; ++b )
      seed[b] = rowPtr[sc * B + b];
  }

  auto idx = [W]( int r, int c ) {
    return quint64( r ) * quint64( W ) + quint64( c );
  };

  out.insert( idx( sr, sc ) );
  std::queue<std::pair<int, int>> q;
  q.emplace( sr, sc );

  static constexpr int dr[4] = { -1, 1, 0, 0 };
  static constexpr int dc[4] = { 0, 0, -1, 1 };

  while ( !q.empty() )
  {
    auto [r, c] = q.front();
    q.pop();
    for ( int d = 0; d < 4; ++d )
    {
      const int nr = r + dr[d];
      const int nc = c + dc[d];
      if ( nr < 0 || nr >= H || nc < 0 || nc >= W )
        continue;
      const quint64 i = idx( nr, nc );
      if ( out.contains( i ) )
        continue;
      auto rowPtr = reinterpret_cast<const float *>( img.ptr( nr ) );
      double dist2 = 0.0;
      for ( int b = 0; b < B; ++b )
      {
        const double diff = double( rowPtr[nc * B + b] ) - double( seed[b] );
        dist2 += diff * diff;
      }
      if ( std::sqrt( dist2 ) < tol )
      {
        out.insert( i );
        q.emplace( nr, nc );
      }
    }
  }
  return out;
}
