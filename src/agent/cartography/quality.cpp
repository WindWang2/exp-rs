// src/agent/cartography/quality.cpp
#include "quality.h"

#include "../contracts/spatial_contracts.h"
#include "../mapspec/mapspec.h"
#include "chart_registry.h"
#include "composition.h"
#include "design_tokens.h"
#include "registry.h"
#include "style_spec.h"
#include "typography.h"

#include <QCryptographicHash>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace sicnu::agent::cartography {

using namespace sicnu::agent::contracts;

namespace {

Json::Value rect( double x, double y, double w, double h )
{
  Json::Value r( Json::arrayValue );
  r.append( x );
  r.append( y );
  r.append( w );
  r.append( h );
  return r;
}

bool rectsIntersect( const Json::Value &a, const Json::Value &b )
{
  if ( !a.isArray() || !b.isArray() || a.size() != 4 || b.size() != 4 )
    return false;
  const double ax = a[0].asDouble();
  const double ay = a[1].asDouble();
  const double aw = a[2].asDouble();
  const double ah = a[3].asDouble();
  const double bx = b[0].asDouble();
  const double by = b[1].asDouble();
  const double bw = b[2].asDouble();
  const double bh = b[3].asDouble();
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/// First map frame id (or empty).
std::string mainMapRef( const Json::Value &spec )
{
  if ( spec.isMember( "map_frames" ) && spec["map_frames"].isArray() &&
       !spec["map_frames"].empty() && spec["map_frames"][0].isMember( "id" ) )
    return spec["map_frames"][0]["id"].asString();
  return std::string();
}

Json::Value issue( const std::string &code, const std::string &severity, const std::string &message,
                   bool repairable, const std::string &itemId, const char *action )
{
  Json::Value suggestion = action ? makeRepairSuggestion( action, Json::Value() ) : Json::Value();
  return makeIssue( code, severity, message, repairable, itemId, suggestion );
}

/// Declared page margin (page.margin_mm); 0 when undeclared.
double declaredMargin( const Json::Value &spec )
{
  if ( spec.isMember( "page" ) && spec["page"].isObject() &&
       spec["page"].isMember( "margin_mm" ) && spec["page"]["margin_mm"].isNumeric() )
    return std::max( 0.0, spec["page"]["margin_mm"].asDouble() );
  return 0.0;
}

/// Deterministic overflow verdict: estimated single-line text width vs the
/// item rect (5% tolerance). Returns 0 when clean, else the needed width.
double overflowAmountMm( const Json::Value &item, const char *collection )
{
  const std::string collectionName( collection );
  if ( collectionName != "titles" && collectionName != "labels" &&
       collectionName != "source_notes" && collectionName != "annotations" )
    return 0.0;
  if ( !item.isMember( "text" ) || !item["text"].isString() || !item.isMember( "rect_mm" ) ||
       !item["rect_mm"].isArray() || item["rect_mm"].size() != 4 )
    return 0.0;
  // Non-positive declared sizes are MAP_INVALID_RECT's business, not ours.
  if ( item["rect_mm"][2].asDouble() <= 0 )
    return 0.0;
  double sizePt = 9.0;
  if ( item.isMember( "font" ) && item["font"].isObject() && item["font"].isMember( "size_pt" ) &&
       item["font"]["size_pt"].isNumeric() )
    sizePt = item["font"]["size_pt"].asDouble();
  const double needed = estimateTextWidthMm( item["text"].asString(), sizePt );
  const double available = item["rect_mm"][2].asDouble();
  return needed > available * 1.05 ? needed - available : 0.0;
}

struct ItemRef
{
    const char *collection;
    Json::Value *item;
};

/// Mutable item lookup across collections.
ItemRef findItemMutable( Json::Value &spec, const std::string &id )
{
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < spec[collection].size(); ++i )
    {
      Json::Value &item = spec[collection][i];
      if ( item.isObject() && item.isMember( "id" ) && item["id"].asString() == id )
        return { collection, &item };
    }
  }
  return { nullptr, nullptr };
}

/// Widen the text rect to fit (typography wrap model), shift left-edge if
/// needed, then grow the height for the wrapped lines; shrink the font as
/// the last resort (floor 12pt titles / 8pt others). Uses the SAME
/// measurement model as the preflight rules (fitTextIntoBox), so a repaired
/// item provably clears MAP_TEXT_OVERFLOW / MAP_TEXT_WRAP_OVERFLOW on
/// re-preflight — the repair converges. Deterministic.
bool repairTextOverflow( Json::Value &item, double pageW, double pageH,
                         const std::string &collection )
{
  if ( !item.isMember( "text" ) || !item["text"].isString() || !item.isMember( "rect_mm" ) ||
       item["rect_mm"].size() != 4 )
    return false;
  double sizePt = item.isMember( "font" ) && item["font"].isObject() &&
                          item["font"].isMember( "size_pt" ) && item["font"]["size_pt"].isNumeric()
                    ? item["font"]["size_pt"].asDouble()
                    : 9.0;
  if ( sizePt <= 0 )
    return false; // MAP_TINY_FONT / content errors own this case
  const double margin = 12.0;
  const double maxW = std::max( 10.0, pageW - 2 * margin );

  // Widen to the widest wrapped line at the declared font (bounded by the
  // margin box), then give the wrapped lines the height they need (bounded
  // by the page).
  TextFitRequest request;
  request.text = item["text"].asString();
  request.boxWidthMm = maxW;
  request.boxHeightMm = std::max( 1.0, pageH );
  request.fontPt = sizePt;
  request.policy = "overflow_report";
  TextFitReport report = fitTextIntoBox( request );
  double neededW = std::max( report.usedWidthMm, 1.0 );
  double neededH = std::max( report.usedHeightMm, 1.0 );
  if ( neededW <= maxW + 1e-9 && neededH <= ( pageH - 2 * margin ) + 1e-9 )
  {
    double x = item["rect_mm"][0].asDouble();
    const double y = item["rect_mm"][1].asDouble();
    if ( x + neededW > pageW - margin )
      x = std::max( margin, pageW - margin - neededW );
    item["rect_mm"] = rect( x, y, neededW, neededH );
    return true;
  }
  // Not representable at the declared font within the page: shrink to fit
  // the ORIGINAL box (declared font floor respected by shrink_to_fit).
  const double floorPt = collection == "titles" ? 12.0 : 8.0;
  TextFitRequest shrink;
  shrink.text = item["text"].asString();
  shrink.boxWidthMm = std::max( 1.0, item["rect_mm"][2].asDouble() );
  shrink.boxHeightMm = std::max( 1.0, item["rect_mm"][3].asDouble() );
  shrink.fontPt = sizePt;
  shrink.fontPtMin = floorPt;
  shrink.policy = "shrink_to_fit";
  TextFitReport shrunk = fitTextIntoBox( shrink );
  if ( item.isMember( "font" ) && item["font"].isObject() )
    item["font"]["size_pt"] = shrunk.fontPt;
  else
  {
    Json::Value font( Json::objectValue );
    font["size_pt"] = shrunk.fontPt;
    item["font"] = font;
  }
  return true;
}

} // namespace

double estimateTextWidthMm( const std::string &text, double sizePt )
{
  const double kPtToMm = 0.352778;
  double maxWidthEm = 0.0;
  double lineEm = 0.0;
  for ( const unsigned char byte : text )
  {
    if ( byte == '\n' )
    {
      maxWidthEm = std::max( maxWidthEm, lineEm );
      lineEm = 0.0;
      continue;
    }
    if ( ( byte & 0xC0 ) == 0x80 )
      continue; // UTF-8 continuation byte: absorbed into its lead character
    if ( byte >= 0xE0 )
      lineEm += 1.0; // three-byte lead: CJK/fullwidth ranges count one em
    else if ( byte == ' ' )
      lineEm += 0.35;
    else
      lineEm += 0.55; // Latin/digits/punctuation (two-byte leads included)
  }
  maxWidthEm = std::max( maxWidthEm, lineEm );
  return maxWidthEm * sizePt * kPtToMm;
}

Json::Value preflightMapSpec( const Json::Value &specIn, const Json::Value &compiledReport )
{
  std::vector<Json::Value> issues;

  // All rules evaluate the *resolved* composition: anchors, size bounds and
  // constraints are solved on a local copy first, so standalone preflight
  // calls see exactly what the compile-time solver would produce. Content
  // fields are identical; only geometry can differ.
  Json::Value spec = specIn;
  const Json::Value tokens = resolveTokenSet( spec );
  const CompositionResult solvedResult = resolveComposition(
    spec, tokenNumber( tokens, "spacing.margin_mm", 12.0 ) );

  const auto specProblems = mapspec::validateMapSpec( spec );
  if ( !specProblems.empty() )
  {
    for ( const auto &problem : specProblems )
      issues.push_back( issue( "MAPSPEC_INVALID", "error", problem, false, "", nullptr ) );
    Json::Value issuesArr( Json::arrayValue );
    for ( const auto &i : issues )
      issuesArr.append( i );
    Json::Value body( Json::objectValue );
    body["quality_score"] = 0;
    body["passed"] = false;
    body["issues"] = issuesArr;
    body["checks"] = Json::Value( Json::arrayValue );
    return makeEnvelope( "map_quality_report", body );
  }

  const double pageW = spec["page"]["width_mm"].asDouble();
  const double pageH = spec["page"]["height_mm"].asDouble();
  const std::string mapRef = mainMapRef( spec );
  const double margin = declaredMargin( spec );

  const auto hasNonEmpty = [ &spec ]( const char *collection ) {
    return spec.isMember( collection ) && spec[collection].isArray() && !spec[collection].empty();
  };

  // --- map frame presence & content ----------------------------------------
  if ( !hasNonEmpty( "map_frames" ) )
  {
    issues.push_back( issue( "MAP_MISSING_MAP", "error", "No map frame in the MapSpec.", false, "",
                             nullptr ) );
  }
  else
  {
    for ( const auto &frame : spec["map_frames"] )
    {
      const std::string id = frame["id"].asString();
      const bool hasLayers = frame.isMember( "layers" ) && frame["layers"].isArray() &&
                             !frame["layers"].empty();
      const bool hasExtent = frame.isMember( "extent" ) && frame["extent"].isArray() &&
                             frame["extent"].size() == 4;
      if ( !hasLayers && !hasExtent )
      {
        issues.push_back( issue(
          "MAP_EMPTY_MAP", "warning",
          "Map frame '" + id + "' has neither layers nor extent — it will render blank.", false,
          id, nullptr ) );
      }
    }
    // --- multi-map frame balance ------------------------------------------
    if ( spec["map_frames"].size() >= 2 )
    {
      double firstH = 0.0;
      for ( const auto &frame : spec["map_frames"] )
      {
        if ( !frame.isObject() || !frame.isMember( "rect_mm" ) || frame["rect_mm"].size() != 4 )
          continue;
        const double h = frame["rect_mm"][3].asDouble();
        if ( firstH <= 0 )
        {
          firstH = h;
          continue;
        }
        if ( std::fabs( h - firstH ) > 0.15 * std::max( firstH, h ) )
        {
          issues.push_back( issue( "MAP_UNBALANCED_FRAMES", "warning",
                                   "Map frame '" + frame["id"].asString() +
                                     "' height deviates >15% from the first frame",
                                   true, frame["id"].asString(), "balance_frames" ) );
        }
      }
    }
  }

  // --- cartographic furniture -----------------------------------------------
  if ( !hasNonEmpty( "titles" ) )
    issues.push_back( issue( "MAP_MISSING_TITLE", "warning", "No title item.", true, "",
                             "add_title" ) );
  if ( !hasNonEmpty( "legends" ) )
    issues.push_back( issue( "MAP_MISSING_LEGEND", "warning", "No legend item.", true, "",
                             "add_legend" ) );
  if ( !hasNonEmpty( "scale_bars" ) )
    issues.push_back( issue( "MAP_MISSING_SCALE_BAR", "warning", "No scale bar.", true, "",
                             "add_scale_bar" ) );
  if ( !hasNonEmpty( "north_arrows" ) )
    issues.push_back( issue( "MAP_MISSING_NORTH_ARROW", "warning", "No north arrow.", true, "",
                             "add_north_arrow" ) );
  if ( !hasNonEmpty( "source_notes" ) )
    issues.push_back( issue( "MAP_MISSING_SOURCE_NOTE", "warning", "No data-source note.", true,
                             "", "add_source_note" ) );

  // --- Platform 7.0: declarative layer visibility & reference checks --------
  {
    std::set<std::string> referencedLayers;
    for ( const char *collection : { "map_frames", "inset_maps" } )
      for ( const auto &frame : spec.isMember( collection ) && spec[collection].isArray()
                                  ? spec[collection]
                                  : Json::Value( Json::arrayValue ) )
        if ( frame.isObject() && frame.isMember( "layers" ) && frame["layers"].isArray() )
          for ( const auto &ref : frame["layers"] )
            if ( ref.isString() )
              referencedLayers.insert( ref.asString() );
    for ( const auto &layer : spec.isMember( "layers" ) && spec["layers"].isArray()
                                ? spec["layers"]
                                : Json::Value( Json::arrayValue ) )
    {
      if ( !layer.isObject() || !layer.isMember( "id" ) || !layer["id"].isString() )
        continue;
      const std::string layerId = layer["id"].asString();
      const bool declaredInvisible =
        ( layer.isMember( "visible" ) && layer["visible"].isBool() && !layer["visible"].asBool() ) ||
        ( layer.isMember( "opacity" ) && layer["opacity"].isNumeric() &&
          layer["opacity"].asDouble() <= 0.0 );
      if ( declaredInvisible )
        issues.push_back( issue( "MAP_INVISIBLE_LAYER", "warning",
                                 layerId + ": declared invisible or fully transparent; the "
                                           "compiled map will not show it",
                                 false, layerId, nullptr ) );
      // A layer referenced by id but absent from the declarative layers
      // collection may still resolve against the workspace — that path is
      // the compile's business. The spec-level gap is the opposite: a
      // declarative layer nothing references will never render anywhere.
      if ( !referencedLayers.count( layerId ) )
        issues.push_back( issue( "MAP_LAYER_UNREFERENCED", "warning",
                                 layerId + ": declared in layers[] but referenced by no "
                                           "map frame or inset; it will not render",
                                 true, layerId, "reference_layer" ) );
    }
  }

  // --- Platform 7.0: legend/style class mismatch ----------------------------
  if ( spec.isMember( "legends" ) && spec["legends"].isArray() )
    for ( const auto &legend : spec["legends"] )
    {
      if ( !legend.isObject() || !legend.isMember( "id" ) )
        continue;
      const std::string id = legend["id"].asString();
      if ( !legend.isMember( "classes" ) || !legend["classes"].isArray() ||
           legend["classes"].empty() || !legend.isMember( "style_ref" ) ||
           !legend["style_ref"].isString() )
        continue;
      const Json::Value style = StyleRegistry::instance().find(
        QString::fromStdString( legend["style_ref"].asString() ) );
      if ( style.isNull() || !style.isObject() )
        continue; // unknown style refs are MAP_STYLE_REF_UNKNOWN's business
      std::vector<std::string> styleLabels;
      if ( style.isMember( "raster" ) && style["raster"].isObject() &&
           style["raster"].isMember( "classification" ) &&
           style["raster"]["classification"].isObject() &&
           style["raster"]["classification"].isMember( "classes" ) &&
           style["raster"]["classification"]["classes"].isArray() )
        for ( const auto &entry : style["raster"]["classification"]["classes"] )
          if ( entry.isObject() && entry.isMember( "label" ) && entry["label"].isString() )
            styleLabels.push_back( entry["label"].asString() );
      if ( style.isMember( "vector" ) && style["vector"].isObject() &&
           style["vector"].isMember( "categories" ) && style["vector"]["categories"].isArray() )
        for ( const auto &entry : style["vector"]["categories"] )
          if ( entry.isObject() && entry.isMember( "label" ) && entry["label"].isString() )
            styleLabels.push_back( entry["label"].asString() );
      if ( styleLabels.empty() )
        continue;
      std::set<std::string> styleSet( styleLabels.begin(), styleLabels.end() );
      int missing = 0;
      std::string firstMissing;
      for ( const auto &entry : legend["classes"] )
      {
        const std::string label = entry.isString() ? entry.asString()
                                  : entry.isObject() && entry.isMember( "label" ) &&
                                        entry["label"].isString()
                                      ? entry["label"].asString()
                                      : std::string();
        if ( !label.empty() && !styleSet.count( label ) )
        {
          ++missing;
          if ( firstMissing.empty() )
            firstMissing = label;
        }
      }
      if ( missing > 0 )
        issues.push_back( issue(
          "MAP_LEGEND_MISMATCH", "warning",
          id + ": " + std::to_string( missing ) + " legend class(es) (first: '" + firstMissing +
            "') do not appear in the referenced style '" + legend["style_ref"].asString() + "'",
          false, id, nullptr ) );
    }

  // --- Platform 8.0: NoData legend coverage ---------------------------------
  // A legend referencing a style that declares raster.nodata must carry a
  // nodata mention (legend.nodata declaration), otherwise the rendered
  // legend hides the declared NoData class from the reader.
  if ( spec.isMember( "legends" ) && spec["legends"].isArray() )
    for ( const auto &legend : spec["legends"] )
    {
      if ( !legend.isObject() || !legend.isMember( "id" ) || !legend.isMember( "style_ref" ) ||
           !legend["style_ref"].isString() )
        continue;
      // Declared AND well-formed counts as covered; a malformed (non-object)
      // declaration compiles nothing, so the rule must keep firing.
      if ( legend.isMember( "nodata" ) && legend["nodata"].isObject() )
        continue;
      const Json::Value style = StyleRegistry::instance().find(
        QString::fromStdString( legend["style_ref"].asString() ) );
      if ( style.isNull() || !style.isObject() )
        continue; // unknown style refs are MAP_STYLE_REF_UNKNOWN's business
      if ( !( style.isMember( "raster" ) && style["raster"].isObject() &&
              style["raster"].isMember( "nodata" ) && style["raster"]["nodata"].isObject() ) )
        continue;
      issues.push_back( issue(
        "MAP_NODATA_LEGEND", "warning",
        legend["id"].asString() + ": referenced style '" + legend["style_ref"].asString() +
          "' declares raster.nodata but the legend does not — the NoData class is "
          "invisible to the reader",
        true, legend["id"].asString(), "declare_nodata" ) );
    }

  // --- per-item geometry, style, text and bindings --------------------------
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || !item.isMember( "id" ) )
        continue;
      const std::string id = item["id"].asString();
      const std::string collectionName( collection );
      if ( item.isMember( "rect_mm" ) && item["rect_mm"].isArray() && item["rect_mm"].size() == 4 )
      {
        const Json::Value &r = item["rect_mm"];
        const double x = r[0].asDouble();
        const double y = r[1].asDouble();
        const double w = r[2].asDouble();
        const double h = r[3].asDouble();
        if ( w <= 0 || h <= 0 )
          issues.push_back( issue( "MAP_INVALID_RECT", "error",
                                   id + ": rect width/height must be positive", false, id,
                                   nullptr ) );
        else if ( x < -0.5 || y < -0.5 || x + w > pageW + 0.5 || y + h > pageH + 0.5 )
          issues.push_back( issue( "MAP_OFF_PAGE", "error",
                                   id + ": rect exceeds the page bounds", true, id,
                                   "move_in_page" ) );
        else if ( margin > 0 && collectionName != "map_frames" && collectionName != "inset_maps" &&
                  ( x < margin - 0.5 || y < margin - 0.5 || x + w > pageW - margin + 0.5 ||
                    y + h > pageH - margin + 0.5 ) )
          issues.push_back( issue( "MAP_MARGIN_VIOLATION", "warning",
                                   id + ": item violates the declared page margin "
                                        "(page.margin_mm)",
                                   true, id, "move_in_margins" ) );
      }
      if ( ( collectionName == "titles" || collectionName == "labels" ||
             collectionName == "source_notes" ) &&
           item.isMember( "font" ) && item["font"].isObject() &&
           item["font"].isMember( "size_pt" ) && item["font"]["size_pt"].isNumeric() &&
           item["font"]["size_pt"].asDouble() < 6.0 )
        issues.push_back( issue( "MAP_TINY_FONT", "warning",
                                 id + ": font below 6 pt is unreadable at export size", true, id,
                                 "bump_font" ) );

      // --- text overflow (title / source-note clipping / labels) -----------
      const double overflow = overflowAmountMm( item, collection );
      if ( overflow > 0 )
      {
        const char *code = collectionName == "titles"        ? "MAP_TITLE_OVERFLOW"
                           : collectionName == "source_notes" ? "MAP_SOURCE_NOTE_CLIPPING"
                                                              : "MAP_TEXT_OVERFLOW";
        issues.push_back( issue( code, "warning",
                                 id + ": text likely overflows its rect by " +
                                   std::to_string( static_cast<int>( std::ceil( overflow ) ) ) +
                                   " mm",
                                 true, id, "widen_or_shrink" ) );
      }

      // Platform 7.0 wrap-aware check: the single-line estimator above only
      // sees the widest hard line. fitTextIntoBox additionally simulates
      // word wrap (CJK kinsoku included) and the line budget, so
      // multi-line clipping surfaces here.
      if ( ( collectionName == "titles" || collectionName == "labels" ||
             collectionName == "source_notes" ) &&
           item.isMember( "text" ) && item["text"].isString() &&
           item.isMember( "rect_mm" ) && item["rect_mm"].size() == 4 )
      {
        sicnu::agent::cartography::TextFitRequest fitRequest;
        fitRequest.text = item["text"].asString();
        fitRequest.boxWidthMm = item["rect_mm"][2].asDouble();
        fitRequest.boxHeightMm = item["rect_mm"][3].asDouble();
        if ( item.isMember( "font" ) && item["font"].isObject() &&
             item["font"].isMember( "size_pt" ) && item["font"]["size_pt"].isNumeric() )
          fitRequest.fontPt = item["font"]["size_pt"].asDouble();
        // Platform 8.0: declared line-end composition + line height ride on
        // the font block; the same model measures the item and the repair.
        if ( item.isMember( "font" ) && item["font"].isObject() &&
             item["font"].isMember( "break_policy" ) && item["font"]["break_policy"].isString() )
          fitRequest.breakPolicy = item["font"]["break_policy"].asString();
        if ( item.isMember( "font" ) && item["font"].isObject() &&
             item["font"].isMember( "line_height" ) && item["font"]["line_height"].isNumeric() )
          fitRequest.lineHeightFactor = item["font"]["line_height"].asDouble();
        fitRequest.policy = "overflow_report";
        const TextFitReport fit = fitTextIntoBox( fitRequest );
        if ( !fit.fits )
          issues.push_back( issue( "MAP_TEXT_WRAP_OVERFLOW", "warning",
                                   id + ": wrapped text does not fit the rect (" +
                                     std::to_string( static_cast<int>( fit.lines.size() ) ) +
                                     " line(s), overflow " +
                                     std::to_string( static_cast<int>( std::ceil(
                                       std::max( fit.overflowWidthMm,
                                                 fit.overflowHeightMm ) ) ) ) +
                                     " mm at " + std::to_string( fit.fontPt ) + " pt)",
                                   true, id, "widen_or_shrink" ) );
      }

      // --- legend density (declared max_entries) ---------------------------
      if ( collectionName == "legends" && item.isMember( "max_entries" ) &&
           item["max_entries"].isIntegral() && item.isMember( "rect_mm" ) &&
           item["rect_mm"].size() == 4 )
      {
        const int entries = item["max_entries"].asInt();
        const double lineH = tokenNumber( tokens, "furniture.legend_line_height_mm", 4.5 );
        const double requiredH = 8.0 + entries * lineH;
        if ( item["rect_mm"][3].asDouble() + 0.5 < requiredH )
          issues.push_back( issue( "MAP_LEGEND_DENSITY", "warning",
                                   id + ": legend may need ~" +
                                     std::to_string( static_cast<int>( std::ceil( requiredH ) ) ) +
                                     " mm for " + std::to_string( entries ) + " entries",
                                   true, id, "grow_legend" ) );
      }

      // --- chart bindings ---------------------------------------------------
      if ( collectionName == "charts" )
      {
        bool validBinding = item.isMember( "chart" ) && item["chart"].isObject() &&
                            item["chart"].isMember( "binding" );
        if ( validBinding )
        {
          const Json::Value &chart = item["chart"];
          const std::string mode = chart["binding"].isMember( "mode" ) &&
                                             chart["binding"]["mode"].isString()
                                       ? chart["binding"]["mode"].asString()
                                       : "inline";
          if ( mode == "inline" && item["chart"].isMember( "chart_id" ) )
          {
            // Legacy style: a chart entity reference must resolve.
            validBinding = !ChartRegistry::instance()
                              .find( QString::fromStdString(
                                chart["chart_id"].asString() ) )
                              .isNull();
          }
          if ( mode == "vector_expression" )
            validBinding = chart["binding"].isMember( "layer" ) &&
                           chart["binding"]["layer"].isString();
        }
        if ( !validBinding )
          issues.push_back( issue( "MAP_INVALID_BINDING", "warning",
                                   id + ": chart binding does not resolve (inline data or a "
                                        "layer reference required)",
                                   false, id, nullptr ) );

        // --- table-family overflow estimate (Platform 5.0): rows that cannot
        // fit the declared rect height would clip at render time.
        const Json::Value &chartSpec = item.isMember( "chart" ) ? item["chart"] : Json::Value();
        if ( validBinding && chartSpec.isObject() && item.isMember( "rect_mm" ) &&
             item["rect_mm"].size() == 4 )
        {
          const std::string chartKind = chartSpec.isMember( "kind" ) && chartSpec["kind"].isString()
                                          ? chartSpec["kind"].asString()
                                          : "";
          const bool isTable = chartKind == "table" || chartKind == "summary_table" ||
                               chartKind == "topn_table";
          if ( isTable && chartSpec["binding"].isMember( "data" ) &&
               chartSpec["binding"]["data"].isArray() )
          {
            const int rows = static_cast<int>( chartSpec["binding"]["data"].size() );
            const double fontPt =
              chartSpec.isMember( "style" ) && chartSpec["style"].isMember( "font_pt" ) &&
                  chartSpec["style"]["font_pt"].isNumeric()
                ? chartSpec["style"]["font_pt"].asDouble()
                : 10.0;
            const double rowMm = fontPt * 0.352778 * 1.5; // leading factor
            const double requiredH = 12.0 + rows * rowMm;
            const double rectH = item["rect_mm"][3].asDouble();
            if ( requiredH > rectH + 0.5 )
              issues.push_back( issue(
                "MAP_CHART_OVERFLOW", "warning",
                id + ": " + std::to_string( rows ) + " table rows need ~" +
                  std::to_string( static_cast<int>( std::ceil( requiredH ) ) ) + " mm but the "
                  "rect is " + std::to_string( static_cast<int>( rectH ) ) + " mm",
                true, id, "grow_chart" ) );
          }
        }
      }

      // --- component references --------------------------------------------
      if ( item.isMember( "source_component" ) )
      {
        Json::Value probe = item;
        QString error;
        if ( !applyComponentDefaults( probe, &error ) )
          issues.push_back( issue( "MAP_UNKNOWN_COMPONENT", "warning",
                                   id + ": " + error.toStdString(), true, id,
                                   "strip_component_ref" ) );
      }
    }
  }

  // --- duplicate furniture (same semantic role twice) -------------------------
  const char *roleCollections[] = { "titles", "legends", "scale_bars",
                                    "north_arrows", "source_notes" };
  for ( const char *collection : roleCollections )
  {
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    std::set<std::string> seenRoles;
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || !item.isMember( "semantic_role" ) ||
           !item["semantic_role"].isString() )
        continue;
      const std::string role = item["semantic_role"].asString();
      if ( !seenRoles.insert( role ).second )
        issues.push_back( issue( "MAP_DUPLICATE_FURNITURE", "warning",
                                 item["id"].asString() + ": duplicate semantic_role '" + role +
                                   "' in " + collection,
                                 true, item["id"].asString(), "dedupe_identical" ) );
    }
  }

  // --- inset placement ---------------------------------------------------------
  if ( hasNonEmpty( "inset_maps" ) && hasNonEmpty( "map_frames" ) )
  {
    for ( const auto &inset : spec["inset_maps"] )
    {
      if ( !inset.isObject() || !inset.isMember( "rect_mm" ) )
        continue;
      bool insideAnyFrame = false;
      for ( const auto &frame : spec["map_frames"] )
        insideAnyFrame =
          insideAnyFrame ||
          ( frame.isObject() && frame.isMember( "rect_mm" ) &&
            rectsIntersect( inset["rect_mm"], frame["rect_mm"] ) );
      if ( !insideAnyFrame )
        issues.push_back( issue( "MAP_INSET_PLACEMENT", "warning",
                                 inset["id"].asString() +
                                   ": inset does not overlap any map frame (locators belong "
                                   "inside the main map)",
                                 true, inset["id"].asString(), "place_inset" ) );

      // --- locator scale rule (Platform 5.0): a locator whose inset extent
      // diverges wildly from the referenced frame renders a useless extent
      // indicator (a sliver or a full-frame copy).
      if ( inset.isMember( "locator" ) && inset["locator"].isObject() &&
           inset["locator"].isMember( "target" ) )
      {
        const Json::Value *targetExtent = nullptr;
        Json::Value insetExtent;
        if ( inset.isMember( "extent" ) && inset["extent"].isArray() && inset["extent"].size() == 4 )
          insetExtent = inset["extent"];
        for ( const auto &frame : spec["map_frames"] )
        {
          if ( frame.isObject() && frame.isMember( "id" ) &&
               frame["id"].asString() == inset["locator"]["target"].asString() )
          {
            if ( frame.isMember( "extent" ) && frame["extent"].isArray() && frame["extent"].size() == 4 )
            {
              targetExtent = &frame["extent"];
              break;
            }
          }
        }
        if ( targetExtent == nullptr && !insetExtent.isNull() && spec["map_frames"].isArray() &&
             !spec["map_frames"].empty() && spec["map_frames"][0].isMember( "extent" ) )
          targetExtent = &spec["map_frames"][0]["extent"];
        if ( targetExtent != nullptr && !insetExtent.isNull() )
        {
          auto extentArea = []( const Json::Value &e ) {
            return std::fabs( ( e[2].asDouble() - e[0].asDouble() ) *
                              ( e[3].asDouble() - e[1].asDouble() ) );
          };
          const double insetArea = extentArea( insetExtent );
          const double targetArea = extentArea( *targetExtent );
          if ( insetArea > 0 && targetArea > 0 )
          {
            const double ratio = std::max( insetArea / targetArea, targetArea / insetArea );
            if ( ratio > 100.0 )
              issues.push_back( issue(
                "MAP_LOCATOR_MISMATCH", "warning",
                inset["id"].asString() + ": locator extent differs from the target frame by ~" +
                  std::to_string( static_cast<int>( ratio ) ) +
                  "× — the extent indicator will be unreadable at render scale",
                false, inset["id"].asString(), nullptr ) );
          }
        }
      }
    }
  }

  // --- atlas completeness (Platform 5.0) ---------------------------------------
  if ( spec.isMember( "page" ) && spec["page"].isObject() && spec["page"].isMember( "atlas" ) &&
       spec["page"]["atlas"].isObject() )
  {
    const Json::Value &atlas = spec["page"]["atlas"];
    if ( atlas.get( "enabled", false ).asBool() )
    {
      const std::string coverage = atlas.get( "coverage_layer", "" ).asString();
      if ( coverage.empty() )
        issues.push_back( issue( "MAP_ATLAS_INCOMPLETE", "error",
                                 "page.atlas is enabled but coverage_layer is empty — the atlas "
                                 "cannot iterate features",
                                 false, "", nullptr ) );
      const bool hasSort =
        ( atlas.isMember( "sort_by" ) && !atlas["sort_by"].asString().empty() ) ||
        ( atlas.isMember( "sort_expression" ) && !atlas["sort_expression"].asString().empty() );
      if ( atlas.isMember( "sort_order" ) && !hasSort )
        issues.push_back( issue( "MAP_ATLAS_INCOMPLETE", "warning",
                                 "page.atlas.sort_order declared without sort_by/sort_expression",
                                 false, "", nullptr ) );
    }
  }

  // --- conditional context missing (Platform 5.0) ------------------------------
  {
    bool carriesConditions = false;
    for ( int c = 0; c < mapspec::kCollectionCount && !carriesConditions; ++c )
    {
      const char *collection = mapspec::kCollections[c];
      if ( !spec.isMember( collection ) || !spec[collection].isArray() )
        continue;
      for ( const auto &item : spec[collection] )
      {
        if ( item.isObject() && ( item.isMember( "visible_if" ) || item.isMember( "content_if" ) ) )
        {
          carriesConditions = true;
          break;
        }
      }
    }
    if ( spec.isMember( "pages" ) && spec["pages"].isArray() )
      for ( const auto &pageEntry : spec["pages"] )
        if ( pageEntry.isObject() && pageEntry.isMember( "page_if" ) )
          carriesConditions = true;
    if ( carriesConditions && !spec.isMember( "condition_context" ) )
      issues.push_back( issue(
        "MAP_CONDITIONAL_CONTEXT_MISSING", "warning",
        "items/pages carry visible_if/content_if/page_if conditions but no condition_context is "
        "stamped — they will keep their content at compile (nothing is silently hidden)",
        false, "", nullptr ) );
  }

  // --- page balance (Platform 5.0): every declared page should carry items ----
  if ( spec.isMember( "pages" ) && spec["pages"].isArray() && !spec["pages"].empty() )
  {
    const int declaredPages = static_cast<int>( spec["pages"].size() ) + 1;
    std::vector<int> itemsPerPage( declaredPages, 0 );
    for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    {
      const char *collection = mapspec::kCollections[c];
      if ( !spec.isMember( collection ) || !spec[collection].isArray() )
        continue;
      for ( const auto &item : spec[collection] )
      {
        if ( !item.isObject() )
          continue;
        const int pageIndex =
          item.isMember( "page" ) && item["page"].isIntegral() ? item["page"].asInt() : 0;
        if ( pageIndex >= 0 && pageIndex < declaredPages )
          ++itemsPerPage[pageIndex];
      }
    }
    for ( int p = 0; p < declaredPages; ++p )
    {
      if ( itemsPerPage[p] == 0 )
        issues.push_back( issue( "MAP_PAGE_BALANCE", "warning",
                                 "page " + std::to_string( p ) + " carries no items and will "
                                                                  "export as a blank sheet",
                                 false, "", nullptr ) );
    }
  }

  // --- report/publication CRS note rule (Platform 5.0) --------------------------
  {
    bool isReportish = false;
    if ( spec.isMember( "template" ) && spec["template"].isString() )
    {
      const std::string templateId = spec["template"].asString();
      isReportish = templateId.find( "report" ) != std::string::npos ||
                    templateId.find( "publication" ) != std::string::npos;
    }
    if ( spec.isMember( "pages" ) && spec["pages"].isArray() && !spec["pages"].empty() )
      isReportish = true;
    if ( isReportish && hasNonEmpty( "source_notes" ) )
    {
      bool crsDeclared = false;
      for ( const auto &note : spec["source_notes"] )
      {
        if ( !note.isObject() || !note.isMember( "text" ) )
          continue;
        const std::string text = note["text"].asString();
        crsDeclared = crsDeclared || text.find( "CRS" ) != std::string::npos ||
                      text.find( "EPSG" ) != std::string::npos ||
                      text.find( "坐标" ) != std::string::npos;
      }
      if ( !crsDeclared )
        issues.push_back( issue( "MAP_MISSING_CRS_NOTE", "warning",
                                 "report/publication documents should state the CRS in a source "
                                 "note (CRS / EPSG / 坐标系统)",
                                 false, "", nullptr ) );
    }
  }

  // --- Platform 6.0 (Milestone I): style-knowledge semantic rules --------------
  {
    StyleRegistry &styles = StyleRegistry::instance();
    bool uncertaintyStyled = false;
    for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    {
      const char *collection = mapspec::kCollections[c];
      if ( !spec.isMember( collection ) || !spec[collection].isArray() )
        continue;
      for ( const auto &item : spec[collection] )
      {
        if ( !item.isObject() || !item.isMember( "style_ref" ) || !item["style_ref"].isString() )
          continue;
        const std::string styleRef = item["style_ref"].asString();
        const Json::Value style = styles.find( QString::fromStdString( styleRef ) );
        if ( style.isNull() )
        {
          issues.push_back( issue( "MAP_STYLE_REF_UNKNOWN", "warning",
                                   "style_ref '" + styleRef + "' does not resolve in the style "
                                   "registry",
                                   false, item.isMember( "id" ) && item["id"].isString()
                                            ? item["id"].asString()
                                            : "",
                                   nullptr ) );
          continue;
        }
        // Semantic applicability: when the item binding declares dataset
        // facts (kind/modality/band_count/value range), the style must agree
        // — a semantically wrong renderer is reported, never applied.
        if ( item.isMember( "binding" ) && item["binding"].isObject() )
        {
          const auto mismatches =
            checkStyleApplicability( style, item["binding"] );
          for ( const auto &mismatch : mismatches )
            issues.push_back( issue( "MAP_STYLE_DATA_MISMATCH", "warning", mismatch, false,
                                     item.isMember( "id" ) && item["id"].isString()
                                       ? item["id"].asString()
                                       : "",
                                     nullptr ) );
        }
        // Uncertainty semantics obligation: probability/uncertainty styles
        // must ship an explicit uncertainty note on the document.
        if ( style.isMember( "semantics" ) && style["semantics"].isArray() )
          for ( const auto &tag : style["semantics"] )
            if ( tag.isString() && ( tag.asString() == "uncertainty" ||
                                     tag.asString() == "probability" ) )
              uncertaintyStyled = true;
      }
    }
    if ( uncertaintyStyled )
    {
      bool uncertaintyNoted = false;
      for ( const char *collection : { "labels", "annotations", "source_notes", "titles" } )
      {
        if ( !spec.isMember( collection ) || !spec[collection].isArray() )
          continue;
        for ( const auto &note : spec[collection] )
        {
          if ( !note.isObject() )
            continue;
          std::string text = note.get( "text", "" ).asString();
          text += " " + note.get( "semantic_role", "" ).asString();
          for ( char &ch : text )
            ch = static_cast<char>( std::tolower( static_cast<unsigned char>( ch ) ) );
          uncertaintyNoted = uncertaintyNoted || text.find( "uncertaint" ) != std::string::npos ||
                             text.find( "probability" ) != std::string::npos ||
                             text.find( "confidence" ) != std::string::npos ||
                             text.find( "不确定性" ) != std::string::npos ||
                             text.find( "概率" ) != std::string::npos;
        }
      }
      if ( !uncertaintyNoted )
        issues.push_back( issue(
          "MAP_UNCERTAINTY_NOTE_MISSING", "warning",
          "the document styles uncertainty/probability data but carries no uncertainty note "
          "(label/annotation mentioning uncertainty, probability or confidence)",
          false, "", nullptr ) );
    }
  }

  // --- composition solver leftovers (from the same resolved copy) -------------
  for ( const auto &note : solvedResult.unsatisfied )
    issues.push_back(
      issue( "MAP_CONSTRAINT_UNSATISFIABLE", "warning", note, false, "", nullptr ) );

  // --- Platform 7.0: contrast advisories over referenced styles --------------
  if ( spec.isMember( "layers" ) && spec["layers"].isArray() )
    for ( const auto &layer : spec["layers"] )
    {
      if ( !layer.isObject() || !layer.isMember( "style_ref" ) ||
           !layer["style_ref"].isString() )
        continue;
      const Json::Value style = StyleRegistry::instance().find(
        QString::fromStdString( layer["style_ref"].asString() ) );
      if ( style.isNull() || !style.isObject() )
        continue; // unknown refs are MAP_STYLE_REF_UNKNOWN's business
      const std::vector<std::string> warnings =
        checkStyleContrast( style, &tokens );
      for ( const auto &warning : warnings )
        issues.push_back( issue( "MAP_CONTRAST_LOW", "warning", warning, false,
                                 layer.isMember( "id" ) && layer["id"].isString()
                                   ? layer["id"].asString()
                                   : std::string(),
                                 nullptr ) );
    }

  // --- pairwise overlap between non-map items ---------------------------------
  // Bounded output: issue lists are capped so a pathological (but capped-
  // input) spec cannot flood the agent context; the truncation is reported.
  static constexpr int kMaxIssues = 500;
  const auto pushIssue = [ &issues ]( Json::Value value ) {
    if ( static_cast<int>( issues.size() ) < kMaxIssues )
      issues.push_back( std::move( value ) );
  };

  const char *overlappable[] = { "titles", "labels", "legends", "scale_bars", "north_arrows",
                                 "source_notes", "annotations", "charts", "colorbars" };
  for ( int i = 0; i < 9; ++i )
  {
    if ( !spec.isMember( overlappable[i] ) || !spec[overlappable[i]].isArray() )
      continue;
    for ( Json::Value::ArrayIndex ai = 0; ai < spec[overlappable[i]].size(); ++ai )
    {
      const Json::Value &a = spec[overlappable[i]][ai];
      if ( !a.isObject() || !a.isMember( "rect_mm" ) )
        continue;
      for ( int j = i; j < 9; ++j )
      {
        if ( !spec.isMember( overlappable[j] ) || !spec[overlappable[j]].isArray() )
          continue;
        for ( Json::Value::ArrayIndex bi = 0; bi < spec[overlappable[j]].size(); ++bi )
        {
          const Json::Value &b = spec[overlappable[j]][bi];
          if ( !b.isObject() || !b.isMember( "rect_mm" ) )
            continue;
          if ( i == j && bi <= ai ) // each pair once, in a stable order
            continue;
          if ( a["id"] == b["id"] )
            continue;
          // Multi-page documents: only same-page items can actually overlap.
          const int pageA = a.isMember( "page" ) && a["page"].isIntegral() ? a["page"].asInt() : 0;
          const int pageB = b.isMember( "page" ) && b["page"].isIntegral() ? b["page"].asInt() : 0;
          if ( pageA != pageB )
            continue;
          if ( rectsIntersect( a["rect_mm"], b["rect_mm"] ) )
          {
            pushIssue( issue(
              "MAP_OVERLAP", "warning",
              a["id"].asString() + " overlaps " + b["id"].asString(), true, a["id"].asString(),
              "reposition" ) );
          }
        }
      }
    }
  }

  // --- merge compiled-layout findings (layout:preflight report) ---------------
  if ( compiledReport.isObject() && compiledReport.isMember( "issues" ) &&
       compiledReport["issues"].isArray() )
  {
    for ( const auto &layoutIssue : compiledReport["issues"] )
    {
      if ( !layoutIssue.isObject() )
        continue;
      Json::Value merged = makeIssue( "LAYOUT_" + layoutIssue.get( "check", "unknown" ).asString(),
                                      layoutIssue.get( "severity", "warning" ).asString(),
                                      layoutIssue.get( "message", "" ).asString(),
                                      false,
                                      layoutIssue.get( "item", "" ).asString(), Json::Value() );
      issues.push_back( merged );
    }
  }

  if ( static_cast<int>( issues.size() ) >= kMaxIssues )
    issues.push_back( issue( "MAPSPEC_ISSUES_TRUNCATED", "warning",
                             "issue list truncated at " + std::to_string( kMaxIssues ) +
                               " entries — fix the reported findings and re-run",
                             false, "", nullptr ) );

  int errorCount = 0;
  int warningCount = 0;
  int repairableCount = 0;
  for ( const auto &i : issues )
  {
    if ( i["severity"].asString() == "error" )
      ++errorCount;
    else
      ++warningCount;
    if ( i.get( "repairable", false ).asBool() )
      ++repairableCount;
  }
  const int score = std::max( 0, 100 - 20 * errorCount - 8 * warningCount );

  Json::Value body( Json::objectValue );
  body["quality_score"] = score;
  // The pass gate drives the repair loop: blocking errors OR unresolved
  // repairable findings keep a map from passing; non-repairable warnings
  // (e.g. empty map frame) are advisory.
  body["passed"] = errorCount == 0 && repairableCount == 0;
  Json::Value issuesArr( Json::arrayValue );
  for ( const auto &i : issues )
    issuesArr.append( i );
  body["issues"] = issuesArr;
  body["error_count"] = errorCount;
  body["warning_count"] = warningCount;
  return makeEnvelope( "map_quality_report", body );
}

int repairMapSpec( Json::Value &spec, const Json::Value &report )
{
  int applied = 0;
  if ( !report.isObject() || !report.isMember( "issues" ) )
    return 0;
  // Callers (cartography:repair, composeRepairLoop helpers) run the
  // composition solver once BEFORE looping repairs — re-solving anchored
  // geometry every pass would un-do MAP_OFF_PAGE/MARGIN clamps and prevent
  // convergence. Anchor outcomes the solver could not satisfy in-page are
  // reported and skipped, so their rects stay untouched.
  const double pageW = spec["page"]["width_mm"].asDouble();
  const double pageH = spec["page"]["height_mm"].asDouble();
  const std::string mapRef = mainMapRef( spec );

  for ( const auto &item : report["issues"] )
  {
    if ( !item.isObject() || !item.get( "repairable", false ).asBool() )
      continue;
    const std::string code = item.get( "code", "" ).asString();
    const Json::Value &action = item.get( "suggested_action", Json::Value() );

    if ( code == "MAP_MISSING_TITLE" && action.isMember( "action" ) &&
         action["action"].asString() == "add_title" )
    {
      Json::Value title( Json::objectValue );
      title["semantic_role"] = "title.main";
      title["text"] = "地图标题";
      title["rect_mm"] = rect( 12, 6, 200, 14 );
      title["font"] = Json::Value( Json::objectValue );
      title["font"]["size_pt"] = 18;
      mapspec::appendMapSpecItem( spec, "titles", title );
      ++applied;
    }
    else if ( code == "MAP_MISSING_LEGEND" )
    {
      Json::Value legend( Json::objectValue );
      legend["semantic_role"] = "legend.primary";
      legend["title"] = "图例";
      legend["rect_mm"] = rect( pageW - 80, 30, 66, 80 );
      if ( !mapRef.empty() )
        legend["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "legends", legend );
      ++applied;
    }
    else if ( code == "MAP_MISSING_SCALE_BAR" )
    {
      Json::Value scaleBar( Json::objectValue );
      scaleBar["semantic_role"] = "scalebar.primary";
      scaleBar["style"] = "Single Box";
      scaleBar["units"] = "km";
      scaleBar["rect_mm"] = rect( 14, pageH - 20, 60, 8 );
      if ( !mapRef.empty() )
        scaleBar["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "scale_bars", scaleBar );
      ++applied;
    }
    else if ( code == "MAP_MISSING_NORTH_ARROW" )
    {
      Json::Value arrow( Json::objectValue );
      arrow["semantic_role"] = "north_arrow.primary";
      arrow["rect_mm"] = rect( pageW - 16, 6, 12, 12 );
      if ( !mapRef.empty() )
        arrow["map_ref"] = mapRef;
      mapspec::appendMapSpecItem( spec, "north_arrows", arrow );
      ++applied;
    }
    else if ( code == "MAP_MISSING_SOURCE_NOTE" )
    {
      Json::Value note( Json::objectValue );
      note["semantic_role"] = "source.primary";
      note["text"] = "数据来源: SICNU GEO RS / exp-rs";
      note["rect_mm"] = rect( pageW - 130, pageH - 16, 116, 8 );
      note["font"] = Json::Value( Json::objectValue );
      note["font"]["size_pt"] = 7;
      mapspec::appendMapSpecItem( spec, "source_notes", note );
      ++applied;
    }
    else if ( code == "MAP_OFF_PAGE" || code == "MAP_INVALID_RECT" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item )
        continue;
      if ( !found.item->isMember( "rect_mm" ) || ( *found.item )["rect_mm"].size() != 4 )
        continue;
      Json::Value &foundItem = *found.item;
      double x = foundItem["rect_mm"][0].asDouble();
      double y = foundItem["rect_mm"][1].asDouble();
      double w = foundItem["rect_mm"][2].asDouble();
      double h = foundItem["rect_mm"][3].asDouble();
      w = std::clamp( w, 1.0, pageW );
      h = std::clamp( h, 1.0, pageH );
      x = std::clamp( x, 0.0, std::max( 0.0, pageW - w ) );
      y = std::clamp( y, 0.0, std::max( 0.0, pageH - h ) );
      foundItem["rect_mm"] = rect( x, y, w, h );
      ++applied;
    }
    else if ( code == "MAP_TINY_FONT" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item )
        continue;
      Json::Value &foundItem = *found.item;
      if ( !foundItem.isMember( "font" ) || !foundItem["font"].isObject() )
        foundItem["font"] = Json::Value( Json::objectValue );
      foundItem["font"]["size_pt"] = 8;
      ++applied;
    }
    else if ( code == "MAP_TITLE_OVERFLOW" || code == "MAP_SOURCE_NOTE_CLIPPING" ||
              code == "MAP_TEXT_OVERFLOW" || code == "MAP_TEXT_WRAP_OVERFLOW" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item )
        continue;
      if ( repairTextOverflow( *found.item, pageW, pageH, found.collection ) )
        ++applied;
    }
    else if ( code == "MAP_NODATA_LEGEND" )
    {
      // Platform 8.0: stamp legend.nodata from the referenced style's
      // declaration (converging: the re-preflight sees the declaration and
      // the rule passes).
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "style_ref" ) ||
           !( *found.item )["style_ref"].isString() )
        continue;
      const Json::Value style = StyleRegistry::instance().find(
        QString::fromStdString( ( *found.item )["style_ref"].asString() ) );
      std::string label = "NoData";
      if ( style.isObject() && style.isMember( "raster" ) && style["raster"].isObject() &&
           style["raster"].isMember( "nodata" ) && style["raster"]["nodata"].isObject() &&
           style["raster"]["nodata"].isMember( "label" ) &&
           style["raster"]["nodata"]["label"].isString() &&
           !style["raster"]["nodata"]["label"].asString().empty() )
        label = style["raster"]["nodata"]["label"].asString();
      Json::Value nodata( Json::objectValue );
      nodata["label"] = label;
      ( *found.item )["nodata"] = nodata;
      ++applied;
    }
    else if ( code == "MAP_LAYER_UNREFERENCED" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      if ( mapRef.empty() || id.empty() )
        continue; // no frame to attach to: stays reported
      ItemRef found = findItemMutable( spec, mapRef );
      if ( !found.item )
        continue;
      Json::Value &layers = ( *found.item )["layers"];
      if ( !layers.isArray() )
        layers = Json::Value( Json::arrayValue );
      bool already = false;
      for ( const auto &ref : layers )
        already = already || ( ref.isString() && ref.asString() == id );
      if ( already )
        continue;
      layers.append( id );
      ++applied;
    }
    else if ( code == "MAP_MARGIN_VIOLATION" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "rect_mm" ) ||
           ( *found.item )["rect_mm"].size() != 4 )
        continue;
      Json::Value &foundItem = *found.item;
      const double m = declaredMargin( spec ) > 0 ? declaredMargin( spec ) : 12.0;
      const double boxW = pageW - 2 * m;
      const double boxH = pageH - 2 * m;
      if ( boxW <= 0 || boxH <= 0 )
        continue; // degenerate margin box: leave the item, rule stays reported
      double x = foundItem["rect_mm"][0].asDouble();
      double y = foundItem["rect_mm"][1].asDouble();
      double w = foundItem["rect_mm"][2].asDouble();
      double h = foundItem["rect_mm"][3].asDouble();
      w = std::min( w, boxW );
      h = std::min( h, boxH );
      x = std::clamp( x, m, std::max( m, pageW - m - w ) );
      y = std::clamp( y, m, std::max( m, pageH - m - h ) );
      foundItem["rect_mm"] = rect( x, y, w, h );
      ++applied;
    }
    else if ( code == "MAP_LEGEND_DENSITY" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "rect_mm" ) ||
           ( *found.item )["rect_mm"].size() != 4 )
        continue;
      Json::Value &foundItem = *found.item;
      const int entries = foundItem.isMember( "max_entries" ) && foundItem["max_entries"].isIntegral()
                            ? foundItem["max_entries"].asInt()
                            : 0;
      const Json::Value tokens = resolveTokenSet( spec );
      const double lineH = tokenNumber( tokens, "furniture.legend_line_height_mm", 4.5 );
      const double requiredH = 8.0 + entries * lineH;
      const double currentH = foundItem["rect_mm"][3].asDouble();
      // Same margin contract as MAP_MARGIN_VIOLATION, so growing here can
      // never fight the margin repair on the next pass.
      const double margin = declaredMargin( spec ) > 0 ? declaredMargin( spec ) : 12.0;
      const double y = foundItem["rect_mm"][1].asDouble();
      if ( y + requiredH <= pageH - margin )
      {
        foundItem["rect_mm"][3] = requiredH; // grow downward inside the margin
        ++applied;
      }
      else if ( currentH > 1.0 )
      {
        // Page-bound: spread entries across columns instead (capped —
        // legends needing more than 6 columns stay reported for the agent).
        const int columns = static_cast<int>( std::ceil( requiredH / std::max( 1.0, currentH ) ) );
        if ( columns > 1 && columns <= 6 )
        {
          foundItem["columns"] = columns;
          ++applied;
        }
      }
    }
    else if ( code == "MAP_DUPLICATE_FURNITURE" )
    {
      // Remove only byte-identical duplicates (content-safe deletion).
      // Preflight reports within-collection duplicates, so the scan is
      // confined to the reported item's collection; both items are copied
      // BEFORE any removal because removeMapSpecItem rebuilds the array
      // (any held Json::Value* would dangle).
      const std::string id = item.get( "item_id", "" ).asString();
      const Json::Value location = mapspec::findMapSpecItem( spec, id );
      if ( location.isNull() )
        continue;
      const char *collection = nullptr;
      for ( int c = 0; c < mapspec::kCollectionCount; ++c )
        if ( std::string( mapspec::kCollections[c] ) == location["collection"].asString() )
          collection = mapspec::kCollections[c];
      if ( !collection || !spec.isMember( collection ) || !spec[collection].isArray() )
        continue;
      const Json::Value self = spec[location["collection"].asString()][location["index"].asInt()];
      const std::string role = self.get( "semantic_role", "" ).asString();
      for ( const auto &other : spec[collection] )
      {
        if ( !other.isObject() || !other.isMember( "id" ) || other["id"].asString() == id )
          continue;
        if ( other.get( "semantic_role", "" ).asString() != role )
          continue;
        Json::Value a = other;
        Json::Value b = self;
        a.removeMember( "id" );
        b.removeMember( "id" );
        if ( a.toStyledString() == b.toStyledString() )
        {
          mapspec::removeMapSpecItem( spec, id );
          ++applied;
        }
        break; // compare against the first same-role candidate only
      }
    }
    else if ( code == "MAP_INSET_PLACEMENT" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "rect_mm" ) ||
           ( *found.item )["rect_mm"].size() != 4 || !spec.isMember( "map_frames" ) ||
           spec["map_frames"].empty() || !spec["map_frames"][0].isMember( "rect_mm" ) )
        continue;
      Json::Value &inset = *found.item;
      const Json::Value &frameRect = spec["map_frames"][0]["rect_mm"];
      const double insetW = inset["rect_mm"][2].asDouble();
      const double insetH = inset["rect_mm"][3].asDouble();
      const double frameX = frameRect[0].asDouble();
      const double frameY = frameRect[1].asDouble();
      const double frameW = frameRect[2].asDouble();
      const double frameH = frameRect[3].asDouble();
      double x = frameX + frameW - insetW - 4.0;
      double y = frameY + frameH - insetH - 4.0;
      // An inset larger than its frame still lands on the page (the frame
      // itself stays the next pass's problem — never both at once).
      x = std::clamp( x, 0.0, std::max( 0.0, pageW - insetW ) );
      y = std::clamp( y, 0.0, std::max( 0.0, pageH - insetH ) );
      inset["rect_mm"] = rect( x, y, insetW, insetH );
      ++applied;
    }
    else if ( code == "MAP_CHART_OVERFLOW" )
    {
      // Platform 5.0: grow a table chart downward to fit its declared rows,
      // clamped to the page margin (same margin contract as legend growth).
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "rect_mm" ) ||
           ( *found.item )["rect_mm"].size() != 4 || !( *found.item ).isMember( "chart" ) )
        continue;
      Json::Value &foundItem = *found.item;
      const Json::Value &chart = foundItem["chart"];
      const int rows = chart.isMember( "binding" ) && chart["binding"].isMember( "data" ) &&
                             chart["binding"]["data"].isArray()
                         ? static_cast<int>( chart["binding"]["data"].size() )
                         : 0;
      if ( rows <= 0 )
        continue;
      const double fontPt = chart.isMember( "style" ) && chart["style"].isMember( "font_pt" ) &&
                                  chart["style"]["font_pt"].isNumeric()
                              ? chart["style"]["font_pt"].asDouble()
                              : 10.0;
      const double requiredH = 12.0 + rows * fontPt * 0.352778 * 1.5;
      const double margin = declaredMargin( spec ) > 0 ? declaredMargin( spec ) : 12.0;
      const double y = foundItem["rect_mm"][1].asDouble();
      const double maxH = pageH - margin - y;
      if ( maxH > foundItem["rect_mm"][3].asDouble() )
      {
        foundItem["rect_mm"][3] = std::min( requiredH, maxH );
        ++applied;
      }
    }
    else if ( code == "MAP_UNKNOWN_COMPONENT" )
    {
      // The reference resolved to nothing — stripping it loses no content.
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item )
        continue;
      ( *found.item ).removeMember( "source_component" );
      ++applied;
    }
    else if ( code == "MAP_UNBALANCED_FRAMES" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item || !found.item->isMember( "rect_mm" ) ||
           ( *found.item )["rect_mm"].size() != 4 || !spec.isMember( "map_frames" ) ||
           spec["map_frames"].empty() || !spec["map_frames"][0].isMember( "rect_mm" ) )
        continue;
      Json::Value &frame = *found.item;
      frame["rect_mm"][3] = spec["map_frames"][0]["rect_mm"][3];
      ++applied;
    }
    else if ( code == "MAP_OVERLAP" )
    {
      const std::string id = item.get( "item_id", "" ).asString();
      ItemRef found = findItemMutable( spec, id );
      if ( !found.item )
        continue;
      Json::Value &foundItem = *found.item;
      if ( !foundItem.isMember( "rect_mm" ) || foundItem["rect_mm"].size() != 4 )
        continue;
      // Deterministic relocation: try the classic anchor slots in a fixed
      // order and take the first that neither leaves the page nor collides
      // with any other item. Convergence > cleverness for agent repair.
      const double w = foundItem["rect_mm"][2].asDouble();
      const double h = foundItem["rect_mm"][3].asDouble();
      const double m = 6.0;
      struct Slot { double x; double y; };
      const Slot candidates[] = {
        { pageW - m - w, m },   { m, m },               { m, pageH - m - h },
        { pageW - m - w, pageH - m - h }, { m, ( pageH - h ) / 2.0 },
        { pageW - m - w, ( pageH - h ) / 2.0 }, { ( pageW - w ) / 2.0, pageH - m - h },
      };
      // Collect every other furniture rect — the same collections the
      // overlap detector scans, so a relocated item never re-triggers
      // MAP_OVERLAP. Map frames are overlays, not obstacles.
      const char *overlappable[] = { "titles",   "labels",    "legends",
                                     "scale_bars", "north_arrows", "source_notes",
                                     "annotations", "charts",  "colorbars" };
      std::vector<Json::Value> others;
      for ( const char *collection : overlappable )
      {
        if ( !spec.isMember( collection ) || !spec[collection].isArray() )
          continue;
        for ( const auto &other : spec[collection] )
        {
          if ( !other.isObject() || !other.isMember( "id" ) || other["id"].asString() == id )
            continue;
          if ( other.isMember( "rect_mm" ) && other["rect_mm"].isArray() &&
               other["rect_mm"].size() == 4 )
            others.push_back( other["rect_mm"] );
        }
      }
      for ( const auto &candidate : candidates )
      {
        if ( candidate.x < 0 || candidate.y < 0 || candidate.x + w > pageW ||
             candidate.y + h > pageH )
          continue;
        bool free = true;
        for ( const auto &other : others )
          free = free && !rectsIntersect( rect( candidate.x, candidate.y, w, h ), other );
        if ( free )
        {
          foundItem["rect_mm"] = rect( candidate.x, candidate.y, w, h );
          ++applied;
          break;
        }
      }
    }
  }
  return applied;
}

Json::Value preflightRuleCatalog()
{
  struct Rule
  {
      const char *code;
      const char *severity;
      bool repairable;
      const char *description;
  };
  static const Rule kRules[] = {
    { "MAPSPEC_INVALID", "error", false, "Structural validation problem (see validateMapSpec)." },
    { "MAP_MISSING_MAP", "error", false, "No map frame in the document." },
    { "MAP_EMPTY_MAP", "warning", false, "Map frame has neither layers nor extent; renders blank." },
    { "MAP_MISSING_TITLE", "warning", true, "No title item." },
    { "MAP_MISSING_LEGEND", "warning", true, "No legend item." },
    { "MAP_MISSING_SCALE_BAR", "warning", true, "No scale bar." },
    { "MAP_MISSING_NORTH_ARROW", "warning", true, "No north arrow." },
    { "MAP_MISSING_SOURCE_NOTE", "warning", true, "No data-source note." },
    { "MAP_INVALID_RECT", "error", false, "rect_mm width/height not positive." },
    { "MAP_OFF_PAGE", "error", true, "Item rect exceeds the page bounds." },
    { "MAP_MARGIN_VIOLATION", "warning", true, "Item violates a declared page.margin_mm." },
    { "MAP_TINY_FONT", "warning", true, "Font below 6 pt." },
    { "MAP_TITLE_OVERFLOW", "warning", true, "Title text likely overflows its rect." },
    { "MAP_SOURCE_NOTE_CLIPPING", "warning", true, "Source-note text likely clipped." },
    { "MAP_TEXT_OVERFLOW", "warning", true, "Label/annotation text likely overflows its rect." },
    { "MAP_LEGEND_DENSITY", "warning", true, "Legend rect too small for the declared max_entries." },
    { "MAP_DUPLICATE_FURNITURE", "warning", true,
      "Same semantic_role twice; identical duplicates are removed by repair." },
    { "MAP_INVALID_BINDING", "warning", false, "Chart binding does not resolve." },
    { "MAP_UNBALANCED_FRAMES", "warning", true, "Multi-map frame heights deviate >15%." },
    { "MAP_INSET_PLACEMENT", "warning", true, "Locator inset does not overlap any map frame." },
    { "MAP_UNKNOWN_COMPONENT", "warning", true, "source_component reference does not resolve." },
    { "MAP_CONSTRAINT_UNSATISFIABLE", "warning", false, "Composition solver could not satisfy a constraint." },
    { "MAP_OVERLAP", "warning", true, "Two furniture items overlap." },
    { "MAP_LOCATOR_MISMATCH", "warning", false,
      "Locator inset extent diverges from the referenced frame (>100x); indicator unreadable." },
    { "MAP_ATLAS_INCOMPLETE", "error", false,
      "Atlas enabled without a coverage layer (error), or sort_order without a sort key." },
    { "MAP_CONDITIONAL_CONTEXT_MISSING", "warning", false,
      "Conditions declared without condition_context; content is kept, nothing hidden." },
    { "MAP_CHART_OVERFLOW", "warning", true, "Table rows cannot fit the chart rect; repair grows it." },
    { "MAP_PAGE_BALANCE", "warning", false, "A declared page carries no items (blank export)." },
    { "MAP_MISSING_CRS_NOTE", "warning", false,
      "Report/publication source notes do not state the CRS." },
    { "MAP_STYLE_REF_UNKNOWN", "warning", false, "style_ref does not resolve in the style registry." },
    { "MAP_STYLE_DATA_MISMATCH", "warning", false,
      "Referenced style contradicts the item binding (kind/modality/bands/value domain)." },
    { "MAP_UNCERTAINTY_NOTE_MISSING", "warning", false,
      "Uncertainty/probability-styled document carries no uncertainty note." },
    { "MAP_INVISIBLE_LAYER", "warning", false,
      "Declarative layer is invisible or fully transparent; it will not render." },
    { "MAP_LAYER_UNREFERENCED", "warning", true,
      "Declarative layer is referenced by no map frame or inset." },
    { "MAP_LEGEND_MISMATCH", "warning", false,
      "Explicit legend classes do not all appear in the referenced style's classes." },
    { "MAP_NODATA_LEGEND", "warning", true,
      "A legend referencing a style that declares raster.nodata carries no nodata "
      "mention; repair stamps legend.nodata from the style." },
    { "MAP_TEXT_WRAP_OVERFLOW", "warning", true,
      "Wrap-aware text layout (CJK kinsoku included) does not fit the item rect." },
    { "MAP_CONTRAST_LOW", "warning", false,
      "Referenced style fails a deterministic contrast floor (label text or adjacent classes)." },
    { "MAPSPEC_ISSUES_TRUNCATED", "warning", false,
      "Issue list capped at 500 entries; fix reported findings and re-run." },
    { "LAYOUT_*", "warning", false, "Findings merged from the compiled layout preflight." },
  };
  Json::Value catalog( Json::arrayValue );
  for ( const auto &rule : kRules )
  {
    Json::Value entry( Json::objectValue );
    entry["code"] = rule.code;
    entry["severity"] = rule.severity;
    entry["repairable"] = rule.repairable;
    entry["description"] = rule.description;
    catalog.append( entry );
  }
  return catalog;
}



std::string structuralDigest( const Json::Value &spec )
{
  // Canonical entries: every item with a usable rect from every collection
  // (plus the legacy items[] collection), id-sorted, geometry rounded to
  // 0.01 mm. Nothing platform-, locale- or pointer-dependent enters the
  // hash. The page envelope participates so page-size changes are visible.
  std::vector<std::string> entries;
  double pageW = 0.0;
  double pageH = 0.0;
  if ( spec.isObject() && spec.isMember( "page" ) && spec["page"].isObject() )
  {
    pageW = spec["page"].get( "width_mm", 0.0 ).asDouble();
    pageH = spec["page"].get( "height_mm", 0.0 ).asDouble();
  }
  entries.push_back( "page:" +
                     std::to_string( static_cast<long long>( std::llround( pageW * 100.0 ) ) ) +
                     "x" +
                     std::to_string( static_cast<long long>( std::llround( pageH * 100.0 ) ) ) );

  auto round2 = []( double v ) {
    return std::round( v * 100.0 ) / 100.0;
  };
  auto addCollection = [ & ]( const char *collection ) {
    if ( !spec.isObject() || !spec.isMember( collection ) || !spec[collection].isArray() )
      return;
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || !item.isMember( "id" ) || !item["id"].isString() ||
           !item.isMember( "rect_mm" ) || !item["rect_mm"].isArray() ||
           item["rect_mm"].size() != 4 )
        continue;
      std::string entry = std::string( collection ) + "/" + item["id"].asString() + ":";
      for ( Json::Value::ArrayIndex i = 0; i < 4; ++i )
      {
        // Hundredths as integers: locale-independent and unambiguous
        // (-0.0 and 0.00 collide deterministically).
        entry += std::to_string(
          static_cast<long long>( std::llround( round2( item["rect_mm"][i].asDouble() ) *
                                                 100.0 ) ) ) + ",";
      }
      if ( item.isMember( "page" ) && item["page"].isIntegral() )
        entry += "p" + std::to_string( item["page"].asInt() ) + ",";
      if ( item.isMember( "z_index" ) && item["z_index"].isIntegral() )
        entry += "z" + std::to_string( item["z_index"].asInt() );
      entries.push_back( entry );
    }
  };
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
    addCollection( mapspec::kCollections[c] );
  addCollection( "items" );

  std::sort( entries.begin(), entries.end() );
  QCryptographicHash hash( QCryptographicHash::Sha256 );
  for ( const std::string &entry : entries )
  {
    // Length-prefix each entry so delimiter characters inside ids cannot
    // forge collisions between different byte streams.
    const std::string framed = std::to_string( entry.size() ) + ":" + entry + ";";
    hash.addData( QByteArray( framed.c_str(), static_cast<qsizetype>( framed.size() ) ) );
  }
  return std::string( hash.result().toHex().constData() );
}

} // namespace sicnu::agent::cartography
