// src/operators/runtime/detection_fusion.cpp — see detection_fusion.h.
#include "operators/runtime/detection_fusion.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

/// IoU of two raster-pixel boxes (corner + size form) — the same geometry the
/// single-model NMS uses, so a cluster match and a suppression decision mean
/// the same thing everywhere.
double boxIou( const DetectionBox &a, const DetectionBox &b )
{
  const double ax1 = a.x, ay1 = a.y, ax2 = a.x + a.w, ay2 = a.y + a.h;
  const double bx1 = b.x, by1 = b.y, bx2 = b.x + b.w, by2 = b.y + b.h;
  const double interW = std::max( 0.0, std::min( ax2, bx2 ) - std::max( ax1, bx1 ) );
  const double interH = std::max( 0.0, std::min( ay2, by2 ) - std::max( ay1, by1 ) );
  const double inter = interW * interH;
  const double areaA = std::max( 0.0, ax2 - ax1 ) * std::max( 0.0, ay2 - ay1 );
  const double areaB = std::max( 0.0, bx2 - bx1 ) * std::max( 0.0, by2 - by1 );
  const double unionArea = areaA + areaB - inter;
  return unionArea > 0.0 ? inter / unionArea : 0.0;
}

/// A fusion cluster: the running weighted sums plus the current representative
/// (the fused box) that later candidates match against.
struct Cluster
{
  int classId = 0;
  double sumEffective = 0.0;   ///< Σ confidence·weight over absorbed boxes
  double sumX = 0.0;           ///< Σ confidence·weight·x
  double sumY = 0.0;
  double sumW = 0.0;
  double sumH = 0.0;
  std::size_t absorbed = 0;    ///< boxes absorbed (NOT distinct members)
  DetectionBox representative; ///< current fused box (match target)
};

/// Deterministic total order over pooled boxes: effective score desc, then
/// class asc, then geometry asc. Never depends on input order or addresses.
bool boxOrderLess( const DetectionBox &a, double aEffective, const DetectionBox &b,
                   double bEffective )
{
  if ( aEffective != bEffective )
    return aEffective > bEffective;
  if ( a.classId != b.classId )
    return a.classId < b.classId;
  if ( a.x != b.x )
    return a.x < b.x;
  if ( a.y != b.y )
    return a.y < b.y;
  if ( a.w != b.w )
    return a.w < b.w;
  return a.h < b.h;
}

/// Deterministic order of the FUSED product: confidence desc, then class,
/// then geometry — a pure function of the fused data.
bool fusedOrderLess( const DetectionBox &a, const DetectionBox &b )
{
  if ( a.confidence != b.confidence )
    return a.confidence > b.confidence;
  if ( a.classId != b.classId )
    return a.classId < b.classId;
  if ( a.x != b.x )
    return a.x < b.x;
  if ( a.y != b.y )
    return a.y < b.y;
  if ( a.w != b.w )
    return a.w < b.w;
  return a.h < b.h;
}

/// True when a decoded box can participate in a weighted average at all:
/// finite, positive-area geometry and a finite confidence. Non-finite inputs
/// never reach the product (defensive — decodeDetections already clamps).
bool boxIsFusable( const DetectionBox &box )
{
  return std::isfinite( box.x ) && std::isfinite( box.y ) && std::isfinite( box.w )
         && std::isfinite( box.h ) && std::isfinite( box.confidence ) && box.w > 0.0f
         && box.h > 0.0f;
}

} // namespace

std::string DetectionFusionContract::validate() const
{
  if ( !std::isfinite( iouThreshold ) || iouThreshold <= 0.0 || iouThreshold > 1.0 )
    return "ensemble.detection.iou_threshold must be a finite value in (0, 1]";
  if ( !std::isfinite( skipBoxThreshold ) || skipBoxThreshold < 0.0
       || skipBoxThreshold >= 1.0 )
    return "ensemble.detection.skip_box_threshold must be a finite value in [0, 1)";
  return {};
}

DetectionFusionResult fuseDetectionsWbf( const std::vector<DetectionMemberBoxes> &members,
                                         const DetectionFusionContract &contract,
                                         const CancelProbe &cancelled )
{
  DetectionFusionResult result;
  result.memberSurviving.assign( members.size(), 0 );
  result.memberMerged.assign( members.size(), 0 );

  // --- Gate + pool ---------------------------------------------------------
  // A box enters the fusion only when its RAW confidence clears the gate (the
  // reference gates the raw score the same way), its geometry is finite and
  // positive-area (an addition over the reference, which would carry a NaN
  // into the cluster), and its member weight is positive (effective score
  // confidence×weight > 0 — a zero-weight member cannot contribute to a
  // weighted average, and a zero-confidence box has no score to contribute).
  struct PooledBox
  {
    DetectionBox box;
    double effective;   ///< confidence × member weight
    std::size_t member; ///< index into `members` (provenance only)
  };
  std::vector<PooledBox> pooled;
  double totalWeight = 0.0;
  for ( std::size_t m = 0; m < members.size(); ++m )
  {
    totalWeight += std::max( 0.0, members[m].weight );
    for ( const DetectionBox &box : members[m].boxes )
    {
      ++result.boxesPooled;
      if ( !boxIsFusable( box ) || !( box.confidence >= contract.skipBoxThreshold ) )
      {
        ++result.boxesGated;
        continue;
      }
      const double effective = static_cast<double>( box.confidence )
                               * std::max( 0.0, members[m].weight );
      if ( !( effective > 0.0 ) )
      {
        ++result.boxesGated;
        continue;
      }
      ++result.memberSurviving[m];
      pooled.push_back( PooledBox{ box, effective, m } );
    }
  }
  if ( pooled.empty() )
    return result;

  std::sort( pooled.begin(), pooled.end(),
             []( const PooledBox &a, const PooledBox &b ) {
               return boxOrderLess( a.box, a.effective, b.box, b.effective );
             } );

  // --- Cluster -------------------------------------------------------------
  // Canonical WBF: visit boxes in deterministic order; each joins the cluster
  // with the same class whose current representative has the best IoU with it
  // when that IoU is strictly above the threshold; otherwise it opens a new
  // cluster. Matching against the LIVE representative (not the first box) is
  // what makes the fused coordinates the weighted average of the whole
  // cluster.
  std::vector<Cluster> clusters;
  clusters.reserve( pooled.size() );
  // #1176: cancellation probe every 256 pooled boxes (F-OPS-5 NMS pattern).
  // The cluster scan remains O(N²) for the no-merge cliff; cancel lets a long
  // ensemble run fail closed instead of freezing the host for minutes.
  std::size_t pooledIndex = 0;
  for ( const PooledBox &entry : pooled )
  {
    if ( cancelled && ( pooledIndex++ % 256 == 0 ) )
      cancelled();
    std::size_t best = clusters.size();
    double bestIou = contract.iouThreshold; // strictly greater required
    for ( std::size_t c = 0; c < clusters.size(); ++c )
    {
      if ( clusters[c].classId != entry.box.classId )
        continue;
      const double iou = boxIou( clusters[c].representative, entry.box );
      if ( iou > bestIou )
      {
        bestIou = iou;
        best = c;
      }
    }
    if ( best == clusters.size() )
    {
      Cluster cluster;
      cluster.classId = entry.box.classId;
      cluster.sumEffective = entry.effective;
      cluster.sumX = entry.effective * entry.box.x;
      cluster.sumY = entry.effective * entry.box.y;
      cluster.sumW = entry.effective * entry.box.w;
      cluster.sumH = entry.effective * entry.box.h;
      cluster.absorbed = 1;
      cluster.representative = entry.box;
      clusters.push_back( cluster );
    }
    else
    {
      Cluster &cluster = clusters[best];
      cluster.sumEffective += entry.effective;
      cluster.sumX += entry.effective * entry.box.x;
      cluster.sumY += entry.effective * entry.box.y;
      cluster.sumW += entry.effective * entry.box.w;
      cluster.sumH += entry.effective * entry.box.h;
      ++cluster.absorbed;
      cluster.representative.x = static_cast<float>( cluster.sumX / cluster.sumEffective );
      cluster.representative.y = static_cast<float>( cluster.sumY / cluster.sumEffective );
      cluster.representative.w = static_cast<float>( cluster.sumW / cluster.sumEffective );
      cluster.representative.h = static_cast<float>( cluster.sumH / cluster.sumEffective );
      ++result.memberMerged[entry.member];
      ++result.boxesMerged;
    }
    ++result.boxesClustered;
  }

  // --- Fused product ---------------------------------------------------------
  // Coordinates: weighted average Σ(effective·coord)/Σ(effective) (the
  // representative already carries it). Confidence: the canonical count-aware
  // mean — mean effective score × min(members, cluster size)/Σ member weights
  // — so agreement ACROSS members raises the fused confidence relative to a
  // lone detection (a cluster of one out of N members is scaled by 1/N).
  result.boxes.reserve( clusters.size() );
  const double memberCount = static_cast<double>( members.size() );
  for ( const Cluster &cluster : clusters )
  {
    DetectionBox fused = cluster.representative;
    const double meanEffective = cluster.sumEffective / static_cast<double>( cluster.absorbed );
    const double agreement = std::min( memberCount, static_cast<double>( cluster.absorbed ) );
    fused.confidence =
      static_cast<float>( meanEffective * agreement / std::max( totalWeight, 1e-12 ) );
    result.boxes.push_back( fused );
  }
  result.clusters = clusters.size();
  std::sort( result.boxes.begin(), result.boxes.end(), fusedOrderLess );
  return result;
}

} // namespace sicnu::operators::runtime
