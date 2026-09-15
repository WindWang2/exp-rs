// src/processing/algorithms/classification_postprocess.cpp — D15 Package D.
#include "classification_postprocess.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <utility>
#include <vector>

namespace rs::processing
{
namespace
{
  int windowHalfSize( int windowSize )
  {
    int half = std::max( 0, windowSize / 2 );
    if ( windowSize > 0 && windowSize % 2 == 0 )
      half = std::max( 0, ( windowSize - 1 ) / 2 ); // even snaps down to odd
    return half;
  }

  bool validRaster( int64_t size, int width, int height )
  {
    return width > 0 && height > 0 && size == static_cast<int64_t>( width ) * height;
  }

  // Component labelling over class-equal connectivity; every pixel class
  // participates (NoData is just another class here).
  struct ComponentMap
  {
    std::vector<int> labels;                    // per pixel, -1 = none
    std::vector<int> classOf;                   // per component
    std::vector<int64_t> area;
    std::map<std::pair<int, int>, int64_t> borders; // ordered comp pair -> shared edges
  };

  ComponentMap labelComponents( std::span<const int> raster, int width, int height, int connectivity )
  {
    ComponentMap map;
    map.labels.assign( raster.size(), -1 );
    const bool use8 = connectivity != 4;
    static constexpr int kDx8[] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    static constexpr int kDy8[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    const int neighbors = use8 ? 8 : 4;

    for ( int y0 = 0; y0 < height; ++y0 )
    {
      for ( int x0 = 0; x0 < width; ++x0 )
      {
        const size_t seed = static_cast<size_t>( y0 ) * width + x0;
        if ( map.labels[seed] >= 0 )
          continue;
        const int cls = raster[seed];
        const int id = static_cast<int>( map.classOf.size() );
        map.classOf.push_back( cls );
        map.area.push_back( 0 );
        std::queue<std::pair<int, int>> queue;
        queue.push( { x0, y0 } );
        map.labels[seed] = id;
        while ( !queue.empty() )
        {
          const auto [cx, cy] = queue.front();
          queue.pop();
          ++map.area[id];
          for ( int n = 0; n < neighbors; ++n )
          {
            const int nx = cx + kDx8[n];
            const int ny = cy + kDy8[n];
            if ( nx < 0 || nx >= width || ny < 0 || ny >= height )
              continue;
            const size_t nIdx = static_cast<size_t>( ny ) * width + nx;
            if ( map.labels[nIdx] >= 0 || raster[nIdx] != cls )
              continue;
            map.labels[nIdx] = id;
            queue.push( { nx, ny } );
          }
        }
      }
    }

    // Shared orthogonal edges between different-class components.
    for ( int y = 0; y < height; ++y )
    {
      for ( int x = 0; x < width; ++x )
      {
        const size_t idx = static_cast<size_t>( y ) * width + x;
        if ( x + 1 < width )
        {
          const size_t right = idx + 1;
          if ( raster[right] != raster[idx] )
            map.borders[std::minmax( map.labels[idx], map.labels[right] )] += 1;
        }
        if ( y + 1 < height )
        {
          const size_t below = idx + static_cast<size_t>( width );
          if ( raster[below] != raster[idx] )
            map.borders[std::minmax( map.labels[idx], map.labels[below] )] += 1;
        }
      }
    }
    return map;
  }
} // namespace

std::vector<int> ClassificationPostProcessor::applyMajorityFilter( std::span<const int> inClassification,
                                                                   int width, int height,
                                                                   const MorphologicalFilterConfig &config )
{
  if ( !validRaster( static_cast<int64_t>( inClassification.size() ), width, height ) )
    return {};
  const int half = windowHalfSize( config.windowSize );
  std::vector<int> out( inClassification.begin(), inClassification.end() );

  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const int center = inClassification[static_cast<size_t>( y ) * width + x];
      if ( center == config.noDataValue )
        continue; // NoData centre stays NoData
      std::map<int, int64_t> histogram;
      int64_t valid = 0;
      for ( int wy = y - half; wy <= y + half; ++wy )
      {
        const int cy = std::clamp( wy, 0, height - 1 );
        for ( int wx = x - half; wx <= x + half; ++wx )
        {
          const int cx = std::clamp( wx, 0, width - 1 );
          const int v = inClassification[static_cast<size_t>( cy ) * width + cx];
          if ( v == config.noDataValue )
            continue;
          ++histogram[v];
          ++valid;
        }
      }
      if ( valid == 0 )
        continue;
      int winner = center;
      int64_t winnerCount = 0;
      for ( const auto &[cls, count] : histogram )
      {
        if ( count > winnerCount )
        {
          winner = cls;
          winnerCount = count;
        }
      }
      if ( winnerCount > valid / 2 )
        out[static_cast<size_t>( y ) * width + x] = winner; // strict majority only
    }
  }
  return out;
}

std::vector<int> ClassificationPostProcessor::applySieveFilter( std::span<const int> inClassification,
                                                                int width, int height,
                                                                int minPixelSize, int connectivity )
{
  if ( !validRaster( static_cast<int64_t>( inClassification.size() ), width, height ) )
    return {};
  constexpr int kNoData = -1; // sieve-side sentinel: see clump contract note
  std::vector<int> out( inClassification.begin(), inClassification.end() );

  // Elimination passes: merging small components can create new small
  // components, so iterate until stable (each pass strictly reduces the
  // component count, which bounds the loop).
  for ( int pass = 0; pass < 1024; ++pass )
  {
    const ComponentMap map = labelComponents( out, width, height, connectivity );
    bool eliminatedAny = false;
    // Immediate best-neighbour target per small component, then chain/cycle
    // resolution: two adjacent small components must not swap classes (the
    // naive simultaneous rewrite oscillates until the pass cap).  A target
    // that is itself small is followed to its final large component or
    // NoData; a cycle of small components collapses to its lowest class id.
    std::map<int, int> targetComp; // small comp -> neighbour comp (-1 = NoData)
    for ( int comp = 0; comp < static_cast<int>( map.classOf.size() ); ++comp )
    {
      const int cls = map.classOf[comp];
      if ( cls == kNoData )
        continue; // NoData components are mask features, never eliminated
      if ( map.area[comp] >= minPixelSize )
        continue;
      int bestComp = -1;
      int64_t bestBorder = 0;
      for ( const auto &[pair, border] : map.borders )
      {
        if ( pair.first != comp && pair.second != comp )
          continue;
        const int other = pair.first == comp ? pair.second : pair.first;
        if ( map.classOf[other] == kNoData )
          continue;
        const bool better = border > bestBorder
                            || ( border == bestBorder && bestComp >= 0
                                 && ( map.classOf[other] < map.classOf[bestComp]
                                      || ( map.classOf[other] == map.classOf[bestComp] && other < bestComp ) ) );
        if ( better )
        {
          bestBorder = border;
          bestComp = other;
        }
      }
      targetComp[comp] = bestComp; // -1 = fully enclosed by NoData
      eliminatedAny = true;
    }
    if ( !eliminatedAny )
      break;
    std::map<int, int> rewriteTo; // small comp -> resolved class
    for ( const auto &[comp, target] : targetComp )
    {
      std::vector<int> chain;
      std::map<int, bool> onChain;
      int cur = comp;
      while ( targetComp.count( cur ) && !onChain[cur] )
      {
        onChain[cur] = true;
        chain.push_back( cur );
        const int next = targetComp[cur];
        if ( next < 0 )
          break;
        cur = next;
      }
      if ( targetComp.count( cur ) && onChain[cur] && targetComp[cur] >= 0 && cur == comp )
      {
        // Full cycle back to the start: collapse to the lowest class id.
        int lowest = std::numeric_limits<int>::max();
        for ( const int c : chain )
          lowest = std::min( lowest, map.classOf[c] );
        rewriteTo[comp] = lowest;
        continue;
      }
      rewriteTo[comp] = ( targetComp.count( cur ) && targetComp.at( cur ) < 0 ) ? kNoData : map.classOf[cur];
    }
    for ( size_t i = 0; i < out.size(); ++i )
    {
      const auto it = rewriteTo.find( map.labels[i] );
      if ( it != rewriteTo.end() )
        out[i] = it->second;
    }
  }
  return out;
}

std::vector<int> ClassificationPostProcessor::clumpAndEliminate( std::span<const int> inClassification,
                                                                 int width, int height,
                                                                 int minPixelSize, int connectivity )
{
  return applySieveFilter( inClassification, width, height, minPixelSize, connectivity );
}

} // namespace rs::processing
