// src/agent/mapspec/mapspec_compiler.cpp
#include "mapspec_compiler.h"

#include "../cartography/chart_registry.h"
#include "../cartography/composition.h"
#include "../cartography/design_tokens.h"
#include "../cartography/registry.h"
#include "../layout_tools/layout_service.h"
#include "../workspace_state.h"
#include "mapspec.h"

#include <qgsapplication.h>
#include <qgslayout.h>
#include <qgslayoutitemchart.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitempicture.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayoutmanager.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgslayoutitempage.h>
#include <qgslayoutpagecollection.h>
#include <qgslayoutsize.h>
#include <qgslayoutpoint.h>
#include <qgslayoutatlas.h>
#include <qgslayoutitemmapoverview.h>
#include <qgslayoutitemshape.h>
#include <qgssymbol.h>
#include <qgsfillsymbol.h>
#include <qgsvectorlayer.h>
#include <qgsmaplayer.h>

#include <QDir>
#include <QVariant>
#include <QVector>

#include <algorithm>

namespace sicnu::agent::mapspec {

using sicnu::agent::WorkspaceEntityRegistry;
using sicnu::agent::layout_tools::LayoutService;

namespace {

Json::Value rectToProps( const Json::Value &rect )
{
  Json::Value props( Json::objectValue );
  if ( rect.isArray() && rect.size() == 4 )
  {
    props["x"] = rect[0];
    props["y"] = rect[1];
    props["width"] = rect[2];
    props["height"] = rect[3];
  }
  return props;
}

/// Resolves a MapSpec layer reference (workspace entity id, layer uuid, or
/// layer name) the same way the map-frame property layer would, but upfront
/// so entity ids resolve too.
QString resolveLayerRef( const std::string &ref )
{
  const QString naturalKey =
    WorkspaceEntityRegistry::instance().naturalKeyFor( QString::fromStdString( ref ) );
  return naturalKey.isEmpty() ? QString::fromStdString( ref ) : naturalKey;
}

/// Adds one item through LayoutService; returns the item or null.
QgsLayoutItem *compileItem( QgsPrintLayout *layout, const QString &type, const std::string &itemId,
                            Json::Value props, QString *error )
{
  props["name"] = itemId;
  return LayoutService::instance().addItem( layout, type, props, error );
}

/// Materializes token-based text styling for a label-ish item. Explicit item
/// font fields win over the token set (ADR 0130 precedence chain).
void applyTokenTextStyle( Json::Value &props, const Json::Value &item, const Json::Value &tokens,
                          const std::string &style, double fallbackPt )
{
  using sicnu::agent::cartography::tokenString;
  using sicnu::agent::cartography::tokenTextStyle;
  using sicnu::agent::cartography::tokenValue;
  const Json::Value tokenStyle = tokenTextStyle( tokens, style, fallbackPt );
  double fontPt = tokenStyle["size_pt"].asDouble();
  bool bold = tokenStyle.get( "weight", "normal" ).asString() == "bold";
  if ( item.isMember( "font" ) && item["font"].isObject() )
  {
    const Json::Value &font = item["font"];
    if ( font.isMember( "size_pt" ) && font["size_pt"].isNumeric() )
      fontPt = font["size_pt"].asDouble();
    if ( font.isMember( "bold" ) && font["bold"].isBool() )
      bold = font["bold"].asBool();
    if ( font.isMember( "color" ) && font["color"].isString() )
      props["color"] = font["color"];
  }
  // Style-level token color: a color-name reference into colors.*.
  if ( !props.isMember( "color" ) )
  {
    const std::string colorName = tokenString( tokenStyle, "color" );
    if ( !colorName.empty() )
    {
      const Json::Value color = tokenValue( tokens, "colors." + colorName );
      if ( color.isString() )
        props["color"] = color;
    }
  }
  props["font_size"] = fontPt;
  props["bold"] = bold;
}

/// Applies the token font family to a compiled label item (QGIS has no
/// label font-family property in LayoutService's surface; QFont handles
/// CJK degradation through the registered substitutions).
void applyTokenFontFamily( QgsLayoutItem *item, const Json::Value &tokens )
{
  auto *label = qobject_cast<QgsLayoutItemLabel *>( item );
  if ( !label )
    return;
  const std::string family =
    sicnu::agent::cartography::tokenString( tokens, "typography.font_family" );
  if ( family.empty() )
    return;
  QFont font = label->font();
  font.setFamily( QString::fromStdString( family ) );
  label->setFont( font );
}

/// compileItem + token font family for the label-backed collections.
QgsLayoutItem *compileTextItem( QgsPrintLayout *layout, const QString &type,
                                const std::string &itemId, Json::Value props,
                                const Json::Value &tokens )
{
  QgsLayoutItem *item = compileItem( layout, type, itemId, props, nullptr );
  applyTokenFontFamily( item, tokens );
  return item;
}

/// Derived furniture (locator caption/connector, nodata swatch/label) is
/// positioned in the coordinates of its PARENT's page. The compiler places
/// it at parent-rect-relative offsets — page-relative for the parent's page
/// — and the v2 post-pass only moves items whose ids appear in the spec
/// (derived ids do not), so a parent declared on page > 0 needs the same
/// attemptMove the post-pass applies, keeping the item's own coordinates.
void placeWithParentPage( QgsLayoutItem *item, const Json::Value &parent )
{
  if ( !item || !parent.isObject() )
    return;
  const int pageIndex =
    parent.isMember( "page" ) && parent["page"].isIntegral() ? parent["page"].asInt() : 0;
  if ( pageIndex <= 0 )
    return;
  item->attemptMove( item->pagePositionWithUnits(), true, false, pageIndex );
}

} // namespace

QgsPrintLayout *MapSpecCompiler::compile( const Json::Value &specIn, QString *error )
{
  // Mutable working copy (the input document is never modified). v3
  // conditional fields resolve first (when the caller stamped a
  // condition_context), then validation runs on what will actually compile.
  Json::Value spec = specIn;
  if ( spec.isObject() && spec.isMember( "condition_context" ) )
  {
    std::vector<std::string> conditionErrors;
    resolveMapSpecConditions( spec, spec["condition_context"], &conditionErrors );
    // Evaluation errors are advisory: unevaluable conditions kept their
    // content, and the caller's preflight reports them.
    Q_UNUSED( conditionErrors );
  }
  const auto problems = validateMapSpec( spec );
  if ( !problems.empty() )
  {
    if ( error )
      *error = QStringLiteral( "invalid MapSpec: %1" ).arg( QString::fromStdString( problems.front() ) );
    return nullptr;
  }
  QgsPrintLayout *existing =
    LayoutService::instance().findLayout( QString::fromStdString( spec["layout_name"].asString() ) );
  if ( existing )
    LayoutService::instance().deleteLayout( QString::fromStdString( spec["layout_name"].asString() ) );

  QgsPrintLayout *layout = LayoutService::instance().createLayout(
    QString::fromStdString( spec["layout_name"].asString() ), QStringLiteral( "A4" ),
    /*landscape=*/true, error );
  if ( !layout )
    return nullptr;

  // Explicit page size from the spec (LayoutService presets are fixed sizes).
  const Json::Value &page = spec["page"];
  if ( QgsLayoutItemPage *pageItem = layout->pageCollection()->page( 0 ) )
  {
    layout->pageCollection()->beginPageSizeChange();
    pageItem->setPageSize( QgsLayoutSize( page["width_mm"].asDouble(), page["height_mm"].asDouble(),
                                          Qgis::LayoutUnit::Millimeters ) );
    layout->pageCollection()->endPageSizeChange();
  }

  // --- v2 multi-page: additional declared pages ------------------------------
  if ( spec.isMember( "pages" ) && spec["pages"].isArray() )
  {
    for ( const auto &pageSpec : spec["pages"] )
    {
      auto *extraPage = new QgsLayoutItemPage( layout );
      extraPage->setPageSize( QgsLayoutSize( pageSpec["width_mm"].asDouble(),
                                             pageSpec["height_mm"].asDouble(),
                                             Qgis::LayoutUnit::Millimeters ) );
      layout->pageCollection()->addPage( extraPage );
    }
  }

  // --- v2/v3 atlas hook --------------------------------------------------------
  double atlasMarginFraction = -1.0;
  bool atlasEnabled = false;
  if ( page.isMember( "atlas" ) && page["atlas"].isObject() )
  {
    const Json::Value &atlasSpec = page["atlas"];
    if ( QgsLayoutAtlas *atlas = layout->atlas() )
    {
      if ( atlasSpec.isMember( "enabled" ) && atlasSpec["enabled"].isBool() )
      {
        atlas->setEnabled( atlasSpec["enabled"].asBool() );
        atlasEnabled = atlasSpec["enabled"].asBool();
      }
      if ( atlasSpec.isMember( "coverage_layer" ) && atlasSpec["coverage_layer"].isString() )
      {
        const QString layerRef = QString::fromStdString( atlasSpec["coverage_layer"].asString() );
        const QString naturalKey =
          WorkspaceEntityRegistry::instance().naturalKeyFor( layerRef );
        QgsMapLayer *layer = nullptr;
        if ( QgsProject *project = QgsProject::instance() )
        {
          const QString key = naturalKey.isEmpty() ? layerRef : naturalKey;
          const QList<QgsMapLayer *> matches = project->mapLayersByName( key );
          if ( !matches.isEmpty() )
            layer = matches.first();
          else
            layer = project->mapLayer( key );
        }
        if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
          atlas->setCoverageLayer( vector );
      }
      if ( atlasSpec.isMember( "filename_expression" ) && atlasSpec["filename_expression"].isString() )
      {
        QString expressionError;
        atlas->setFilenameExpression(
          QString::fromStdString( atlasSpec["filename_expression"].asString() ), expressionError );
      }
      // v3 atlas surface: filter, sort, per-feature margin fraction.
      if ( atlasSpec.isMember( "filter" ) && atlasSpec["filter"].isString() &&
           !atlasSpec["filter"].asString().empty() )
      {
        QString filterError;
        if ( atlas->setFilterExpression(
               QString::fromStdString( atlasSpec["filter"].asString() ), filterError ) )
          atlas->setFilterFeatures( true );
      }
      std::string sortExpression;
      if ( atlasSpec.isMember( "sort_expression" ) && atlasSpec["sort_expression"].isString() )
        sortExpression = atlasSpec["sort_expression"].asString();
      else if ( atlasSpec.isMember( "sort_by" ) && atlasSpec["sort_by"].isString() )
        sortExpression = atlasSpec["sort_by"].asString();
      if ( !sortExpression.empty() )
      {
        atlas->setSortExpression( QString::fromStdString( sortExpression ) );
        atlas->setSortFeatures( true );
        atlas->setSortAscending(
          !( atlasSpec.isMember( "sort_order" ) && atlasSpec["sort_order"].isString() &&
             atlasSpec["sort_order"].asString() == "desc" ) );
      }
      if ( atlasSpec.isMember( "margin_fraction" ) && atlasSpec["margin_fraction"].isNumeric() )
      {
        // Applied per map item below (QGIS keeps the atlas margin on the map).
        atlasMarginFraction = atlasSpec["margin_fraction"].asDouble();
      }
    }
  }

  // --- design tokens (ADR 0130): resolved once, applied as defaults --------
  const Json::Value tokens = sicnu::agent::cartography::resolveTokenSet( spec );
  sicnu::agent::cartography::applyTokenFontFallbacks( tokens );

  // --- component defaults (Design System 4.0): source_component references
  // are resolved and merged under the items' explicit fields; unknown
  // references are advisory (preflight reports them) and never fatal.
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( Json::Value::ArrayIndex i = 0; i < spec[collection].size(); ++i )
    {
      Json::Value &item = spec[collection][i];
      if ( item.isObject() && item.isMember( "source_component" ) )
        sicnu::agent::cartography::applyComponentDefaults( item, nullptr );
    }
  }

  // --- composition solver (ADR 0131): anchors, size bounds, constraints ----
  sicnu::agent::cartography::resolveComposition(
    spec, sicnu::agent::cartography::tokenNumber( tokens, "spacing.margin_mm", 12.0 ) );

  const auto itemsOf = [ &spec ]( const char *collection ) -> Json::Value {
    return spec.get( collection, Json::Value( Json::arrayValue ) );
  };
  QString itemError;

  // --- map frames first (legends/scale bars link to them) ------------------
  std::vector<std::string> mapFrameIds;
  for ( const auto &frame : itemsOf( "map_frames" ) )
  {
    Json::Value props = rectToProps( frame["rect_mm"] );
    if ( frame.isMember( "extent" ) && frame["extent"].isArray() && frame["extent"].size() == 4 )
      props["extent"] = frame["extent"];
    if ( frame.isMember( "scale" ) && frame["scale"].isNumeric() )
      props["scale"] = frame["scale"];
    if ( frame.isMember( "rotation" ) && frame["rotation"].isNumeric() )
      props["map_rotation"] = frame["rotation"];
    if ( frame.isMember( "layers" ) && frame["layers"].isArray() )
    {
      Json::Value layerRefs( Json::arrayValue );
      for ( const auto &ref : frame["layers"] )
        if ( ref.isString() )
          layerRefs.append( resolveLayerRef( ref.asString() ).toStdString() );
      props["layers"] = layerRefs;
    }
    const std::string id = frame["id"].asString();
    QgsLayoutItem *frameItem = compileItem( layout, "map", id, props, &itemError );
    if ( !frameItem )
    {
      if ( error )
        *error = QStringLiteral( "map frame '%1': %2" ).arg( QString::fromStdString( id ), itemError );
      // Never leave a half-built layout registered: the previous layout was
      // already replaced, so a failed compile yields no layout at all.
      LayoutService::instance().deleteLayout( QString::fromStdString( spec["layout_name"].asString() ) );
      return nullptr;
    }
    if ( auto *compiledMap = qobject_cast<QgsLayoutItemMap *>( frameItem ) )
    {
      // QGIS only iterates feature extents for maps marked atlas-driven —
      // without this every atlas page renders the same extent.
      if ( atlasEnabled )
        compiledMap->setAtlasDriven( true );
      if ( atlasMarginFraction >= 0.0 )
        compiledMap->setAtlasMargin( atlasMarginFraction );
    }
    mapFrameIds.push_back( id );
  }

  // --- inset maps compile as secondary map frames --------------------------
  // Locator semantics (extent outline of the main map) are drawn at render
  // time by referencing the same layers; the inset carries its own extent.
  for ( const auto &inset : itemsOf( "inset_maps" ) )
  {
    Json::Value props = rectToProps( inset["rect_mm"] );
    // Inset extent: explicit, else inherit the referenced (or first) frame's.
    if ( inset.isMember( "extent" ) && inset["extent"].isArray() && inset["extent"].size() == 4 )
    {
      props["extent"] = inset["extent"];
    }
    else
    {
      const Json::Value frames = itemsOf( "map_frames" );
      const Json::Value *source = nullptr;
      if ( inset.isMember( "map_ref" ) && inset["map_ref"].isString() )
      {
        const std::string ref = inset["map_ref"].asString();
        for ( const auto &frame : frames )
          if ( frame.isObject() && frame.isMember( "id" ) && frame["id"].asString() == ref )
            source = &frame;
      }
      if ( !source && frames.isArray() && !frames.empty() )
        source = &frames[0];
      if ( source && ( *source ).isMember( "extent" ) )
        props["extent"] = ( *source )["extent"];
    }
    if ( inset.isMember( "layers" ) && inset["layers"].isArray() )
    {
      Json::Value layerRefs( Json::arrayValue );
      for ( const auto &ref : inset["layers"] )
        if ( ref.isString() )
          layerRefs.append( resolveLayerRef( ref.asString() ).toStdString() );
      props["layers"] = layerRefs;
    }
    const std::string id = inset["id"].asString();
    if ( !compileItem( layout, "map", id, props, &itemError ) )
    {
      if ( error )
        *error = QStringLiteral( "inset map '%1': %2" ).arg( QString::fromStdString( id ), itemError );
      LayoutService::instance().deleteLayout( QString::fromStdString( spec["layout_name"].asString() ) );
      return nullptr;
    }

    // v3 locator extent indicator: the inset draws the referenced frame's
    // extent through the QGIS-native overview mechanism (a real layout
    // primitive — no screenshot hacks).
    if ( inset.isMember( "locator" ) && inset["locator"].isObject() )
    {
      const Json::Value &locator = inset["locator"];
      if ( locator.isMember( "target" ) && locator["target"].isString() )
      {
        auto *insetMap = qobject_cast<QgsLayoutItemMap *>(
          LayoutService::instance().findItem( layout, QString::fromStdString( id ) ) );
        auto *targetMap = qobject_cast<QgsLayoutItemMap *>(
          LayoutService::instance().findItem( layout,
                                              QString::fromStdString( locator["target"].asString() ) ) );
        if ( insetMap && targetMap )
        {
          const std::string style =
            locator.isMember( "style" ) && locator["style"].isString() ? locator["style"].asString()
                                                                      : "outline";
          auto *overview = new QgsLayoutItemMapOverview(
            QStringLiteral( "locator-%1" ).arg( QString::fromStdString( id ) ), insetMap );
          overview->setLinkedMap( targetMap );
          overview->setEnabled( true );
          if ( style == "region" )
          {
            // Region highlight: translucent accent fill over the extent.
            QVariantMap regionProps;
            regionProps[QStringLiteral( "color" )] = QStringLiteral( "#0072b2" );
            regionProps[QStringLiteral( "style" )] = QStringLiteral( "solid" );
            regionProps[QStringLiteral( "outline_color" )] = QStringLiteral( "#004488" );
            regionProps[QStringLiteral( "outline_width" )] = QStringLiteral( "0.4" );
            regionProps[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
            overview->setFrameSymbol(
              QgsFillSymbol::createSimple( regionProps ).release() );
            overview->setBlendMode( QPainter::CompositionMode_SourceOver );
          }
          else if ( style == "frame" )
          {
            // Transparent fill, heavier stroke only.
            QVariantMap frameProps;
            frameProps[QStringLiteral( "style" )] = QStringLiteral( "no" );
            frameProps[QStringLiteral( "outline_color" )] = QStringLiteral( "#222222" );
            const double stroke = locator.isMember( "stroke_mm" ) && locator["stroke_mm"].isNumeric()
                                    ? locator["stroke_mm"].asDouble()
                                    : 0.8;
            frameProps[QStringLiteral( "outline_width" )] = QString::number( stroke );
            frameProps[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
            overview->setFrameSymbol( QgsFillSymbol::createSimple( frameProps ).release() );
          }
          else
          {
            // Default QGIS overview frame: inverted shading outside the extent.
            const bool inverted = !( locator.isMember( "inverted" ) && locator["inverted"].isBool() &&
                                     !locator["inverted"].asBool() );
            overview->setInverted( inverted );
            const double stroke = locator.isMember( "stroke_mm" ) && locator["stroke_mm"].isNumeric()
                                    ? locator["stroke_mm"].asDouble()
                                    : 0.4;
            QVariantMap outlineProps;
            outlineProps[QStringLiteral( "style" )] = QStringLiteral( "no" );
            outlineProps[QStringLiteral( "outline_color" )] = QStringLiteral( "#333333" );
            outlineProps[QStringLiteral( "outline_width" )] = QString::number( stroke );
            outlineProps[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
            overview->setFrameSymbol( QgsFillSymbol::createSimple( outlineProps ).release() );
          }
          insetMap->overviews()->addOverview( overview );
          insetMap->update();
          if ( locator.isMember( "label" ) && locator["label"].isString() &&
               inset.isMember( "rect_mm" ) && inset["rect_mm"].isArray() &&
               inset["rect_mm"].size() == 4 )
          {
            // Caption under the inset (label size from tokens unless given).
            Json::Value labelProps( Json::objectValue );
            labelProps["x"] = inset["rect_mm"][0];
            labelProps["y"] = inset["rect_mm"][1].asDouble() + inset["rect_mm"][3].asDouble() + 1.0;
            labelProps["width"] = inset["rect_mm"][2];
            labelProps["height"] = 6.0;
            labelProps["text"] = locator["label"];
            if ( locator.isMember( "label_size_pt" ) && locator["label_size_pt"].isNumeric() )
              labelProps["font_size"] = locator["label_size_pt"];
            else
              applyTokenTextStyle( labelProps, locator, tokens, "caption", 8.0 );
            if ( QgsLayoutItem *caption = compileTextItem( layout, "label", id + "-locator-label",
                                                           labelProps, tokens ) )
              placeWithParentPage( caption, inset );
          }

          // Platform 8.0 connector graphics: a QGIS-native polyline from the
          // inset frame edge to the target frame's projected extent anchor —
          // the classic locator relationship line. Geometry is a pure
          // function of the declared page rects/extents (deterministic).
          if ( locator.isMember( "connector" ) && locator["connector"].isObject() )
          {
            const Json::Value &connector = locator["connector"];
            const Json::Value insetRect = inset["rect_mm"];
            Json::Value targetRect;
            Json::Value targetExtent;
            const std::string targetId = locator["target"].asString();
            for ( const char *collection : { "map_frames", "inset_maps" } )
            {
              for ( const auto &candidate : itemsOf( collection ) )
              {
                if ( candidate.isObject() && candidate.isMember( "id" ) &&
                     candidate["id"].asString() == targetId )
                {
                  if ( candidate.isMember( "rect_mm" ) && candidate["rect_mm"].isArray() &&
                       candidate["rect_mm"].size() == 4 )
                    targetRect = candidate["rect_mm"];
                  if ( candidate.isMember( "extent" ) && candidate["extent"].isArray() &&
                       candidate["extent"].size() == 4 )
                    targetExtent = candidate["extent"];
                }
              }
            }
            if ( insetRect.isArray() && insetRect.size() == 4 && targetRect.isArray() &&
                 targetRect.size() == 4 )
            {
              const double insetCenterX = insetRect[0].asDouble() + insetRect[2].asDouble() / 2.0;
              const double insetCenterY = insetRect[1].asDouble() + insetRect[3].asDouble() / 2.0;
              // Anchor: where the inset extent center falls inside the target
              // frame (linear extent→page mapping, clamped to the frame).
              // Without resolvable extents the anchor is the frame center.
              double anchorX = targetRect[0].asDouble() + targetRect[2].asDouble() / 2.0;
              double anchorY = targetRect[1].asDouble() + targetRect[3].asDouble() / 2.0;
              const Json::Value insetExtent = props.isMember( "extent" ) ? props["extent"] : Json::Value();
              if ( insetExtent.isArray() && insetExtent.size() == 4 && targetExtent.isArray() &&
                   targetExtent.size() == 4 )
              {
                const double targetW = targetExtent[2].asDouble() - targetExtent[0].asDouble();
                const double targetH = targetExtent[3].asDouble() - targetExtent[1].asDouble();
                if ( targetW > 1e-12 && targetH > 1e-12 )
                {
                  // Extents are [xmin, ymin, xmax, ymax]: center = (min + max) / 2.
                  const double insetCenterMapX =
                    ( insetExtent[0].asDouble() + insetExtent[2].asDouble() ) / 2.0;
                  const double insetCenterMapY =
                    ( insetExtent[1].asDouble() + insetExtent[3].asDouble() ) / 2.0;
                  // North-up rendering puts ymin at the BOTTOM edge of the
                  // frame (larger page y): page-y grows as the map y
                  // DECREASES, hence the (ymax - mapY) numerator.
                  anchorX = targetRect[0].asDouble() +
                            ( insetCenterMapX - targetExtent[0].asDouble() ) / targetW *
                                targetRect[2].asDouble();
                  anchorY = targetRect[1].asDouble() +
                            ( targetExtent[3].asDouble() - insetCenterMapY ) / targetH *
                                targetRect[3].asDouble();
                  anchorX = std::max( targetRect[0].asDouble(),
                                      std::min( anchorX, targetRect[0].asDouble() +
                                                             targetRect[2].asDouble() ) );
                  anchorY = std::max( targetRect[1].asDouble(),
                                      std::min( anchorY, targetRect[1].asDouble() +
                                                             targetRect[3].asDouble() ) );
                }
              }
              // Start point: where the center→anchor ray leaves the inset rect.
              const double dx = anchorX - insetCenterX;
              const double dy = anchorY - insetCenterY;
              const double halfW = insetRect[2].asDouble() / 2.0;
              const double halfH = insetRect[3].asDouble() / 2.0;
              const bool leavesInset =
                std::abs( dx ) > 1e-9 || std::abs( dy ) > 1e-9;
              const double denomX = std::abs( dx ) > 1e-9 ? std::abs( dx ) : 1e-9;
              const double denomY = std::abs( dy ) > 1e-9 ? std::abs( dy ) : 1e-9;
              const double scale = std::min( halfW / denomX, halfH / denomY );
              if ( leavesInset )
              {
                // Classic locator practice: no leader when the projected
                // anchor lies inside the inset footprint (the line would
                // double back over the inset map).
                Json::Value points( Json::arrayValue );
                Json::Value start( Json::arrayValue );
                start.append( insetCenterX + dx * scale );
                start.append( insetCenterY + dy * scale );
                Json::Value end( Json::arrayValue );
                end.append( anchorX );
                end.append( anchorY );
                points.append( start );
                points.append( end );
                Json::Value lineProps( Json::objectValue );
                lineProps["points"] = points;
                lineProps["color"] =
                  connector.isMember( "color" ) && connector["color"].isString()
                    ? connector["color"]
                    : Json::Value( "#333333" );
                lineProps["width_mm"] =
                  connector.isMember( "stroke_mm" ) && connector["stroke_mm"].isNumeric()
                    ? connector["stroke_mm"]
                    : Json::Value( 0.4 );
                lineProps["line_style"] =
                  connector.isMember( "style" ) && connector["style"].isString() &&
                      connector["style"].asString() == "dash"
                    ? Json::Value( "dash" )
                    : Json::Value( "solid" );
                QgsLayoutItem *connectorItem =
                  compileItem( layout, "line", id + "-locator-connector", lineProps, &itemError );
                if ( !connectorItem )
                {
                  // A declared relationship graphic that cannot materialize
                  // is a compile failure, like an inset that cannot compile —
                  // never a silently absent feature.
                  if ( error )
                    *error = QStringLiteral( "inset map '%1': connector: %2" )
                               .arg( QString::fromStdString( id ), itemError );
                  LayoutService::instance().deleteLayout(
                    QString::fromStdString( spec["layout_name"].asString() ) );
                  return nullptr;
                }
                placeWithParentPage( connectorItem, inset );
              }
            }
          }
        }
      }
    }
  }

  // --- grids attach to map frames ------------------------------------------
  for ( const auto &grid : itemsOf( "grids" ) )
  {
    QgsLayoutItem *frameItem =
      grid.isMember( "map_ref" )
        ? LayoutService::instance().findItem( layout, QString::fromStdString( grid["map_ref"].asString() ) )
        : nullptr;
    auto *map = qobject_cast<QgsLayoutItemMap *>( frameItem );
    if ( !map || map->grid() == nullptr )
      continue;
    if ( grid.isMember( "interval" ) && grid["interval"].isNumeric() )
    {
      map->grid()->setIntervalX( grid["interval"].asDouble() );
      map->grid()->setIntervalY( grid["interval"].asDouble() );
    }
    map->grid()->setEnabled( true );
  }

  // --- text furniture (token-styled; item font overrides tokens) -----------
  for ( const auto &title : itemsOf( "titles" ) )
  {
    Json::Value props = rectToProps( title["rect_mm"] );
    props["text"] = title.get( "text", "" );
    applyTokenTextStyle( props, title, tokens, "title", 18.0 );
    compileTextItem( layout, "title", title["id"].asString(), props, tokens );
  }
  for ( const auto &label : itemsOf( "labels" ) )
  {
    Json::Value props = rectToProps( label["rect_mm"] );
    props["text"] = label.get( "text", "" );
    applyTokenTextStyle( props, label, tokens, "body", 9.0 );
    compileTextItem( layout, "label", label["id"].asString(), props, tokens );
  }
  for ( const auto &note : itemsOf( "source_notes" ) )
  {
    Json::Value props = rectToProps( note["rect_mm"] );
    props["text"] = note.get( "text", "" );
    applyTokenTextStyle( props, note, tokens, "source_note", 7.0 );
    compileTextItem( layout, "label", note["id"].asString(), props, tokens );
  }
  for ( const auto &annotation : itemsOf( "annotations" ) )
  {
    if ( annotation.isMember( "qgis_type" ) )
      continue; // extracted placeholder for a non-mappable QGIS item — leave it
    Json::Value props = rectToProps( annotation["rect_mm"] );
    props["text"] = annotation.get( "text", "" );
    applyTokenTextStyle( props, annotation, tokens, "annotation", 8.0 );
    compileTextItem( layout, "label", annotation["id"].asString(), props, tokens );
  }

  // --- legends / scale bars / north arrows (link to map frames) -------------
  for ( const auto &legend : itemsOf( "legends" ) )
  {
    Json::Value props = rectToProps( legend["rect_mm"] );
    if ( legend.isMember( "title" ) )
      props["title"] = legend["title"];
    if ( legend.isMember( "map_ref" ) )
      props["linked_map"] = legend["map_ref"];
    QgsLayoutItem *item = compileItem( layout, "legend", legend["id"].asString(), props, nullptr );
    // Explicit column counts take the legend out of auto-update mode (the
    // agent owns the entries from that point on) — QGIS semantics.
    if ( auto *legendItem = qobject_cast<QgsLayoutItemLegend *>( item ) )
    {
      if ( legend.isMember( "columns" ) && legend["columns"].isIntegral() &&
           legend["columns"].asInt() > 1 )
      {
        // setAutoUpdateModel is the update toggle this QGIS exposes (the
        // renamed setter does not exist here); deprecation is intentional.
        legendItem->setAutoUpdateModel( false );
        legendItem->setColumnCount( legend["columns"].asInt() );
      }
    }
    // Platform 8.0: declared NoData legend entry. QgsLayoutItemLegend cannot
    // host custom nodes declaratively, so the entry compiles as a sanctioned
    // swatch composite (QGIS shape rectangle + label) pinned inside the
    // declared legend rect bottom — the same furniture class as charts and
    // colorbars (QPainter→picture since 4.0).
    if ( legend.isMember( "nodata" ) && legend["nodata"].isObject() &&
         legend.isMember( "rect_mm" ) && legend["rect_mm"].isArray() &&
         legend["rect_mm"].size() == 4 )
    {
      const Json::Value &nodata = legend["nodata"];
      const std::string nodataLabel =
        nodata.isMember( "label" ) && nodata["label"].isString() &&
            !nodata["label"].asString().empty()
          ? nodata["label"].asString()
          : "NoData";
      const Json::Value &r = legend["rect_mm"];
      Json::Value swatchProps( Json::objectValue );
      swatchProps["x"] = r[0].asDouble() + 2.0;
      // Clamp into the legend rect so degenerate (very flat) legends cannot
      // push the swatch above the legend top.
      swatchProps["y"] = std::max( r[1].asDouble(),
                                   r[1].asDouble() + r[3].asDouble() - 6.0 );
      swatchProps["width"] = 4.0;
      swatchProps["height"] = 4.0;
      QgsLayoutItem *swatch = compileItem( layout, "shape", legend["id"].asString() + "-nodata-swatch",
                                           swatchProps, nullptr );
      if ( auto *swatchShape = qobject_cast<QgsLayoutItemShape *>( swatch ) )
      {
        QVariantMap swatchSymbol;
        swatchSymbol[QStringLiteral( "color" )] =
          nodata.isMember( "color" ) && nodata["color"].isString()
            ? QString::fromStdString( nodata["color"].asString() )
            : QStringLiteral( "#f0f0f0" );
        swatchSymbol[QStringLiteral( "outline_color" )] = QStringLiteral( "#666666" );
        swatchSymbol[QStringLiteral( "outline_width" )] = QStringLiteral( "0.2" );
        swatchSymbol[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
        swatchShape->setSymbol( QgsFillSymbol::createSimple( swatchSymbol ).release() );
        swatchShape->update();
        placeWithParentPage( swatchShape, legend );
      }
      Json::Value nodataLabelProps( Json::objectValue );
      nodataLabelProps["x"] = r[0].asDouble() + 8.0;
      nodataLabelProps["y"] = r[1].asDouble() + r[3].asDouble() - 6.0;
      nodataLabelProps["width"] = std::max( 10.0, r[2].asDouble() - 10.0 );
      nodataLabelProps["height"] = 4.0;
      nodataLabelProps["text"] = nodataLabel;
      applyTokenTextStyle( nodataLabelProps, legend, tokens, "caption", 6.0 );
      if ( QgsLayoutItem *swatchLabel =
             compileTextItem( layout, "label", legend["id"].asString() + "-nodata-label",
                              nodataLabelProps, tokens ) )
        placeWithParentPage( swatchLabel, legend );
    }
  }
  for ( const auto &scaleBar : itemsOf( "scale_bars" ) )
  {
    Json::Value props = rectToProps( scaleBar["rect_mm"] );
    if ( scaleBar.isMember( "style" ) && scaleBar["style"].isString() )
      props["style"] = scaleBar["style"];
    if ( scaleBar.isMember( "units" ) && scaleBar["units"].isString() )
      props["unit_label"] = scaleBar["units"];
    if ( scaleBar.isMember( "units_per_segment" ) && scaleBar["units_per_segment"].isNumeric() )
      props["units_per_segment"] = scaleBar["units_per_segment"];
    if ( scaleBar.isMember( "map_ref" ) )
      props["linked_map"] = scaleBar["map_ref"];
    compileItem( layout, "scalebar", scaleBar["id"].asString(), props, nullptr );
  }
  for ( const auto &arrow : itemsOf( "north_arrows" ) )
  {
    Json::Value props = rectToProps( arrow["rect_mm"] );
    if ( arrow.isMember( "svg" ) && arrow["svg"].isString() )
      props["path"] = arrow["svg"];
    if ( arrow.isMember( "north_mode" ) && arrow["north_mode"].isString() )
      props["north_mode"] = arrow["north_mode"];
    if ( arrow.isMember( "map_ref" ) )
      props["linked_map"] = arrow["map_ref"];
    compileItem( layout, "northarrow", arrow["id"].asString(), props, nullptr );
  }

  // --- charts ---------------------------------------------------------------
  // Rendered chart/colorbar PNGs are layout-scoped so two layouts cannot
  // clobber each other's pictures when item ids coincide. Files live in a
  // session temp subdir (the OS tmpdir reaper bounds accumulation).
  const QString tempDirPath = QDir::temp().filePath(
    QStringLiteral( "sicnu-cartography-%1" ).arg( QString::fromStdString( spec["layout_name"].asString() ) ) );
  QDir().mkpath( tempDirPath );
  QString chartError;
  QString chartPath;
  for ( const auto &chartItem : itemsOf( "charts" ) )
  {
    // Token defaults are materialized into a mutable copy; explicit chart
    // fields always win.
    Json::Value chart = chartItem["chart"];
    using sicnu::agent::cartography::tokenBool;
    using sicnu::agent::cartography::tokenNumber;
    using sicnu::agent::cartography::tokenPalette;
    using sicnu::agent::cartography::tokenValue;
    if ( !chart.isMember( "style" ) || !chart["style"].isObject() )
      chart["style"] = Json::Value( Json::objectValue );
    if ( !chart["style"].isMember( "font_pt" ) )
      chart["style"]["font_pt"] = tokenNumber( tokens, "chart.font_pt", 10 );
    if ( !chart["style"].isMember( "show_grid" ) )
      chart["style"]["show_grid"] = tokenBool( tokens, "chart.show_grid", true );
    if ( !chart["style"].isMember( "text_color" ) )
    {
      const Json::Value textColor = tokenValue( tokens, "colors.text" );
      if ( textColor.isString() )
        chart["style"]["text_color"] = textColor;
    }
    // style.palette may name a token palette; resolve it to hex stops.
    if ( chart["style"].isMember( "palette" ) && chart["style"]["palette"].isString() )
    {
      const Json::Value palette = tokenPalette( tokens, chart["style"]["palette"].asString() );
      if ( !palette.empty() )
        chart["style"]["palette"] = palette;
    }
    else if ( !chart["style"].isMember( "palette" ) )
    {
      const Json::Value palette =
        tokenPalette( tokens, sicnu::agent::cartography::tokenString( tokens, "chart.palette" ) );
      if ( !palette.empty() )
        chart["style"]["palette"] = palette;
    }
    Json::Value props = rectToProps( chartItem["rect_mm"] );
    if ( !chartItem.isMember( "rect_mm" ) || !chartItem["rect_mm"].isArray() )
    {
      // Rect-less charts (component defaults carry pixel canvases): place at
      // the bottom-left margin, sized from the token chart canvas.
      const double widthMm = tokenNumber( tokens, "chart.width_px", 480 ) * 25.4 / 96.0;
      const double heightMm = tokenNumber( tokens, "chart.height_px", 320 ) * 25.4 / 96.0;
      props["x"] = sicnu::agent::cartography::tokenNumber( tokens, "spacing.margin_mm", 12.0 );
      props["y"] = page["height_mm"].asDouble() -
                   sicnu::agent::cartography::tokenNumber( tokens, "spacing.margin_mm", 12.0 ) -
                   heightMm;
      props["width"] = widthMm;
      props["height"] = heightMm;
    }
    const std::string mode = chart.isMember( "binding" ) && chart["binding"].isMember( "mode" )
                               ? chart["binding"]["mode"].asString()
                               : "inline";
    if ( mode == "vector_expression" )
    {
      QgsLayoutItem *item = compileItem( layout, "chart", chartItem["id"].asString(), props, nullptr );
      if ( item )
      {
        auto *nativeChart = qobject_cast<QgsLayoutItemChart *>( item );
        if ( nativeChart && !sicnu::agent::cartography::bindNativeChart( nativeChart, chart, &chartError ) )
        {
          // Fall through to the inline path so a bad binding never kills the
          // whole composition; the placeholder documents the failure.
          props["text_placeholder"] = chartError.toStdString();
          QgsLayoutItem *fallback =
            compileItem( layout, "picture", chartItem["id"].asString() + "-placeholder", props, nullptr );
          Q_UNUSED( fallback );
        }
      }
    }
    else
    {
      // Inline charts render through the QPainter path into a stable session
      // file, then land as picture items.
      chartPath = QDir( tempDirPath ).filePath(
        QStringLiteral( "sicnu-chart-%1-%2.png" )
          .arg( QString::fromStdString( spec["layout_name"].asString() ),
                QString::fromStdString( chartItem["id"].asString() ) ) );
      if ( sicnu::agent::cartography::renderChartToFile( chart, chartPath, &chartError ) )
      {
        props["path"] = chartPath.toStdString();
        compileItem( layout, "picture", chartItem["id"].asString(), props, nullptr );
      }
      else
      {
        Json::Value textProps = rectToProps( chartItem["rect_mm"] );
        textProps["text"] = "[chart error: " + chartError.toStdString() + "]";
        compileItem( layout, "label", chartItem["id"].asString(), textProps, nullptr );
      }
    }
  }

  // --- colorbars --------------------------------------------------------------
  for ( const auto &colorbarItem : itemsOf( "colorbars" ) )
  {
    // Ramp names resolve through the token palette table when available;
    // explicit `colors` stops always win.
    Json::Value colorbar = colorbarItem;
    if ( !colorbar.isMember( "colors" ) || !colorbar["colors"].isArray() )
    {
      const std::string ramp = colorbar.isMember( "ramp" ) && colorbar["ramp"].isString()
                                 ? colorbar["ramp"].asString()
                                 : "sequential";
      const Json::Value palette =
        sicnu::agent::cartography::tokenValue( tokens, "palettes." + ramp );
      if ( palette.isArray() && palette.size() >= 2 )
        colorbar["colors"] = palette;
    }
    if ( !colorbar.isMember( "text_color" ) )
    {
      const Json::Value textColor = sicnu::agent::cartography::tokenValue( tokens, "colors.text" );
      if ( textColor.isString() )
        colorbar["text_color"] = textColor;
    }
    if ( !colorbar.isMember( "font_pt" ) )
      colorbar["font_pt"] = sicnu::agent::cartography::tokenNumber( tokens,
        "typography.styles.caption.size_pt", 8.0 );
    const QString path = QDir( tempDirPath ).filePath(
      QStringLiteral( "sicnu-colorbar-%1-%2.png" )
        .arg( QString::fromStdString( spec["layout_name"].asString() ),
              QString::fromStdString( colorbar["id"].asString() ) ) );
    if ( sicnu::agent::cartography::renderColorbarToFile( colorbar, path ) )
    {
      Json::Value props = rectToProps( colorbar["rect_mm"] );
      if ( !colorbar.isMember( "rect_mm" ) || !colorbar["rect_mm"].isArray() )
      {
        // Rect-less colorbars: bottom margin strip sized from descriptor
        // defaults merged into the item, placed inside the token margin.
        const double widthMm = colorbar.isMember( "width_mm" ) && colorbar["width_mm"].isNumeric()
                                 ? colorbar["width_mm"].asDouble()
                                 : 60.0;
        const double heightMm = colorbar.isMember( "height_mm" ) && colorbar["height_mm"].isNumeric()
                                  ? colorbar["height_mm"].asDouble()
                                  : 8.0;
        props["x"] = sicnu::agent::cartography::tokenNumber( tokens, "spacing.margin_mm", 12.0 );
        props["y"] = page["height_mm"].asDouble() - heightMm -
                     sicnu::agent::cartography::tokenNumber( tokens, "spacing.margin_mm", 12.0 );
        props["width"] = widthMm;
        props["height"] = heightMm;
      }
      props["path"] = path.toStdString();
      compileItem( layout, "picture", colorbar["id"].asString(), props, nullptr );
    }
  }

  // --- v2 post-pass: page placement, z-order, semantic-role stamping --------
  for ( int c = 0; c < mapspec::kCollectionCount; ++c )
  {
    const char *collection = mapspec::kCollections[c];
    if ( !spec.isMember( collection ) || !spec[collection].isArray() )
      continue;
    for ( const auto &item : spec[collection] )
    {
      if ( !item.isObject() || !item.isMember( "id" ) )
        continue;
      const int pageIndex =
        item.isMember( "page" ) && item["page"].isIntegral() ? item["page"].asInt() : 0;
      const bool hasZ = item.isMember( "z_index" ) && item["z_index"].isIntegral();
      const bool hasRole = item.isMember( "semantic_role" ) && item["semantic_role"].isString();
      if ( pageIndex <= 0 && !hasZ && !hasRole )
        continue;
      QgsLayoutItem *compiled =
        LayoutService::instance().findItem( layout, QString::fromStdString( item["id"].asString() ) );
      if ( !compiled )
        continue;
      if ( pageIndex > 0 )
      {
        double x = compiled->pagePos().x();
        double y = compiled->pagePos().y();
        if ( item.isMember( "rect_mm" ) && item["rect_mm"].isArray() && item["rect_mm"].size() == 4 )
        {
          x = item["rect_mm"][0].asDouble();
          y = item["rect_mm"][1].asDouble();
        }
        compiled->attemptMove( QgsLayoutPoint( x, y, Qgis::LayoutUnit::Millimeters ), true, false,
                               pageIndex );
      }
      if ( hasZ )
        compiled->setZValue( item["z_index"].asInt() );
      if ( hasRole )
        compiled->setProperty( "mapspec_role",
                               QVariant( QString::fromStdString( item["semantic_role"].asString() ) ) );
    }
  }

  return layout;
}

Json::Value MapSpecCompiler::extract( QgsPrintLayout *layout )
{
  Json::Value spec = makeMapSpec( layout ? layout->name().toStdString() : "",
                                 Json::Value() );
  if ( !layout )
    return spec;
  if ( QgsLayoutItemPage *page = layout->pageCollection()->page( 0 ) )
  {
    spec["page"]["width_mm"] = page->pageSize().width();
    spec["page"]["height_mm"] = page->pageSize().height();
  }

  const auto classifyLabel = []( const QgsLayoutItemLabel *label ) -> const char * {
    // Stamped semantic roles (compile-time custom property) are authoritative;
    // the geometric heuristic only serves foreign labels.
    const QString role = label->property( "mapspec_role" ).toString();
    if ( role == QLatin1String( "source.primary" ) )
      return "source_notes";
    if ( role.startsWith( QLatin1String( "title." ) ) )
      return "titles";
    if ( role.startsWith( QLatin1String( "text." ) ) )
      return "labels";
    if ( role.startsWith( QLatin1String( "annotation." ) ) )
      return "annotations";
    const QString text = label->text().toLower();
    if ( text.contains( QLatin1String( "source" ) ) ||
         label->text().contains( QStringLiteral( "来源" ) ) )
      return "source_notes";
    if ( label->font().pointSizeF() >= 16.0 )
      return "titles";
    return "labels";
  };

  const QList<QGraphicsItem *> sceneItems = layout->items();
  for ( QGraphicsItem *sceneItem : sceneItems )
  {
    auto *item = dynamic_cast<QgsLayoutItem *>( sceneItem );
    if ( !item || item->type() == QgsLayoutItemRegistry::LayoutPage )
      continue;

    Json::Value entry( Json::objectValue );
    const QString name = item->id().isEmpty() ? QStringLiteral( "extracted-%1" ).arg( item->type() )
                                              : item->id();
    const QRectF rect = item->mapToScene( item->rect() ).boundingRect();
    Json::Value rectJson( Json::arrayValue );
    rectJson.append( rect.x() );
    rectJson.append( rect.y() );
    rectJson.append( rect.width() );
    rectJson.append( rect.height() );
    entry["rect_mm"] = rectJson;
    const QString stampedRole = item->property( "mapspec_role" ).toString();
    if ( !stampedRole.isEmpty() )
      entry["semantic_role"] = stampedRole.toStdString();

    const char *collection = "annotations";
    if ( auto *map = qobject_cast<QgsLayoutItemMap *>( item ) )
    {
      Q_UNUSED( map );
      collection = "map_frames";
      if ( map->extent().isValid() )
      {
        Json::Value extent( Json::arrayValue );
        extent.append( map->extent().xMinimum() );
        extent.append( map->extent().yMinimum() );
        extent.append( map->extent().xMaximum() );
        extent.append( map->extent().yMaximum() );
        entry["extent"] = extent;
      }
    }
    else if ( qobject_cast<QgsLayoutItemLabel *>( item ) )
    {
      collection = classifyLabel( qobject_cast<QgsLayoutItemLabel *>( item ) );
      entry["text"] = qobject_cast<QgsLayoutItemLabel *>( item )->text().toStdString();
    }
    else if ( auto *legend = qobject_cast<QgsLayoutItemLegend *>( item ) )
    {
      collection = "legends";
      entry["title"] = legend->title().toStdString();
      if ( legend->linkedMap() && !legend->linkedMap()->id().isEmpty() )
        entry["map_ref"] = legend->linkedMap()->id().toStdString();
    }
    else if ( qobject_cast<QgsLayoutItemScaleBar *>( item ) )
    {
      collection = "scale_bars";
    }
    else if ( auto *picture = qobject_cast<QgsLayoutItemPicture *>( item ) )
    {
      collection = picture->picturePath().contains( QLatin1String( "north_arrows" ) )
                     ? "north_arrows"
                     : "annotations";
      if ( std::string( collection ) == "annotations" )
        entry["qgis_type"] = "picture";
      if ( picture->linkedMap() )
        entry["map_ref"] = picture->linkedMap()->id().toStdString();
    }
    else if ( qobject_cast<QgsLayoutItemChart *>( item ) )
    {
      collection = "charts";
      entry["chart"] = Json::Value( Json::objectValue );
      entry["chart"]["kind"] = "native";
      entry["chart"]["binding"]["mode"] = "qgis_layout_item";
    }
    else
    {
      entry["qgis_type"] = sicnu::agent::layout_tools::LayoutService::itemTypeToString( item )
                             .toStdString();
    }

    entry["id"] = name.toStdString();
    // Extraction merges into the right collection, avoiding id clashes.
    if ( findMapSpecItem( spec, entry["id"].asString() ).isNull() )
      spec[collection].append( entry );
  }
  return spec;
}

Json::Value MapSpecCompiler::compileAndAssess( const Json::Value &spec, QString *error )
{
  Json::Value out( Json::objectValue );
  QgsPrintLayout *layout = compile( spec, error );
  if ( !layout )
  {
    out["compiled"] = false;
    out["error"] = error ? error->toStdString() : "";
    return out;
  }
  out["compiled"] = true;
  out["layout_name"] = spec["layout_name"].asString();
  out["layout_id"] = sicnu::agent::WorkspaceEntityRegistry::instance()
                       .idFor( QStringLiteral( "layout" ), layout->name() )
                       .toStdString();
  // Quality assessment is provided by cartography:preflight (Phase M).
  return out;
}

} // namespace sicnu::agent::mapspec
