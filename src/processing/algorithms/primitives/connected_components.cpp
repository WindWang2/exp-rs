// primitives/connected_components.cpp — see connected_components.h.

#include "connected_components.h"

#include <algorithm>
#include <numeric>

namespace sicnu::rs::primitives
{
namespace
{

constexpr uint8_t kForeground = 1;

class UnionFind
{
  public:
    explicit UnionFind( size_t initialSize ) : m_parent( initialSize )
    {
      std::iota( m_parent.begin(), m_parent.end(), 0 );
    }

    size_t size() const { return m_parent.size(); }

    int32_t find( int32_t x )
    {
      while ( m_parent[static_cast<size_t>( x )] != x )
      {
        m_parent[static_cast<size_t>( x )] = m_parent[static_cast<size_t>( m_parent[static_cast<size_t>( x )] )]; // path halving
        x = m_parent[static_cast<size_t>( x )];
      }
      return x;
    }

    void unite( int32_t a, int32_t b )
    {
      a = find( a );
      b = find( b );
      if ( a != b )
      {
        // Smaller root wins so the final representative is the component's
        // first provisional label — the raster-order determinism contract.
        if ( a < b )
          m_parent[static_cast<size_t>( b )] = a;
        else
          m_parent[static_cast<size_t>( a )] = b;
      }
    }

  private:
    std::vector<int32_t> m_parent;
};

} // namespace

Labeling labelComponents( const uint8_t *mask, int width, int height, Connectivity conn )
{
  Labeling out;
  if ( !mask || width <= 0 || height <= 0 )
    return out;

  const size_t n = static_cast<size_t>( width ) * height;
  out.labels.assign( n, 0 );

  // Pass 1: provisional labels + union with already-labeled neighbours.
  // Upper bound: one provisional label per foreground cell — sized once, so
  // unions never invalidate state.
  UnionFind uf( n + 2 );
  std::vector<int32_t> provisional( n, 0 );
  int32_t nextLabel = 0;

  const bool eight = ( conn == Connectivity::Eight );
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const size_t i = static_cast<size_t>( y ) * width + x;
      if ( mask[i] != kForeground )
        continue;

      int32_t label = 0;
      // Already-scanned neighbours: west, north, (north-west, north-east).
      if ( x > 0 && mask[i - 1] == kForeground )
        label = provisional[i - 1];
      if ( y > 0 )
      {
        const size_t north = i - static_cast<size_t>( width );
        if ( mask[north] == kForeground )
        {
          if ( label == 0 )
            label = provisional[north];
          else
            uf.unite( label, provisional[north] );
        }
        if ( eight && x > 0 && mask[north - 1] == kForeground )
        {
          if ( label == 0 )
            label = provisional[north - 1];
          else
            uf.unite( label, provisional[north - 1] );
        }
        if ( eight && x + 1 < width && mask[north + 1] == kForeground )
        {
          if ( label == 0 )
            label = provisional[north + 1];
          else
            uf.unite( label, provisional[north + 1] );
        }
      }

      if ( label == 0 )
        label = ++nextLabel;
      provisional[i] = label;
    }
  }

  if ( nextLabel == 0 )
    return out;

  // Pass 2: compact provisional labels through the union-find roots into
  // 1..componentCount in raster order of first occurrence.
  std::vector<int32_t> rootToLabel( static_cast<size_t>( nextLabel ) + 1, 0 );
  int32_t componentCount = 0;
  for ( size_t i = 0; i < n; ++i )
  {
    const int32_t p = provisional[i];
    if ( p == 0 )
      continue;
    const int32_t root = uf.find( p );
    if ( rootToLabel[static_cast<size_t>( root )] == 0 )
      rootToLabel[static_cast<size_t>( root )] = ++componentCount;
    out.labels[i] = rootToLabel[static_cast<size_t>( root )];
  }
  out.componentCount = componentCount;
  return out;
}

std::vector<int32_t> componentAreas( const Labeling &labeling )
{
  if ( labeling.componentCount <= 0 )
    return {};
  std::vector<int32_t> areas( static_cast<size_t>( labeling.componentCount ), 0 );
  for ( const int32_t label : labeling.labels )
  {
    if ( label > 0 )
      ++areas[static_cast<size_t>( label - 1 )];
  }
  return areas;
}

size_t removeSmallObjects( uint8_t *mask, int width, int height, int64_t minAreaPixels,
                           Connectivity conn )
{
  if ( !mask || width <= 0 || height <= 0 || minAreaPixels <= 0 )
    return 0;

  const Labeling labeling = labelComponents( mask, width, height, conn );
  if ( labeling.componentCount == 0 )
    return 0;

  const std::vector<int32_t> areas = componentAreas( labeling );
  size_t removed = 0;
  const size_t n = labeling.labels.size();
  for ( size_t i = 0; i < n; ++i )
  {
    const int32_t label = labeling.labels[i];
    if ( label == 0 )
      continue;
    if ( static_cast<int64_t>( areas[static_cast<size_t>( label - 1 )] ) < minAreaPixels )
    {
      if ( mask[i] == 1 )
      {
        mask[i] = 0;
        ++removed;
      }
    }
  }
  return removed;
}

} // namespace sicnu::rs::primitives
