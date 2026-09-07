// src/agent/cartography/composition.cpp
#include "composition.h"

#include "../mapspec/mapspec.h"

#include <algorithm>
#include <cmath>

namespace sicnu::agent::cartography {

namespace {

struct Rect
{
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

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
bool anchorPoint( const std::string &edge, double marginMm, double pageW, double pageH,
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
  return true;
}

} // namespace

Json::Value CompositionResult::toJson() const
{
  Json::Value out( Json::objectValue );
  out["anchors_resolved"] = anchorsResolved;
  out["sizes_clamped"] = sizesClamped;
  out["constraints_solved"] = constraintsSolved;
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

  // --- 1. anchors ------------------------------------------------------------
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < spec[collection].size(); ++i )
    {
      Json::Value &item = spec[collection][i];
      if ( !item.isObject() || !item.isMember( "anchor" ) || !item["anchor"].isObject() )
        continue;
      const Json::Value &anchor = item["anchor"];
      if ( !anchor.isMember( "edge" ) || !anchor["edge"].isString() )
        continue;
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
          result.unsatisfied.push_back(
            std::string( item["id"].asString() ) + ": anchor needs rect_mm or min_size_mm" );
          continue;
        }
      }
      const std::string edge = anchor["edge"].asString();
      if ( !mapspec::isAnchorEdge( edge ) )
      {
        result.unsatisfied.push_back( std::string( item["id"].asString() ) +
                                      ": unknown anchor edge '" + edge + "'" );
        continue;
      }
      double x = 0.0;
      double y = 0.0;
      anchorPoint( edge, anchorMargin( anchor, marginDefaultMm ), pageW, pageH, rect.w, rect.h, x,
                   y );
      if ( x < 0 || y < 0 || x + rect.w > pageW || y + rect.h > pageH )
      {
        // Reported, never written: writing an off-page rect would make the
        // repair loop clamp and re-pin forever.
        result.unsatisfied.push_back( std::string( item["id"].asString() ) +
                                      ": anchor '" + edge + "' cannot place the item inside "
                                      "the page (size " + std::to_string( rect.w ) + "×" +
                                      std::to_string( rect.h ) + " mm)" );
        continue;
      }
      rect.x = x;
      rect.y = y;
      writeRect( item, rect );
      ++result.anchorsResolved;
    }
  }

  // --- 2. size clamps ---------------------------------------------------------
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < spec[collection].size(); ++i )
    {
      Json::Value &item = spec[collection][i];
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
      if ( std::fabs( clamped.w - rect.w ) > 1e-9 || std::fabs( clamped.h - rect.h ) > 1e-9 )
      {
        // Keep the origin: clamping grows/shrinks from the top-left corner.
        // Bottom/right-anchored items should declare the anchor instead of
        // relying on clamp edge behavior.
        clamped.x = rect.x;
        clamped.y = rect.y;
        writeRect( item, clamped );
        ++result.sizesClamped;
      }
    }
  }

  // --- 3. constraints (declared order, one deterministic pass each) -----------
  const Json::Value constraints = spec.get( "constraints", Json::Value( Json::arrayValue ) );
  if ( constraints.isArray() )
  {
    for ( const auto &constraint : constraints )
    {
      if ( !constraint.isObject() || !constraint.isMember( "kind" ) ||
           !constraint["kind"].isString() || !constraint.isMember( "items" ) ||
           !constraint["items"].isArray() || constraint["items"].size() < 2 )
        continue;
      const std::string kind = constraint["kind"].asString();
      if ( !mapspec::isConstraintKind( kind ) )
        continue; // legacy/free-form constraint items (e.g. frame_style) are not solver input
      const std::string cid = constraint.isMember( "id" ) && constraint["id"].isString()
                                ? constraint["id"].asString()
                                : kind;

      // Resolve referenced items (skip unresolvable ones — validation flags).
      std::vector<Json::Value *> items;
      for ( const auto &reference : constraint["items"] )
      {
        if ( !reference.isString() )
          continue;
        Json::Value location = mapspec::findMapSpecItem( spec, reference.asString() );
        if ( location.isNull() )
          continue;
        items.push_back(
          &spec[location["collection"].asString()][location["index"].asInt()] );
      }
      if ( items.size() < 2 )
      {
        result.unsatisfied.push_back( cid + ": fewer than two resolvable items" );
        continue;
      }

      Rect leader;
      if ( !toRect( *items[0], leader ) )
      {
        result.unsatisfied.push_back( cid + ": leader item has no usable rect_mm" );
        continue;
      }
      double gap = marginDefaultMm;
      if ( constraint.isMember( "gap_mm" ) && constraint["gap_mm"].isNumeric() )
        gap = constraint["gap_mm"].asDouble();
      const std::string direction = constraint.get( "direction", "" ).asString();
      const std::string edge = constraint.get( "edge", "" ).asString();
      bool solved = true;

      if ( kind == "align" )
      {
        for ( int i = 1; i < static_cast<int>( items.size() ); ++i )
        {
          Rect rect;
          if ( !toRect( *items[i], rect ) )
          {
            solved = false;
            continue;
          }
          if ( edge == "top" )
            rect.y = leader.y;
          else if ( edge == "bottom" )
            rect.y = leader.y + leader.h - rect.h;
          else if ( edge == "left" )
            rect.x = leader.x;
          else if ( edge == "right" )
            rect.x = leader.x + leader.w - rect.w;
          else
          {
            result.unsatisfied.push_back( cid + ": unknown align edge '" + edge + "'" );
            solved = false;
            break;
          }
          writeRect( *items[i], rect );
        }
      }
      else if ( kind == "match_width" || kind == "match_height" )
      {
        for ( int i = 1; i < static_cast<int>( items.size() ); ++i )
        {
          Rect rect;
          if ( !toRect( *items[i], rect ) )
          {
            solved = false;
            continue;
          }
          if ( kind == "match_width" )
            rect.w = leader.w;
          else
            rect.h = leader.h;
          writeRect( *items[i], rect );
        }
      }
      else if ( kind == "stack" )
      {
        Rect previous = leader;
        for ( int i = 1; i < static_cast<int>( items.size() ); ++i )
        {
          Rect rect;
          if ( !toRect( *items[i], rect ) )
          {
            solved = false;
            continue;
          }
          if ( direction == "below" )
          {
            rect.y = previous.y + previous.h + gap;
            rect.x = leader.x;
          }
          else if ( direction == "above" )
          {
            rect.y = previous.y - gap - rect.h;
            rect.x = leader.x;
          }
          else if ( direction == "right_of" )
          {
            rect.x = previous.x + previous.w + gap;
            rect.y = leader.y;
          }
          else if ( direction == "left_of" )
          {
            rect.x = previous.x - gap - rect.w;
            rect.y = leader.y;
          }
          else
          {
            result.unsatisfied.push_back( cid + ": unknown stack direction '" + direction +
                                          "'" );
            solved = false;
            break;
          }
          writeRect( *items[i], rect );
          previous = rect;
        }
      }
      else if ( kind == "distribute" )
      {
        // Even spacing between the first and last item along the direction.
        Rect first;
        Rect last;
        if ( !toRect( *items.front(), first ) || !toRect( *items.back(), last ) )
        {
          result.unsatisfied.push_back( cid + ": first/last item rect unusable" );
          continue;
        }
        const int count = static_cast<int>( items.size() );
        if ( direction == "horizontal" )
        {
          const double span = ( last.x - ( first.x + first.w ) ) / std::max( 1, count - 1 );
          for ( int i = 1; i < count - 1; ++i )
          {
            Rect rect;
            if ( !toRect( *items[i], rect ) )
              continue;
            rect.x = first.x + first.w + span * i;
            writeRect( *items[i], rect );
          }
        }
        else if ( direction == "vertical" )
        {
          const double span = ( last.y - ( first.y + first.h ) ) / std::max( 1, count - 1 );
          for ( int i = 1; i < count - 1; ++i )
          {
            Rect rect;
            if ( !toRect( *items[i], rect ) )
              continue;
            rect.y = first.y + first.h + span * i;
            writeRect( *items[i], rect );
          }
        }
        else
        {
          result.unsatisfied.push_back( cid + ": unknown distribute direction '" + direction +
                                        "'" );
          solved = false;
        }
      }
      else
      {
        result.unsatisfied.push_back( cid + ": unsupported constraint kind '" + kind + "'" );
        solved = false;
      }
      if ( solved )
        ++result.constraintsSolved;
    }
  }

  return result;
}

} // namespace sicnu::agent::cartography
