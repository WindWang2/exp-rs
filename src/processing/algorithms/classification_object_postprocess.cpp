// classification_object_postprocess.cpp — see classification_object_postprocess.h.
#include "classification_object_postprocess.h"

#include <functional>
#include <set>
#include <vector>

namespace rs::processing
{

ClassificationObjectPostProcessor::SegmentTable
ClassificationObjectPostProcessor::computeSegmentClasses( std::span<const int> labels,
                                                          std::span<const int> segments )
{
  SegmentTable table;
  if ( labels.size() != segments.size() )
    return table;
  // Per segment: votes per class. Overall cost stays O(pixels).
  std::unordered_map<int, std::unordered_map<int, int>> votes;
  for ( size_t i = 0; i < segments.size(); ++i )
  {
    const int seg = segments[i];
    if ( seg <= 0 )
      continue; // NoData segment
    ++table[seg].area;
    const int label = labels[i];
    if ( label < 0 )
      continue; // NoData label never votes
    ++votes[seg][label];
  }
  for ( auto &[seg, classVotes] : votes )
  {
    int bestClass = -1;
    int bestCount = -1;
    for ( const auto &[classId, count] : classVotes )
    {
      // Tie → lowest class id (rs_majority_vote convention).
      if ( count > bestCount || ( count == bestCount && classId < bestClass ) )
      {
        bestClass = classId;
        bestCount = count;
      }
    }
    table[seg].majorityClass = bestClass;
  }
  return table;
}

ClassificationObjectPostProcessor::Adjacency
ClassificationObjectPostProcessor::buildAdjacency( std::span<const int> segments,
                                                   int width, int height, int connectivity )
{
  Adjacency adjacency;
  if ( width <= 0 || height <= 0 ||
       segments.size() != static_cast<size_t>( width ) * static_cast<size_t>( height ) )
    return adjacency;
  const bool use8 = connectivity != 4;
  const auto at = [&]( int x, int y ) -> int {
    return segments[static_cast<size_t>( y ) * width + x];
  };
  auto record = [&]( int a, int b ) {
    if ( a == b || a <= 0 || b <= 0 )
      return;
    ++adjacency[a][b];
    ++adjacency[b][a];
  };
  for ( int y = 0; y < height; ++y )
  {
    for ( int x = 0; x < width; ++x )
    {
      const int a = at( x, y );
      if ( x + 1 < width )
        record( a, at( x + 1, y ) );
      if ( y + 1 < height )
        record( a, at( x, y + 1 ) );
      if ( use8 )
      {
        // Diagonals anchored on (x,y): down-left and down-right. The
        // up-neighbours are covered as down-neighbours of row y-1.
        if ( x - 1 >= 0 && y + 1 < height )
          record( a, at( x - 1, y + 1 ) );
        if ( x + 1 < width && y + 1 < height )
          record( a, at( x + 1, y + 1 ) );
      }
    }
  }
  return adjacency;
}

std::unordered_map<int, int>
ClassificationObjectPostProcessor::applyMinAreaRule( const SegmentTable &table,
                                                     const Adjacency &adjacency,
                                                     int minSegmentArea,
                                                     int *mergedCount )
{
  if ( mergedCount )
    *mergedCount = 0;
  std::unordered_map<int, int> classes;
  classes.reserve( table.size() );
  for ( const auto &[seg, node] : table )
    classes[seg] = node.majorityClass;
  if ( minSegmentArea <= 0 )
    return classes;

  // Union-find over merged segments with path compression.
  std::unordered_map<int, int> parent;
  std::function<int( int )> find = [&]( int x ) -> int {
    while ( parent[x] != x )
    {
      parent[x] = parent[parent[x]];
      x = parent[x];
    }
    return x;
  };
  std::unordered_map<int, int> repArea;
  // Group members per root — frontier of a merged group = union of its
  // members' frontiers. Groups only exist below the merge threshold, so
  // member lists stay small and splicing is bounded.
  std::unordered_map<int, std::vector<int>> members;
  for ( const auto &[seg, node] : table )
  {
    parent[seg] = seg;
    repArea[seg] = node.area;
    members[seg] = { seg };
  }

  // Deterministic offender set: (area, id) pairs below the threshold.
  std::set<std::pair<int, int>> offenders;
  for ( const auto &[seg, area] : repArea )
  {
    if ( area < minSegmentArea )
      offenders.insert( { area, seg } );
  }

  int merged = 0;
  while ( !offenders.empty() )
  {
    const auto it = offenders.begin();
    const int offender = it->second;
    offenders.erase( it );
    // Stale entry guard: the root may have grown past the threshold by
    // absorbing other offenders after this entry was queued.
    if ( !repArea.count( offender ) || repArea[offender] >= minSegmentArea )
      continue;

    // Group border to neighbouring roots = union of the members' frontiers.
    std::unordered_map<int, int> groupBorder;
    const auto memberIt = members.find( offender );
    if ( memberIt != members.end() )
    {
      for ( int member : memberIt->second )
      {
        const auto adjIt = adjacency.find( member );
        if ( adjIt == adjacency.end() )
          continue;
        for ( const auto &[nbr, border] : adjIt->second )
        {
          const int nbrRoot = find( nbr );
          if ( nbrRoot != offender )
            groupBorder[nbrRoot] += border;
        }
      }
    }
    if ( groupBorder.empty() )
      continue; // isolated (NoData-only neighbourhood) — left untouched

    // Absorber: longest shared border; ties → lowest class, then lowest id.
    int absorber = 0;
    int bestBorder = -1;
    int bestClass = 0;
    for ( const auto &[nbrRoot, border] : groupBorder )
    {
      const int cls = classes.count( nbrRoot ) ? classes[nbrRoot] : -1;
      if ( border > bestBorder ||
           ( border == bestBorder &&
             ( cls < bestClass || ( cls == bestClass && nbrRoot < absorber ) ) ) )
      {
        absorber = nbrRoot;
        bestBorder = border;
        bestClass = cls;
      }
    }
    if ( absorber == 0 )
      continue;

    parent[offender] = absorber;
    repArea[absorber] += repArea[offender];
    repArea.erase( offender );
    // Splice the member list (offender groups are small by construction).
    std::vector<int> &absorberMembers = members[absorber];
    if ( memberIt != members.end() )
    {
      absorberMembers.insert( absorberMembers.end(),
                              memberIt->second.begin(), memberIt->second.end() );
      members.erase( memberIt );
    }
    if ( repArea[absorber] < minSegmentArea )
      offenders.insert( { repArea[absorber], absorber } );
    ++merged;
  }

  // Resolve every original segment through its chain to its root's class.
  for ( auto &[seg, cls] : classes )
  {
    const int root = find( seg );
    if ( root != seg )
      cls = classes[root];
  }
  if ( mergedCount )
    *mergedCount = merged;
  return classes;
}

std::unordered_map<int, int>
ClassificationObjectPostProcessor::applySmoothing( const std::unordered_map<int, int> &classes,
                                                   const Adjacency &adjacency,
                                                   int iterations,
                                                   int *changedCount )
{
  if ( changedCount )
    *changedCount = 0;
  std::unordered_map<int, int> current = classes;
  int totalChanged = 0;
  for ( int it = 0; it < iterations; ++it )
  {
    std::unordered_map<int, int> next = current;
    int changed = 0;
    for ( const auto &[seg, cls] : current )
    {
      const auto adjIt = adjacency.find( seg );
      if ( adjIt == adjacency.end() )
        continue;
      // Border weight per neighbour class (NoData classes skipped).
      std::unordered_map<int, int> classBorder;
      int totalClassed = 0;
      for ( const auto &[nbr, border] : adjIt->second )
      {
        const auto clsIt = current.find( nbr );
        if ( clsIt == current.end() || clsIt->second < 0 )
          continue;
        classBorder[clsIt->second] += border;
        totalClassed += border;
      }
      if ( totalClassed == 0 )
        continue;
      int bestClass = -1;
      int bestBorder = -1;
      for ( const auto &[cls, border] : classBorder )
      {
        if ( border > bestBorder || ( border == bestBorder && cls < bestClass ) )
        {
          bestClass = cls;
          bestBorder = border;
        }
      }
      // Strict majority over classed neighbour border; keep otherwise.
      if ( bestClass >= 0 && bestClass != cls && bestBorder * 2 > totalClassed )
      {
        next[seg] = bestClass;
        ++changed;
      }
    }
    current = std::move( next );
    totalChanged += changed;
    if ( changed == 0 )
      break;
  }
  if ( changedCount )
    *changedCount = totalChanged;
  return current;
}

ClassificationObjectPostProcessor::RunResult
ClassificationObjectPostProcessor::run( std::span<const int> labels,
                                        std::span<const int> segments,
                                        int width, int height,
                                        const ObjectPostProcessConfig &config,
                                        std::span<int> outLabels )
{
  RunResult result;
  const size_t cells = static_cast<size_t>( width ) * static_cast<size_t>( height );
  if ( width <= 0 || height <= 0 || labels.size() != cells ||
       segments.size() != cells || outLabels.size() != cells )
  {
    result.error.code = RunResult::Error::Code::SizeMismatch;
    return result;
  }

  const SegmentTable table = computeSegmentClasses( labels, segments );
  if ( static_cast<int>( table.size() ) > config.maxSegments )
  {
    result.error.code = RunResult::Error::Code::TooManySegments;
    return result;
  }
  result.stats.segmentCount = static_cast<int>( table.size() );

  const Adjacency adjacency = buildAdjacency( segments, width, height, config.connectivity );

  int merged = 0;
  const std::unordered_map<int, int> mergedClasses =
    applyMinAreaRule( table, adjacency, config.minSegmentArea, &merged );
  result.stats.mergedSegments = merged;

  int changed = 0;
  const std::unordered_map<int, int> finalClasses =
    applySmoothing( mergedClasses, adjacency, config.smoothingIterations, &changed );
  result.stats.smoothedSegments = changed;

  // Paint: every pixel receives its segment's final class; NoData segments
  // (id <= 0) and class-less segments paint -1.
  for ( size_t i = 0; i < cells; ++i )
  {
    const int seg = segments[i];
    auto it = finalClasses.find( seg );
    outLabels[i] = ( it != finalClasses.end() && it->second >= 0 ) ? it->second : -1;
  }

  result.ok = true;
  return result;
}

} // namespace rs::processing
