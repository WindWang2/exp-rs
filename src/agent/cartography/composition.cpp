// src/agent/cartography/composition.cpp
#include "composition.h"

#include "../mapspec/mapspec.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace sicnu::agent::cartography {

namespace {

constexpr double kEps = 1e-6;

struct Rect
{
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

bool sameRect( const Rect &a, const Rect &b )
{
  return std::fabs( a.x - b.x ) <= kEps && std::fabs( a.y - b.y ) <= kEps &&
         std::fabs( a.w - b.w ) <= kEps && std::fabs( a.h - b.h ) <= kEps;
}

bool toRect( const Json::Value &item, Rect &out )
{
  if ( !item.isObject() || !item.isMember( "rect_mm" ) || !item["rect_mm"].isArray() ||
       item["rect_mm"].size() != 4 )
    return false;
  out.x = item["rect_mm"][0].asDouble();
  out.y = item["rect_mm"][1].asDouble();
  out.w = item["rect_mm"][2].asDouble();
  out.h = item["rect_mm"][3].asDouble();
  return out.w > 0 && out.h > 0;
}

void writeRect( Json::Value &item, const Rect &rect )
{
  Json::Value array( Json::arrayValue );
  array.append( rect.x );
  array.append( rect.y );
  array.append( rect.w );
  array.append( rect.h );
  item["rect_mm"] = array;
}

double anchorMargin( const Json::Value &anchor, double marginDefaultMm )
{
  return anchor.isObject() && anchor.isMember( "margin_mm" ) && anchor["margin_mm"].isNumeric()
           ? anchor["margin_mm"].asDouble()
           : marginDefaultMm;
}

/// Anchor edge → (x, y) for an item of size w×h on a page W×H.
/// `edge` must already be validated with mapspec::isAnchorEdge.
void anchorPoint( const std::string &edge, double marginMm, double pageW, double pageH,
                  double w, double h, double &x, double &y )
{
  if ( edge == "top-left" )
  {
    x = marginMm;
    y = marginMm;
  }
  else if ( edge == "top-center" )
  {
    x = ( pageW - w ) / 2.0;
    y = marginMm;
  }
  else if ( edge == "top-right" )
  {
    x = pageW - marginMm - w;
    y = marginMm;
  }
  else if ( edge == "center-left" )
  {
    x = marginMm;
    y = ( pageH - h ) / 2.0;
  }
  else if ( edge == "center" )
  {
    x = ( pageW - w ) / 2.0;
    y = ( pageH - h ) / 2.0;
  }
  else if ( edge == "center-right" )
  {
    x = pageW - marginMm - w;
    y = ( pageH - h ) / 2.0;
  }
  else if ( edge == "bottom-left" )
  {
    x = marginMm;
    y = pageH - marginMm - h;
  }
  else if ( edge == "bottom-center" )
  {
    x = ( pageW - w ) / 2.0;
    y = pageH - marginMm - h;
  }
  else // bottom-right
  {
    x = pageW - marginMm - w;
    y = pageH - marginMm - h;
  }
}

/// Outcome of one constraint evaluation during a sweep.
enum class Apply
{
    Satisfied, ///< already holds — nothing written
    Applied,   ///< geometry written this pass
    Blocked,   ///< inputs not usable YET (e.g. rect materialized later by
               ///< fit_content): stays in the sweep, may resolve next pass
    Failed     ///< cannot apply (permanent cause — reported once, the
               ///< constraint leaves the sweep)
};

class Report
{
  public:
    void add( const std::string &key, const std::string &text )
    {
      if ( mSeen.insert( key ).second )
        mNotes.push_back( text );
    }
    const std::vector<std::string> &notes() const { return mNotes; }

  private:
    std::set<std::string> mSeen;
    std::vector<std::string> mNotes;
};

/// One solver constraint in normalized, resolvable form.
struct ConstraintRuntime
{
    std::string cid;
    std::string kind;
    std::vector<Json::Value *> items;   ///< resolvable targets, reference order
    std::vector<std::string> itemIds;
    Json::Value contentMm;              ///< fit_content declared content size (copied)
    double gap = 0.0;
    std::string edge;
    std::string direction;
    bool disabled = false;              ///< permanent failure — leaves the sweep
    bool blockedInLastPass = false;     ///< inputs not usable yet at fixpoint
    // Platform 7.0 explainable-solve surface (validated upstream; defensive
    // defaults reproduce the v3 all-hard behavior).
    bool isSoft = false;                ///< hardness "soft"
    int priority = 50;                  ///< 0..100
    double weight = 1.0;                ///< 0..1000 (soft only)
    int index = 0;                      ///< canonical declaration index
    int rank = 0;                       ///< canonical order position (set by sortCanonical)
    bool decided = false;               ///< first hard application recorded?
    bool anchorDecided = false;         ///< anchor_wins decision recorded?
    bool rejected = false;              ///< soft: reverted by a higher-ranked constraint
};

/// A pending geometry write computed by computeTargets.
struct TargetWrite
{
    Json::Value *item = nullptr;
    Rect target;
};

class Solver
{
  public:
    Solver( Json::Value &spec, double marginDefaultMm, Report &report )
      : mSpec( spec ), mMargin( marginDefaultMm ), mReport( report ) {}

    // --- bounded pipeline ---------------------------------------------------
    void normalize();     ///< item index in canonical collection order
    void buildRuntimes(); ///< declared constraints → normalized form (#781 aware)
    void sortCanonical(); ///< multi-fixpoint policy: canonical constraint order
    void detectCycles();  ///< leader→follower cycles, diagnosed not absorbed
    void snapshotGeometry(); ///< pre-solve rects for the unsat-core simulation
    void relaxHard();     ///< anchors + clamps + HARD constraints to a fixpoint
    void searchCores();   ///< bounded unsat cores for unsatisfied hard constraints
    void applySofts();    ///< SOFT constraints: priority-desc greedy, rank-aware
    void finalize( CompositionResult &result );

  private:
    Json::Value *findItem( const std::string &id );
    bool resolveAnchors();
    bool clampSizes();
    /// Computes the geometry a constraint wants, WITHOUT writing. `targets`
    /// receives one entry per item the constraint would change (only entries
    /// that differ from the current rect). Returns Blocked/Failed when the
    /// constraint cannot be evaluated; Satisfied when `targets` ends empty
    /// for "already holds" reasons.
    Apply computeTargets( const ConstraintRuntime &c, std::vector<TargetWrite> &targets,
                          std::string *failReason = nullptr ) const;
    /// Writes the computed targets (pure mechanical step).
    static void applyTargets( const std::vector<TargetWrite> &targets );
    /// Full sweep of the active hard constraints (+ anchors + clamps). Returns
    /// true when anything was written. Honors mSimulating (no reports, no
    /// disablement recording beyond runtime state).
    bool sweepHardOnce();
    /// True when `c` currently holds at the live geometry (no writes).
    bool holds( const ConstraintRuntime &c ) const;
    /// Anchor-authority guard: non-fit_content constraints whose follower is
    /// anchor-pinned are disabled with a targeted report (6.0 rule).
    void enforceAnchorAuthority( ConstraintRuntime &c );
    /// Appends a decision, bounded by kMaxDecisions (overflow noted once).
    void decide( const ConstraintRuntime &c, const std::string &outcome,
                 const std::string &reason, bool recordAlways = false );

    struct RectSnapshot
    {
        Json::Value *item = nullptr;
        Json::Value rect; ///< deep copy of the item's rect_mm (or null)
    };
    void saveRects( std::vector<RectSnapshot> &out ) const;
    void restoreRects( const std::vector<RectSnapshot> &snapshots ) const;

    Json::Value &mSpec;
    double mMargin = 0.0;
    Report &mReport;

    double mPageW = 297.0;
    double mPageH = 210.0;

    std::map<std::string, std::pair<std::string, int>> mById;

    std::vector<ConstraintRuntime> mConstraints;
    int mArityFailuresHard = 0;
    int mArityFailuresSoft = 0;
    int mPasses = 0;
    int mSoftPasses = 0;
    bool mConverged = true;
    bool mSoftsExhaustedBudget = false;
    std::set<std::string> mAnchorIds;
    std::set<std::string> mClampedIds;

    // Decisions ledger (bounded) + simulation mode guard.
    std::vector<CompositionDecision> mDecisions;
    bool mDecisionsTruncated = false;
    bool mSimulating = false;
    int mSimulations = 0;

    // Unsat-core search output.
    Json::Value mUnsatCores = Json::Value( Json::arrayValue );

    // Pre-solve geometry snapshot for the unsat-core simulation.
    std::vector<RectSnapshot> mPreSolveRects;
};

Json::Value *Solver::findItem( const std::string &id )
{
  const auto it = mById.find( id );
  if ( it == mById.end() )
    return nullptr;
  return &mSpec[it->second.first][it->second.second];
}

void Solver::normalize()
{
  if ( mSpec.isObject() && mSpec.isMember( "page" ) && mSpec["page"].isObject() &&
       mSpec["page"].isMember( "width_mm" ) && mSpec["page"]["width_mm"].isNumeric() &&
       mSpec["page"].isMember( "height_mm" ) && mSpec["page"]["height_mm"].isNumeric() )
  {
    mPageW = mSpec["page"]["width_mm"].asDouble();
    mPageH = mSpec["page"]["height_mm"].asDouble();
  }
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !mSpec.isMember( collection ) || !mSpec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < mSpec[collection].size(); ++i )
    {
      const Json::Value &item = mSpec[collection][i];
      if ( !item.isObject() || !item.isMember( "id" ) || !item["id"].isString() )
        continue;
      mById.emplace( item["id"].asString(),
                     std::make_pair( std::string( collection ), static_cast<int>( i ) ) );
    }
  }
  if ( mSpec.isMember( "items" ) && mSpec["items"].isArray() )
  {
    for ( Json::Value::ArrayIndex i = 0; i < mSpec["items"].size(); ++i )
    {
      const Json::Value &item = mSpec["items"][i];
      if ( !item.isObject() || !item.isMember( "id" ) || !item["id"].isString() )
        continue;
      mById.emplace( item["id"].asString(),
                     std::make_pair( std::string( "items" ), static_cast<int>( i ) ) );
    }
  }
}

void Solver::buildRuntimes()
{
  const Json::Value constraints = mSpec.get( "constraints", Json::Value( Json::arrayValue ) );
  if ( !constraints.isArray() )
    return;
  int declaredIndex = 0;
  for ( const auto &constraint : constraints )
  {
    if ( !constraint.isObject() || !constraint.isMember( "kind" ) ||
         !constraint["kind"].isString() || !constraint.isMember( "items" ) ||
         !constraint["items"].isArray() )
      continue;
    const std::string kind = constraint["kind"].asString();
    if ( !mapspec::isConstraintKind( kind ) )
      continue; // legacy/free-form constraint items (e.g. frame_style) are not solver input
    // Review P2: id-less constraints share `kind` as cid and duplicate ids
    // are legal documents — synthesize a unique identity so per-constraint
    // reports can never silently collapse into one deduped entry.
    std::string cid = kind;
    if ( constraint.isMember( "id" ) && constraint["id"].isString() &&
         !constraint["id"].asString().empty() )
      cid = constraint["id"].asString();
    else
      cid = kind + "#" + std::to_string( declaredIndex );
    const int canonicalIndex = declaredIndex;
    ++declaredIndex;
    // Issue #781: arity must know the kind — fit_content is a legal
    // single-item constraint.
    const int requiredItems = kind == "fit_content" ? 1 : 2;

    ConstraintRuntime runtime;
    runtime.cid = cid;
    runtime.kind = kind;
    runtime.index = canonicalIndex;
    runtime.contentMm = constraint.get( "content_mm", Json::Value() );
    // v4 hardness/priority/weight: validated upstream (validateMapSpec);
    // here they are read defensively — anything malformed falls back to the
    // v3 defaults instead of inventing semantics.
    if ( constraint.isMember( "hardness" ) && constraint["hardness"].isString() &&
         mapspec::isConstraintHardness( constraint["hardness"].asString() ) )
      runtime.isSoft = constraint["hardness"].asString() == "soft";
    if ( constraint.isMember( "priority" ) && constraint["priority"].isIntegral() &&
         constraint["priority"].asInt() >= 0 && constraint["priority"].asInt() <= 100 )
      runtime.priority = constraint["priority"].asInt();
    if ( runtime.isSoft && constraint.isMember( "weight" ) && constraint["weight"].isNumeric() &&
         constraint["weight"].asDouble() >= 0 && constraint["weight"].asDouble() <= 1000 )
      runtime.weight = constraint["weight"].asDouble();
    for ( const auto &reference : constraint["items"] )
    {
      if ( !reference.isString() )
        continue;
      runtime.itemIds.push_back( reference.asString() );
      if ( Json::Value *item = findItem( reference.asString() ) )
        runtime.items.push_back( item );
    }
    if ( static_cast<int>( runtime.items.size() ) < requiredItems )
    {
      mReport.add( cid + "|arity",
                   cid + ": fewer than " + std::to_string( requiredItems ) + " resolvable items" );
      if ( runtime.isSoft )
        ++mArityFailuresSoft;
      else
        ++mArityFailuresHard;
      continue;
    }
    runtime.gap = mMargin;
    if ( constraint.isMember( "gap_mm" ) && constraint["gap_mm"].isNumeric() )
      runtime.gap = constraint["gap_mm"].asDouble();
    runtime.edge = constraint.get( "edge", "" ).asString();
    runtime.direction = constraint.get( "direction", "" ).asString();
    mConstraints.push_back( std::move( runtime ) );
  }
}

void Solver::sortCanonical()
{
  // Multi-fixpoint policy: the solver's chosen fixpoint must not depend on
  // declaration order. Constraints are totally ordered by
  // (hard before soft, priority desc, weight desc, canonical index asc);
  // std::stable_sort with the full key makes the order total and the whole
  // pipeline a pure function of the document.
  std::stable_sort( mConstraints.begin(), mConstraints.end(),
                    []( const ConstraintRuntime &a, const ConstraintRuntime &b ) {
                      if ( a.isSoft != b.isSoft )
                        return !a.isSoft;
                      if ( a.priority != b.priority )
                        return a.priority > b.priority;
                      if ( std::fabs( a.weight - b.weight ) > kEps )
                        return a.weight > b.weight;
                      return a.index < b.index;
                    } );
  for ( int i = 0; i < static_cast<int>( mConstraints.size() ); ++i )
    mConstraints[i].rank = i;
}

void Solver::detectCycles()
{
  // Leader → follower write edges over positional constraints. A cycle means
  // constraints fight over the same geometry: relaxation stays bounded, and
  // the cycle is diagnosed instead of silently absorbed. fit_content writes
  // only its own item's size and distribute fixes endpoints, so neither can
  // participate in a positional cycle.
  std::map<std::string, std::set<std::string>> adjacency;
  for ( const auto &c : mConstraints )
  {
    if ( c.kind == "fit_content" || c.kind == "distribute" )
      continue;
    if ( c.kind == "stack" )
    {
      for ( int i = 1; i < static_cast<int>( c.itemIds.size() ); ++i )
        adjacency[c.itemIds[i - 1]].insert( c.itemIds[i] );
    }
    else if ( c.itemIds.size() >= 2 )
    {
      adjacency[c.itemIds[0]].insert( c.itemIds[1] );
    }
  }

  // Self-reachability (bounded BFS per node over the small constraint graph):
  // a node is on a cycle exactly when it can reach itself.
  std::set<std::string> cyclic;
  for ( const auto &entry : adjacency )
  {
    const std::string &node = entry.first;
    // `visited` deliberately does NOT contain node: node must stay
    // enqueue-able so the "came back to node" test below can fire.
    std::set<std::string> visited;
    std::vector<std::string> queue( entry.second.begin(), entry.second.end() );
    bool reachesSelf = false;
    for ( int head = 0; head < static_cast<int>( queue.size() ); ++head )
    {
      const std::string current = queue[head];
      if ( current == node )
      {
        reachesSelf = true;
        break;
      }
      if ( !visited.insert( current ).second )
        continue;
      const auto it = adjacency.find( current );
      if ( it != adjacency.end() )
        for ( const std::string &next : it->second )
          if ( !visited.count( next ) )
            queue.push_back( next );
    }
    if ( reachesSelf )
      cyclic.insert( node );
  }

  if ( cyclic.empty() )
    return;
  std::string nodes;
  for ( const std::string &node : cyclic )
    nodes += ( nodes.empty() ? "" : ", " ) + node;
  for ( auto &c : mConstraints )
  {
    if ( c.kind == "fit_content" || c.kind == "distribute" )
      continue;
    bool touchesCycle = false;
    for ( const std::string &id : c.itemIds )
      touchesCycle = touchesCycle || cyclic.count( id ) > 0;
    if ( touchesCycle )
    {
      mReport.add( c.cid + "|cycle",
                   c.cid + ": participates in a cyclic constraint dependency (" + nodes +
                     "); residual violations are reported, not absorbed" );
      decide( c, "cycle", "participates in a cyclic dependency (" + nodes + ")" );
    }
  }
}

bool Solver::resolveAnchors()
{
  bool wrote = false;
  std::vector<std::string> allCollections;
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    allCollections.push_back( mapspec::kCollections[c] );
  allCollections.push_back( "items" );
  for ( const auto &collection : allCollections )
  {
    if ( !mSpec.isMember( collection ) || !mSpec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < mSpec[collection].size(); ++i )
    {
      Json::Value &item = mSpec[collection][i];
      if ( !item.isObject() || !item.isMember( "anchor" ) || !item["anchor"].isObject() )
        continue;
      const Json::Value &anchor = item["anchor"];
      if ( !anchor.isMember( "edge" ) || !anchor["edge"].isString() )
        continue;
      const std::string id = item.isMember( "id" ) && item["id"].isString()
                               ? item["id"].asString()
                               : std::string();
      Rect rect;
      if ( !toRect( item, rect ) )
      {
        // Size-less items: use min_size_mm when declared so the anchor still
        // resolves; otherwise report.
        if ( item.isMember( "min_size_mm" ) && item["min_size_mm"].isArray() &&
             item["min_size_mm"].size() == 2 && item["min_size_mm"][0].isNumeric() &&
             item["min_size_mm"][1].isNumeric() )
        {
          rect.w = item["min_size_mm"][0].asDouble();
          rect.h = item["min_size_mm"][1].asDouble();
        }
        else
        {
          mReport.add( id + "|anchor-no-size", id + ": anchor needs rect_mm or min_size_mm" );
          continue;
        }
      }
      const std::string edge = anchor["edge"].asString();
      if ( !mapspec::isAnchorEdge( edge ) )
      {
        mReport.add( id + "|anchor-edge", id + ": unknown anchor edge '" + edge + "'" );
        continue;
      }
      double x = 0.0;
      double y = 0.0;
      anchorPoint( edge, anchorMargin( anchor, mMargin ), mPageW, mPageH, rect.w, rect.h, x, y );
      if ( x < 0 || y < 0 || x + rect.w > mPageW || y + rect.h > mPageH )
      {
        // Reported, never written: writing an off-page rect would make the
        // repair loop clamp and re-pin forever.
        mReport.add( id + "|anchor-off-page",
                     id + ": anchor '" + edge + "' cannot place the item inside "
                     "the page (size " + std::to_string( rect.w ) + "×" +
                     std::to_string( rect.h ) + " mm)" );
        continue;
      }
      const Rect target{ x, y, rect.w, rect.h };
      if ( !sameRect( rect, target ) )
      {
        rect.x = x;
        rect.y = y;
        writeRect( item, rect );
        wrote = true;
      }
      mAnchorIds.insert( id );
    }
  }
  return wrote;
}

bool Solver::clampSizes()
{
  bool wrote = false;
  std::vector<std::string> allCollections;
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    allCollections.push_back( mapspec::kCollections[c] );
  allCollections.push_back( "items" );
  for ( const auto &collection : allCollections )
  {
    if ( !mSpec.isMember( collection ) || !mSpec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < mSpec[collection].size(); ++i )
    {
      Json::Value &item = mSpec[collection][i];
      if ( !item.isObject() )
        continue;
      Rect rect;
      if ( !toRect( item, rect ) )
        continue;
      const Json::Value &minSize = item.get( "min_size_mm", Json::Value() );
      const Json::Value &maxSize = item.get( "max_size_mm", Json::Value() );
      Rect clamped = rect;
      if ( minSize.isArray() && minSize.size() == 2 )
      {
        clamped.w = std::max( clamped.w, minSize[0].asDouble() );
        clamped.h = std::max( clamped.h, minSize[1].asDouble() );
      }
      if ( maxSize.isArray() && maxSize.size() == 2 )
      {
        clamped.w = std::min( clamped.w, maxSize[0].asDouble() );
        clamped.h = std::min( clamped.h, maxSize[1].asDouble() );
      }
      if ( std::fabs( clamped.w - rect.w ) > kEps || std::fabs( clamped.h - rect.h ) > kEps )
      {
        // Keep the origin: clamping grows/shrinks from the top-left corner.
        // Bottom/right-anchored items should declare the anchor instead of
        // relying on clamp edge behavior.
        clamped.x = rect.x;
        clamped.y = rect.y;
        writeRect( item, clamped );
        if ( item.isMember( "id" ) && item["id"].isString() )
          mClampedIds.insert( item["id"].asString() );
        wrote = true;
      }
    }
  }
  return wrote;
}

Apply Solver::computeTargets( const ConstraintRuntime &c, std::vector<TargetWrite> &targets,
                              std::string *failReason ) const
{
  const std::string &kind = c.kind;
  const std::string &edge = c.edge;
  const std::string &direction = c.direction;
  const double gap = c.gap;
  auto rectOf = []( const Json::Value *item, Rect &out ) { return toRect( *item, out ); };
  auto push = [ &targets ]( Json::Value *item, const Rect &target ) {
    targets.push_back( { item, target } );
  };

  if ( kind == "align" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
      return Apply::Blocked;
    if ( edge != "top" && edge != "bottom" && edge != "left" && edge != "right" )
    {
      if ( failReason )
        *failReason = "unknown align edge '" + edge + "'";
      return Apply::Failed;
    }
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
        return Apply::Blocked;
      Rect target = rect;
      if ( edge == "top" )
        target.y = leader.y;
      else if ( edge == "bottom" )
        target.y = leader.y + leader.h - rect.h;
      else if ( edge == "left" )
        target.x = leader.x;
      else
        target.x = leader.x + leader.w - rect.w;
      if ( !sameRect( rect, target ) )
        push( c.items[i], target );
    }
    return targets.empty() ? Apply::Satisfied : Apply::Applied;
  }

  if ( kind == "match_width" || kind == "match_height" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
      return Apply::Blocked;
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
        return Apply::Blocked;
      Rect target = rect;
      if ( kind == "match_width" )
        target.w = leader.w;
      else
        target.h = leader.h;
      if ( !sameRect( rect, target ) )
        push( c.items[i], target );
    }
    return targets.empty() ? Apply::Satisfied : Apply::Applied;
  }

  if ( kind == "stack" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
      return Apply::Blocked;
    if ( direction != "below" && direction != "above" && direction != "left_of" &&
         direction != "right_of" )
    {
      if ( failReason )
        *failReason = "unknown stack direction '" + direction + "'";
      return Apply::Failed;
    }
    Rect previous = leader;
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
        return Apply::Blocked;
      Rect target = rect;
      if ( direction == "below" )
      {
        target.y = previous.y + previous.h + gap;
        target.x = leader.x;
      }
      else if ( direction == "above" )
      {
        target.y = previous.y - gap - rect.h;
        target.x = leader.x;
      }
      else if ( direction == "right_of" )
      {
        target.x = previous.x + previous.w + gap;
        target.y = leader.y;
      }
      else // left_of
      {
        target.x = previous.x - gap - rect.w;
        target.y = leader.y;
      }
      if ( !sameRect( rect, target ) )
        push( c.items[i], target );
      previous = target;
    }
    return targets.empty() ? Apply::Satisfied : Apply::Applied;
  }

  if ( kind == "distribute" )
  {
    // Even spacing between the first and last item along the direction.
    Rect first;
    Rect last;
    if ( !rectOf( c.items.front(), first ) || !rectOf( c.items.back(), last ) )
      return Apply::Blocked;
    if ( direction != "horizontal" && direction != "vertical" )
    {
      if ( failReason )
        *failReason = "unknown distribute direction '" + direction + "'";
      return Apply::Failed;
    }
    const int count = static_cast<int>( c.items.size() );
    if ( direction == "horizontal" )
    {
      const double span = ( last.x - ( first.x + first.w ) ) / std::max( 1, count - 1 );
      for ( int i = 1; i < count - 1; ++i )
      {
        Rect rect;
        if ( !rectOf( c.items[i], rect ) )
          continue;
        Rect target = rect;
        target.x = first.x + first.w + span * i;
        if ( !sameRect( rect, target ) )
          push( c.items[i], target );
      }
    }
    else
    {
      const double span = ( last.y - ( first.y + first.h ) ) / std::max( 1, count - 1 );
      for ( int i = 1; i < count - 1; ++i )
      {
        Rect rect;
        if ( !rectOf( c.items[i], rect ) )
          continue;
        Rect target = rect;
        target.y = first.y + first.h + span * i;
        if ( !sameRect( rect, target ) )
          push( c.items[i], target );
      }
    }
    return targets.empty() ? Apply::Satisfied : Apply::Applied;
  }

  if ( kind == "below" || kind == "above" || kind == "left_of" || kind == "right_of" )
  {
    // v3 relative placement: items = [target, follower] + optional gap_mm.
    Rect targetRect;
    Rect follower;
    if ( !rectOf( c.items[0], targetRect ) || !rectOf( c.items[1], follower ) )
      return Apply::Blocked;
    Rect result = follower;
    if ( kind == "below" )
    {
      result.x = targetRect.x;
      result.y = targetRect.y + targetRect.h + gap;
    }
    else if ( kind == "above" )
    {
      result.x = targetRect.x;
      result.y = targetRect.y - gap - follower.h;
    }
    else if ( kind == "right_of" )
    {
      result.x = targetRect.x + targetRect.w + gap;
      result.y = targetRect.y;
    }
    else // left_of
    {
      result.x = targetRect.x - gap - follower.w;
      result.y = targetRect.y;
    }
    if ( sameRect( follower, result ) )
      return Apply::Satisfied;
    push( c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "inside" )
  {
    // items = [container, content]: center the content inside the
    // container, clamped to stay within it.
    Rect container;
    Rect content;
    if ( !rectOf( c.items[0], container ) || !rectOf( c.items[1], content ) )
      return Apply::Blocked;
    Rect result = content;
    result.x = container.x + ( container.w - content.w ) / 2.0;
    result.y = container.y + ( container.h - content.h ) / 2.0;
    result.x = std::max( container.x, std::min( result.x, container.x + container.w - content.w ) );
    result.y = std::max( container.y, std::min( result.y, container.y + container.h - content.h ) );
    if ( sameRect( content, result ) )
      return Apply::Satisfied;
    push( c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "keep_with" )
  {
    // items = [anchor, companion]: pin the companion directly below the
    // anchor with the gap, preserving its horizontal position.
    Rect anchorRect;
    Rect companion;
    if ( !rectOf( c.items[0], anchorRect ) || !rectOf( c.items[1], companion ) )
      return Apply::Blocked;
    Rect result = companion;
    result.y = anchorRect.y + anchorRect.h + gap;
    if ( sameRect( companion, result ) )
      return Apply::Satisfied;
    push( c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "avoid_overlap" )
  {
    // items = [keeper, mover]: when they intersect, the mover is pushed
    // below the keeper by the gap (deterministic resolution direction).
    Rect keeper;
    Rect mover;
    if ( !rectOf( c.items[0], keeper ) || !rectOf( c.items[1], mover ) )
      return Apply::Blocked;
    const bool overlaps = mover.x < keeper.x + keeper.w && keeper.x < mover.x + mover.w &&
                          mover.y < keeper.y + keeper.h && keeper.y < mover.y + mover.h;
    if ( !overlaps )
      return Apply::Satisfied;
    Rect result = mover;
    result.y = keeper.y + keeper.h + gap;
    if ( sameRect( mover, result ) )
      return Apply::Satisfied;
    push( c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "fit_content" )
  {
    // items = [item] with content_mm [w, h]: resize the item to its
    // declared content size clamped by min/max_size_mm. Single-item
    // relative constraint (target + declared content).
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
      return Apply::Blocked;
    Json::Value content = c.contentMm;
    if ( !content.isArray() || content.size() != 2 || !content[0].isNumeric() ||
         !content[1].isNumeric() )
    {
      content = c.items[0]->get( "content_mm", Json::Value() );
    }
    if ( !content.isArray() || content.size() != 2 || !content[0].isNumeric() ||
         !content[1].isNumeric() )
    {
      if ( failReason )
        *failReason = "fit_content needs content_mm";
      return Apply::Failed;
    }
    Rect rect = leader;
    rect.w = content[0].asDouble();
    rect.h = content[1].asDouble();
    const Json::Value &minSize = c.items[0]->get( "min_size_mm", Json::Value() );
    const Json::Value &maxSize = c.items[0]->get( "max_size_mm", Json::Value() );
    if ( minSize.isArray() && minSize.size() == 2 )
    {
      rect.w = std::max( rect.w, minSize[0].asDouble() );
      rect.h = std::max( rect.h, minSize[1].asDouble() );
    }
    if ( maxSize.isArray() && maxSize.size() == 2 )
    {
      rect.w = std::min( rect.w, maxSize[0].asDouble() );
      rect.h = std::min( rect.h, maxSize[1].asDouble() );
    }
    rect.x = leader.x;
    rect.y = leader.y;
    if ( sameRect( leader, rect ) )
      return Apply::Satisfied;
    push( c.items[0], rect );
    return Apply::Applied;
  }

  if ( failReason )
    *failReason = "unsupported constraint kind '" + c.kind + "'";
  return Apply::Failed;
}

void Solver::applyTargets( const std::vector<TargetWrite> &targets )
{
  for ( const TargetWrite &write : targets )
    writeRect( *write.item, write.target );
}

bool Solver::holds( const ConstraintRuntime &c ) const
{
  std::vector<TargetWrite> targets;
  const Apply outcome = computeTargets( c, targets );
  return outcome == Apply::Satisfied; // no pending writes == currently holds
}

void Solver::decide( const ConstraintRuntime &c, const std::string &outcome,
                     const std::string &reason, bool recordAlways )
{
  if ( mSimulating )
    return;
  if ( outcome == "satisfied" && !recordAlways )
    return; // the ledger records decisions, not every no-op check
  if ( static_cast<int>( mDecisions.size() ) >= kMaxDecisions )
  {
    mDecisionsTruncated = true;
    return;
  }
  CompositionDecision decision;
  decision.cid = c.cid;
  decision.kind = c.kind;
  decision.outcome = outcome;
  decision.reason = reason;
  decision.order = static_cast<int>( mDecisions.size() );
  mDecisions.push_back( std::move( decision ) );
}

void Solver::enforceAnchorAuthority( ConstraintRuntime &c )
{
  if ( c.kind == "fit_content" || c.disabled )
    return;
  std::string anchoredFollower;
  for ( int k = 1; k < static_cast<int>( c.itemIds.size() ); ++k )
    if ( mAnchorIds.count( c.itemIds[k] ) )
    {
      anchoredFollower = c.itemIds[k];
      break;
    }
  if ( anchoredFollower.empty() )
    return;
  c.disabled = true;
  if ( mSimulating || c.anchorDecided )
    return;
  c.anchorDecided = true;
  const std::string reason = "conflicts with a declared anchor on '" + anchoredFollower +
                             "'; the anchor wins and the constraint is disabled";
  mReport.add( c.cid + "|anchor-conflict", c.cid + ": " + reason );
  decide( c, "anchor_wins", reason );
}

bool Solver::sweepHardOnce()
{
  bool wrote = resolveAnchors();
  wrote = clampSizes() || wrote;
  for ( auto &c : mConstraints )
  {
    if ( c.isSoft || c.disabled )
      continue;
    enforceAnchorAuthority( c );
    if ( c.disabled )
      continue;
    std::vector<TargetWrite> targets;
    std::string failReason;
    const Apply outcome = computeTargets( c, targets, &failReason );
    if ( outcome == Apply::Applied )
    {
      applyTargets( targets );
      wrote = true;
      if ( !mSimulating && !c.decided )
      {
        c.decided = true;
        decide( c, "applied", "hard constraint applied during relaxation (pass " +
                                std::to_string( mPasses ) + ")" );
      }
    }
    else if ( outcome == Apply::Failed )
    {
      c.disabled = true; // reported once; leaves the sweep
      if ( !mSimulating )
        mReport.add( c.cid + "|failed",
                     c.cid + ": " + ( failReason.empty()
                                        ? std::string( "cannot be applied; it leaves the sweep" )
                                        : failReason + " — it leaves the sweep" ) );
    }
    else if ( outcome == Apply::Blocked )
    {
      c.blockedInLastPass = true; // inputs may materialize in a later pass
    }
  }
  return wrote;
}

void Solver::relaxHard()
{
  for ( mPasses = 1; mPasses <= kMaxRelaxationPasses; ++mPasses )
  {
    for ( auto &c : mConstraints )
      c.blockedInLastPass = false;
    const bool wrote = sweepHardOnce();
    if ( !wrote )
    {
      // Fixpoint: every satisfiable constraint holds, nothing moved this
      // pass — the layout is stable.
      mConverged = true;
      return;
    }
  }
  mPasses = kMaxRelaxationPasses;
  mConverged = false;
  std::string ids;
  for ( const auto &c : mConstraints )
    if ( !c.isSoft && !c.disabled )
      ids += ( ids.empty() ? "" : ", " ) + c.cid;
  mReport.add( "|non-convergence",
               "constraint relaxation did not converge within " +
                 std::to_string( kMaxRelaxationPasses ) + " passes; still active: " +
                 ( ids.empty() ? std::string( "(none)" ) : ids ) );
}

void Solver::snapshotGeometry()
{
  mPreSolveRects.clear();
  saveRects( mPreSolveRects );
}

void Solver::saveRects( std::vector<RectSnapshot> &out ) const
{
  out.clear();
  // Iterate the collections directly (not mById): anchors and size clamps
  // also touch id-less items, and the restore must cover every rect the
  // solver could have modified.
  std::vector<std::string> allCollections;
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    allCollections.push_back( mapspec::kCollections[c] );
  allCollections.push_back( "items" );
  for ( const auto &collection : allCollections )
  {
    if ( !mSpec.isMember( collection ) || !mSpec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < mSpec[collection].size(); ++i )
    {
      Json::Value &item = mSpec[collection][i];
      if ( !item.isObject() )
        continue;
      RectSnapshot snapshot;
      snapshot.item = &item;
      snapshot.rect = item.isMember( "rect_mm" ) ? item.get( "rect_mm", Json::Value() )
                                                 : Json::Value();
      out.push_back( std::move( snapshot ) );
    }
  }
}

void Solver::restoreRects( const std::vector<RectSnapshot> &snapshots ) const
{
  for ( const RectSnapshot &snapshot : snapshots )
  {
    if ( snapshot.rect.isNull() )
      snapshot.item->removeMember( "rect_mm" );
    else
      ( *snapshot.item )["rect_mm"] = snapshot.rect;
  }
}

void Solver::searchCores()
{
  // For every active hard constraint that does NOT hold at the hard fixpoint,
  // run the bounded unsat-core search: find the smallest subset (within the
  // declared radius) of conflicting constraints whose absence would let this
  // constraint be satisfied. The search re-runs the bounded hard sweep on the
  // pre-solve geometry with the subset disabled — a bounded, honest witness,
  // never claimed to be a global minimum.
  for ( const auto &target : mConstraints )
  {
    if ( target.isSoft || target.disabled || mSimulating )
      continue;
    if ( holds( target ) )
      continue;
    // Candidate neighborhood: hard constraints sharing ≥1 item with the
    // target, canonical order, capped at kMaxCoreCandidates. The anchor of a
    // shared item participates as a pseudo-member (anchors are the stronger
    // contract — disabling their constraint opponents is the meaningful
    // counterfactual).
    std::vector<const ConstraintRuntime *> candidates;
    for ( const auto &other : mConstraints )
    {
      if ( other.isSoft || other.disabled || other.cid == target.cid )
        continue;
      bool shares = false;
      for ( const std::string &id : other.itemIds )
        shares = shares || std::count( target.itemIds.begin(), target.itemIds.end(), id ) > 0;
      if ( !shares )
        continue;
      if ( static_cast<int>( candidates.size() ) < kMaxCoreCandidates )
        candidates.push_back( &other );
    }

    Json::Value coreEntry( Json::objectValue );
    coreEntry["constraint"] = target.cid;
    bool found = false;
    bool bounded = false;
    std::vector<int> chosen;
    const int n = static_cast<int>( candidates.size() );
    const int maxSubset = std::min( kMaxCoreSubsetSize, n );
    for ( int size = 1; size <= maxSubset && !found; ++size )
    {
      std::vector<int> combination( size );
      for ( int i = 0; i < size; ++i )
        combination[i] = i;
      while ( !found )
      {
        if ( ++mSimulations > kMaxCoreSimulations )
        {
          bounded = true;
          break;
        }
        // Simulation: pre-solve geometry + all candidates re-enabled except
        // the tested subset, reports suppressed.
        restoreRects( mPreSolveRects );
        for ( auto &c : mConstraints )
          c.disabled = false;
        for ( int idx : combination )
          candidates[idx]->disabled = true;
        mSimulating = true;
        for ( int pass = 0; pass < kMaxRelaxationPasses; ++pass )
        {
          if ( !sweepHardOnce() )
            break;
        }
        mSimulating = false;
        found = holds( target );
        if ( found )
          chosen = combination;
        // Next combination (lexicographic index order).
        int pos = size - 1;
        while ( pos >= 0 && combination[pos] == n - size + pos )
          --pos;
        if ( pos < 0 )
          break;
        ++combination[pos];
        for ( int i = pos + 1; i < size; ++i )
          combination[i] = combination[i - 1] + 1;
      }
      if ( bounded )
        break;
    }
    // Restore the real post-fixpoint state.
    restoreRects( mPreSolveRects );
    for ( auto &c : mConstraints )
      c.disabled = false;
    for ( int pass = 0; pass < kMaxRelaxationPasses; ++pass )
    {
      if ( !sweepHardOnce() )
        break;
    }
    // Re-apply anchor authority + failed leave-sweep state for disabled
    // constraints so finalize() counts them exactly as the real run did.
    for ( auto &c : mConstraints )
    {
      if ( c.isSoft || c.disabled )
        continue;
      enforceAnchorAuthority( c );
    }

    coreEntry["core"] = Json::Value( Json::arrayValue );
    coreEntry["core"].append( target.cid );
    if ( found )
    {
      for ( int idx : chosen )
        coreEntry["core"].append( candidates[idx]->cid );
      coreEntry["explanation"] = "'" + target.cid + "' is satisfiable only without " +
                                 std::to_string( static_cast<int>( chosen.size() ) ) +
                                 " of its conflicting constraints (bounded search)";
      mReport.add( target.cid + "|unsat-core",
                   target.cid + ": unsatisfied — minimal conflicting subset found: " +
                     coreEntry["explanation"].asString() );
    }
    else
    {
      coreEntry["bounded"] = true;
      coreEntry["explanation"] =
        "no conflicting subset within the search radius (candidates: " +
        std::to_string( n ) + ", simulations: " + std::to_string( mSimulations ) +
        ") explains the failure; the constraint conflicts with the anchor-"
        "resolved geometry itself";
      mReport.add( target.cid + "|unsat-core",
                   target.cid + ": unsatisfied — " + coreEntry["explanation"].asString() );
    }
    if ( bounded )
      coreEntry["bounded"] = true;
    mUnsatCores.append( coreEntry );
  }
}

void Solver::applySofts()
{
  // SOFT phase: canonical-order greedy application. A soft application that
  // would break any hard constraint or any higher-ranked (earlier) soft
  // constraint is REVERTED and reported — rank, not luck, decides which
  // constraint keeps the scarce space.
  for ( mSoftPasses = 1; mSoftPasses <= kMaxSoftPasses; ++mSoftPasses )
  {
    bool anyWritten = false;
    for ( auto &c : mConstraints )
    {
      if ( !c.isSoft || c.disabled )
        continue;
      enforceAnchorAuthority( c );
      if ( c.disabled )
        continue;
      std::vector<TargetWrite> targets;
      std::string failReason;
      const Apply outcome = computeTargets( c, targets, &failReason );
      if ( outcome == Apply::Satisfied )
      {
        c.blockedInLastPass = false;
        continue;
      }
      if ( outcome == Apply::Failed )
      {
        c.disabled = true;
        mReport.add( c.cid + "|failed",
                     c.cid + ": " + ( failReason.empty()
                                        ? std::string( "cannot be applied; it leaves the sweep" )
                                        : failReason + " — it leaves the sweep" ) );
        continue;
      }
      if ( outcome == Apply::Blocked )
      {
        c.blockedInLastPass = true; // inputs may materialize in a later pass
        continue;
      }
      // Rank-aware conflict check: snapshot this constraint's writes, apply,
      // verify all hard + higher-ranked soft constraints still hold.
      std::vector<RectSnapshot> before;
      saveRects( before );
      applyTargets( targets );
      std::string conflict;
      for ( const auto &other : mConstraints )
      {
        if ( other.cid == c.cid || other.disabled )
          continue;
        // Hard constraints always outrank softs; softs outrank later softs
        // by canonical order position (NOT declaration index).
        const bool higherRanked = !other.isSoft || other.rank < c.rank;
        if ( !higherRanked )
          continue;
        if ( !holds( other ) )
        {
          conflict = other.cid;
          break;
        }
      }
      if ( !conflict.empty() )
      {
        restoreRects( before );
        c.rejected = true;
        const std::string reason =
          "rejected: applying it would break '" + conflict +
          "' (higher-ranked constraint keeps the geometry)";
        mReport.add( c.cid + "|rejected", c.cid + ": " + reason );
        decide( c, "rejected", reason );
        continue;
      }
      anyWritten = true;
      decide( c, "applied",
              "applied in canonical order (priority " + std::to_string( c.priority ) + ", weight " +
                std::to_string( c.weight ) + ")" );
    }
    if ( !anyWritten )
      return; // fixpoint: no soft constraint can move further
  }
  mSoftsExhaustedBudget = true;
  mReport.add( "|soft-non-convergence",
               "soft constraint application did not settle within " +
                 std::to_string( kMaxSoftPasses ) + " passes; remaining soft constraints are "
                 "reported as unsatisfied" );
}

void Solver::finalize( CompositionResult &result )
{
  int hardTotal = mArityFailuresHard;
  int hardSolved = 0;
  int softCount = 0;
  int softSatisfied = 0;
  double satisfiedWeight = 0.0;
  double violatedWeight = 0.0;
  for ( const auto &c : mConstraints )
  {
    if ( c.isSoft )
    {
      ++softCount;
      const bool holdsNow = !c.disabled && holds( c ) &&
                            !( mSoftsExhaustedBudget && c.blockedInLastPass );
      if ( holdsNow )
      {
        ++softSatisfied;
        satisfiedWeight += c.weight;
      }
      else
      {
        violatedWeight += c.weight;
        CompositionViolation violation;
        violation.cid = c.cid;
        violation.kind = c.kind;
        if ( c.disabled )
          violation.reason = "disabled (anchor conflict or permanent failure)";
        else if ( c.rejected )
          violation.reason = "rejected: a higher-ranked constraint keeps the geometry";
        else
        {
          violation.reason = "not satisfied at the final geometry";
          // Blocked softs produce no other report line — surface them so
          // preflight sees every violated soft, not only the loud ones.
          mReport.add( c.cid + "|soft-unsat",
                       c.cid + ": soft constraint not satisfied at the final geometry "
                               "(inputs never materialized)" );
        }
        result.violated.push_back( violation );
      }
      continue;
    }
    ++hardTotal;
    // Only constraints that are active AND settled count as solved: a
    // hard-failed (disabled) constraint never solved, a constraint still
    // blocked at the fixpoint never applied, and a non-converged layout
    // cannot claim any solver constraint as satisfied.
    const bool solved = mConverged && !c.disabled && !c.blockedInLastPass;
    if ( solved )
    {
      ++hardSolved;
      continue;
    }
    CompositionViolation violation;
    violation.cid = c.cid;
    violation.kind = c.kind;
    if ( c.disabled )
      violation.reason = "disabled (anchor conflict or permanent failure)";
    else if ( !mConverged )
      // On non-convergence only the constraints that verifiably do NOT hold
      // at the final geometry are listed as violated — holding constraints
      // stay out of the violation list even though none may claim "solved".
      violation.reason = holds( c ) ? "unresolved: the relaxation did not converge"
                                    : "not satisfied at the fixpoint";
    else
      violation.reason = "blocked at the fixpoint (inputs never materialized)";
    result.violated.push_back( violation );
  }
  result.constraintsTotal = hardTotal;
  result.constraintsSolved = mConverged ? hardSolved : 0;
  result.softTotal = softCount;
  result.softSatisfied = softSatisfied;
  result.satisfiedWeight = satisfiedWeight;
  result.violatedWeight = violatedWeight;
  result.passes = mPasses;
  result.converged = mConverged;
  result.anchorsResolved = static_cast<int>( mAnchorIds.size() );
  result.sizesClamped = static_cast<int>( mClampedIds.size() );
  result.decisions = std::move( mDecisions );
  if ( mDecisionsTruncated )
  {
    CompositionDecision truncated;
    truncated.cid = "(ledger)";
    truncated.kind = "(ledger)";
    truncated.outcome = "truncated";
    truncated.reason = "decisions ledger truncated at kMaxDecisions";
    result.decisions.push_back( std::move( truncated ) );
  }
  result.unsatCores = mUnsatCores;
  result.fixpointPolicy = "canonical: hard before soft, priority desc, weight desc, index asc";
  // Copied last so every report line added while accounting (e.g. blocked
  // softs) is included, in report order.
  for ( const auto &note : mReport.notes() )
    result.unsatisfied.push_back( note );
}

} // namespace

Json::Value CompositionResult::toJson() const
{
  Json::Value out( Json::objectValue );
  out["anchors_resolved"] = anchorsResolved;
  out["sizes_clamped"] = sizesClamped;
  out["constraints_solved"] = constraintsSolved;
  out["constraints_total"] = constraintsTotal;
  out["passes"] = passes;
  out["converged"] = converged;
  Json::Value array( Json::arrayValue );
  for ( const auto &note : unsatisfied )
    array.append( note );
  out["unsatisfied"] = array;
  // Platform 7.0 explainability surface (additive).
  out["soft_total"] = softTotal;
  out["soft_satisfied"] = softSatisfied;
  out["satisfied_weight"] = satisfiedWeight;
  out["violated_weight"] = violatedWeight;
  out["fixpoint_policy"] = fixpointPolicy;
  Json::Value decisions( Json::arrayValue );
  for ( const auto &decision : decisions )
  {
    Json::Value entry( Json::objectValue );
    entry["cid"] = decision.cid;
    entry["kind"] = decision.kind;
    entry["outcome"] = decision.outcome;
    entry["reason"] = decision.reason;
    entry["order"] = decision.order;
    decisions.append( entry );
  }
  out["decisions"] = decisions;
  Json::Value violated( Json::arrayValue );
  for ( const auto &violation : violated )
  {
    Json::Value entry( Json::objectValue );
    entry["cid"] = violation.cid;
    entry["kind"] = violation.kind;
    entry["reason"] = violation.reason;
    violated.append( entry );
  }
  out["violated"] = violated;
  out["unsat_cores"] = unsatCores;
  return out;
}

CompositionResult resolveComposition( Json::Value &spec, double marginDefaultMm )
{
  CompositionResult result;
  if ( !spec.isObject() || !spec.isMember( "page" ) || !spec["page"].isObject() )
    return result;
  const double pageW = spec["page"].get( "width_mm", 297.0 ).asDouble();
  const double pageH = spec["page"].get( "height_mm", 210.0 ).asDouble();
  if ( !( pageW > 0 ) || !( pageH > 0 ) )
  {
    result.unsatisfied.push_back( "page geometry must be positive before composition resolves" );
    return result;
  }

  Report report;
  Solver solver( spec, marginDefaultMm, report );
  solver.normalize();
  solver.buildRuntimes();
  solver.sortCanonical();
  solver.detectCycles();
  solver.snapshotGeometry();
  solver.relaxHard();
  solver.searchCores();
  solver.applySofts();
  solver.finalize( result );
  return result;
}

} // namespace sicnu::agent::cartography
