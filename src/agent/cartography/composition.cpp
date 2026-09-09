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

/// Outcome of one constraint application during a relaxation pass.
enum class Apply
{
    Satisfied, ///< already holds — nothing written
    Applied,   ///< geometry written this pass
    Failed     ///< cannot apply (reported once, constraint leaves the sweep)
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
    bool disabled = false;              ///< hard failure recorded — leaves the sweep
};

class Solver
{
  public:
    Solver( Json::Value &spec, double marginDefaultMm, Report &report )
      : mSpec( spec ), mMargin( marginDefaultMm ), mReport( report ) {}

    // --- bounded pipeline ---------------------------------------------------
    void normalize();     ///< item index in canonical collection order
    void buildRuntimes(); ///< declared constraints → normalized form (#781 aware)
    void detectCycles();  ///< leader→follower cycles, diagnosed not absorbed
    void relax();         ///< anchors + clamps + constraint sweeps to a fixpoint
    void finalize( CompositionResult &result );

  private:
    Json::Value *findItem( const std::string &id );
    bool resolveAnchors();
    bool clampSizes();
    Apply applyConstraint( ConstraintRuntime &c );

    Json::Value &mSpec;
    double mMargin = 0.0;
    Report &mReport;

    double mPageW = 297.0;
    double mPageH = 210.0;

    std::map<std::string, std::pair<std::string, int>> mById;

    std::vector<ConstraintRuntime> mConstraints;
    int mHardFailures = 0;
    int mPasses = 0;
    bool mConverged = true;
    std::set<std::string> mAnchorIds;
    std::set<std::string> mClampedIds;
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
}

void Solver::buildRuntimes()
{
  const Json::Value constraints = mSpec.get( "constraints", Json::Value( Json::arrayValue ) );
  if ( !constraints.isArray() )
    return;
  for ( const auto &constraint : constraints )
  {
    if ( !constraint.isObject() || !constraint.isMember( "kind" ) ||
         !constraint["kind"].isString() || !constraint.isMember( "items" ) ||
         !constraint["items"].isArray() )
      continue;
    const std::string kind = constraint["kind"].asString();
    if ( !mapspec::isConstraintKind( kind ) )
      continue; // legacy/free-form constraint items (e.g. frame_style) are not solver input
    const std::string cid = constraint.isMember( "id" ) && constraint["id"].isString()
                              ? constraint["id"].asString()
                              : kind;
    // Issue #781: arity must know the kind — fit_content is a legal
    // single-item constraint.
    const int requiredItems = kind == "fit_content" ? 1 : 2;

    ConstraintRuntime runtime;
    runtime.cid = cid;
    runtime.kind = kind;
    runtime.contentMm = constraint.get( "content_mm", Json::Value() );
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
      ++mHardFailures;
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
      mReport.add( c.cid + "|cycle",
                   c.cid + ": participates in a cyclic constraint dependency (" + nodes +
                     "); residual violations are reported, not absorbed" );
  }
}

bool Solver::resolveAnchors()
{
  bool wrote = false;
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
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
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
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

Apply Solver::applyConstraint( ConstraintRuntime &c )
{
  const std::string &kind = c.kind;
  const std::string &cid = c.cid;
  const std::string &edge = c.edge;
  const std::string &direction = c.direction;
  const double gap = c.gap;
  const auto rectOf = []( Json::Value *item, Rect &out ) { return toRect( *item, out ); };

  if ( kind == "align" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
    {
      mReport.add( cid + "|leader-rect", cid + ": leader item has no usable rect_mm" );
      return Apply::Failed;
    }
    if ( edge != "top" && edge != "bottom" && edge != "left" && edge != "right" )
    {
      mReport.add( cid + "|edge", cid + ": unknown align edge '" + edge + "'" );
      return Apply::Failed;
    }
    bool applied = false;
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
      {
        mReport.add( cid + "|follower-rect", cid + ": follower item has no usable rect_mm" );
        return Apply::Failed;
      }
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
      {
        writeRect( *c.items[i], target );
        applied = true;
      }
    }
    return applied ? Apply::Applied : Apply::Satisfied;
  }

  if ( kind == "match_width" || kind == "match_height" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
    {
      mReport.add( cid + "|leader-rect", cid + ": leader item has no usable rect_mm" );
      return Apply::Failed;
    }
    bool applied = false;
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
      {
        mReport.add( cid + "|follower-rect", cid + ": follower item has no usable rect_mm" );
        return Apply::Failed;
      }
      Rect target = rect;
      if ( kind == "match_width" )
        target.w = leader.w;
      else
        target.h = leader.h;
      if ( !sameRect( rect, target ) )
      {
        writeRect( *c.items[i], target );
        applied = true;
      }
    }
    return applied ? Apply::Applied : Apply::Satisfied;
  }

  if ( kind == "stack" )
  {
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
    {
      mReport.add( cid + "|leader-rect", cid + ": leader item has no usable rect_mm" );
      return Apply::Failed;
    }
    if ( direction != "below" && direction != "above" && direction != "left_of" &&
         direction != "right_of" )
    {
      mReport.add( cid + "|direction", cid + ": unknown stack direction '" + direction + "'" );
      return Apply::Failed;
    }
    bool applied = false;
    Rect previous = leader;
    for ( int i = 1; i < static_cast<int>( c.items.size() ); ++i )
    {
      Rect rect;
      if ( !rectOf( c.items[i], rect ) )
      {
        mReport.add( cid + "|follower-rect", cid + ": follower item has no usable rect_mm" );
        return Apply::Failed;
      }
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
      {
        writeRect( *c.items[i], target );
        applied = true;
      }
      previous = target;
    }
    return applied ? Apply::Applied : Apply::Satisfied;
  }

  if ( kind == "distribute" )
  {
    // Even spacing between the first and last item along the direction.
    Rect first;
    Rect last;
    if ( !rectOf( c.items.front(), first ) || !rectOf( c.items.back(), last ) )
    {
      mReport.add( cid + "|endpoints", cid + ": first/last item rect unusable" );
      return Apply::Failed;
    }
    if ( direction != "horizontal" && direction != "vertical" )
    {
      mReport.add( cid + "|direction", cid + ": unknown distribute direction '" + direction + "'" );
      return Apply::Failed;
    }
    const int count = static_cast<int>( c.items.size() );
    bool applied = false;
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
        {
          writeRect( *c.items[i], target );
          applied = true;
        }
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
        {
          writeRect( *c.items[i], target );
          applied = true;
        }
      }
    }
    return applied ? Apply::Applied : Apply::Satisfied;
  }

  if ( kind == "below" || kind == "above" || kind == "left_of" || kind == "right_of" )
  {
    // v3 relative placement: items = [target, follower] + optional gap_mm.
    Rect target;
    Rect follower;
    if ( !rectOf( c.items[0], target ) || !rectOf( c.items[1], follower ) )
    {
      mReport.add( cid + "|pair-rect", cid + ": target/follower rect unusable" );
      return Apply::Failed;
    }
    Rect result = follower;
    if ( kind == "below" )
    {
      result.x = target.x;
      result.y = target.y + target.h + gap;
    }
    else if ( kind == "above" )
    {
      result.x = target.x;
      result.y = target.y - gap - follower.h;
    }
    else if ( kind == "right_of" )
    {
      result.x = target.x + target.w + gap;
      result.y = target.y;
    }
    else // left_of
    {
      result.x = target.x - gap - follower.w;
      result.y = target.y;
    }
    if ( sameRect( follower, result ) )
      return Apply::Satisfied;
    writeRect( *c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "inside" )
  {
    // items = [container, content]: center the content inside the
    // container, clamped to stay within it.
    Rect container;
    Rect content;
    if ( !rectOf( c.items[0], container ) || !rectOf( c.items[1], content ) )
    {
      mReport.add( cid + "|pair-rect", cid + ": container/content rect unusable" );
      return Apply::Failed;
    }
    Rect result = content;
    result.x = container.x + ( container.w - content.w ) / 2.0;
    result.y = container.y + ( container.h - content.h ) / 2.0;
    result.x = std::max( container.x, std::min( result.x, container.x + container.w - content.w ) );
    result.y = std::max( container.y, std::min( result.y, container.y + container.h - content.h ) );
    if ( sameRect( content, result ) )
      return Apply::Satisfied;
    writeRect( *c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "keep_with" )
  {
    // items = [anchor, companion]: pin the companion directly below the
    // anchor with the gap, preserving its horizontal position.
    Rect anchor;
    Rect companion;
    if ( !rectOf( c.items[0], anchor ) || !rectOf( c.items[1], companion ) )
    {
      mReport.add( cid + "|pair-rect", cid + ": anchor/companion rect unusable" );
      return Apply::Failed;
    }
    Rect result = companion;
    result.y = anchor.y + anchor.h + gap;
    if ( sameRect( companion, result ) )
      return Apply::Satisfied;
    writeRect( *c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "avoid_overlap" )
  {
    // items = [keeper, mover]: when they intersect, the mover is pushed
    // below the keeper by the gap (deterministic resolution direction).
    Rect keeper;
    Rect mover;
    if ( !rectOf( c.items[0], keeper ) || !rectOf( c.items[1], mover ) )
    {
      mReport.add( cid + "|pair-rect", cid + ": keeper/mover rect unusable" );
      return Apply::Failed;
    }
    const bool overlaps = mover.x < keeper.x + keeper.w && keeper.x < mover.x + mover.w &&
                          mover.y < keeper.y + keeper.h && keeper.y < mover.y + mover.h;
    if ( !overlaps )
      return Apply::Satisfied;
    Rect result = mover;
    result.y = keeper.y + keeper.h + gap;
    if ( sameRect( mover, result ) )
      return Apply::Satisfied;
    writeRect( *c.items[1], result );
    return Apply::Applied;
  }

  if ( kind == "fit_content" )
  {
    // items = [item] with content_mm [w, h]: resize the item to its
    // declared content size clamped by min/max_size_mm. Single-item
    // relative constraint (target + declared content).
    Rect leader;
    if ( !rectOf( c.items[0], leader ) )
    {
      mReport.add( cid + "|rect", cid + ": fit_content needs one item with a rect" );
      return Apply::Failed;
    }
    const Json::Value &content = c.contentMm;
    if ( !content.isArray() || content.size() != 2 || !content[0].isNumeric() ||
         !content[1].isNumeric() )
    {
      mReport.add( cid + "|content", cid + ": fit_content needs content_mm" );
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
    writeRect( *c.items[0], rect );
    return Apply::Applied;
  }

  mReport.add( cid + "|kind", cid + ": unsupported constraint kind '" + kind + "'" );
  return Apply::Failed;
}

void Solver::relax()
{
  for ( mPasses = 1; mPasses <= kMaxRelaxationPasses; ++mPasses )
  {
    bool wrote = resolveAnchors();
    wrote = clampSizes() || wrote;
    bool anyApplied = false;
    for ( auto &c : mConstraints )
    {
      if ( c.disabled )
        continue;
      const Apply outcome = applyConstraint( c );
      if ( outcome == Apply::Applied )
        anyApplied = true;
      else if ( outcome == Apply::Failed )
        c.disabled = true; // reported once; leaves the sweep
    }
    if ( !anyApplied && !wrote )
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
    if ( !c.disabled )
      ids += ( ids.empty() ? "" : ", " ) + c.cid;
  mReport.add( "|non-convergence",
               "constraint relaxation did not converge within " +
                 std::to_string( kMaxRelaxationPasses ) + " passes; still active: " +
                 ( ids.empty() ? std::string( "(none)" ) : ids ) );
}

void Solver::finalize( CompositionResult &result )
{
  for ( const auto &note : mReport.notes() )
    result.unsatisfied.push_back( note );
  result.constraintsTotal = static_cast<int>( mConstraints.size() ) + mHardFailures;
  // Only constraints that are active AND settled count as solved: a
  // hard-failed (disabled) constraint never solved, and a non-converged
  // layout cannot claim any solver constraint as satisfied.
  int active = 0;
  for ( const auto &c : mConstraints )
    if ( !c.disabled )
      ++active;
  result.constraintsSolved = mConverged ? active : 0;
  result.passes = mPasses;
  result.converged = mConverged;
  result.anchorsResolved = static_cast<int>( mAnchorIds.size() );
  result.sizesClamped = static_cast<int>( mClampedIds.size() );
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
  solver.detectCycles();
  solver.relax();
  solver.finalize( result );
  return result;
}

} // namespace sicnu::agent::cartography
