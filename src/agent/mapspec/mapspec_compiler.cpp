// src/agent/mapspec/mapspec_compiler.cpp
#include "mapspec_compiler.h"

#include "../cartography/chart_registry.h"
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

#include <QDir>

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

} // namespace

QgsPrintLayout *MapSpecCompiler::compile( const Json::Value &spec, QString *error )
{
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
    if ( !compileItem( layout, "map", id, props, &itemError ) )
    {
      if ( error )
        *error = QStringLiteral( "map frame '%1': %2" ).arg( QString::fromStdString( id ), itemError );
      return nullptr;
    }
    mapFrameIds.push_back( id );
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

  // --- text furniture -------------------------------------------------------
  for ( const auto &title : itemsOf( "titles" ) )
  {
    Json::Value props = rectToProps( title["rect_mm"] );
    props["text"] = title.get( "text", "" );
    double fontPt = 18.0;
    if ( title.isMember( "font" ) && title["font"].isObject() &&
         title["font"].isMember( "size_pt" ) && title["font"]["size_pt"].isNumeric() )
      fontPt = title["font"]["size_pt"].asDouble();
    props["font_size"] = fontPt;
    props["bold"] = true;
    compileItem( layout, "title", title["id"].asString(), props, nullptr );
  }
  for ( const auto &label : itemsOf( "labels" ) )
  {
    Json::Value props = rectToProps( label["rect_mm"] );
    props["text"] = label.get( "text", "" );
    if ( label.isMember( "font" ) && label["font"].isObject() &&
         label["font"].isMember( "size_pt" ) )
      props["font_size"] = label["font"]["size_pt"];
    compileItem( layout, "label", label["id"].asString(), props, nullptr );
  }
  for ( const auto &note : itemsOf( "source_notes" ) )
  {
    Json::Value props = rectToProps( note["rect_mm"] );
    props["text"] = note.get( "text", "" );
    props["font_size"] = 7.0;
    compileItem( layout, "label", note["id"].asString(), props, nullptr );
  }
  for ( const auto &annotation : itemsOf( "annotations" ) )
  {
    if ( annotation.isMember( "qgis_type" ) )
      continue; // extracted placeholder for a non-mappable QGIS item — leave it
    Json::Value props = rectToProps( annotation["rect_mm"] );
    props["text"] = annotation.get( "text", "" );
    compileItem( layout, "label", annotation["id"].asString(), props, nullptr );
  }

  // --- legends / scale bars / north arrows (link to map frames) -------------
  for ( const auto &legend : itemsOf( "legends" ) )
  {
    Json::Value props = rectToProps( legend["rect_mm"] );
    if ( legend.isMember( "title" ) )
      props["title"] = legend["title"];
    if ( legend.isMember( "map_ref" ) )
      props["linked_map"] = legend["map_ref"];
    compileItem( layout, "legend", legend["id"].asString(), props, nullptr );
  }
  for ( const auto &scaleBar : itemsOf( "scale_bars" ) )
  {
    Json::Value props = rectToProps( scaleBar["rect_mm"] );
    if ( scaleBar.isMember( "style" ) && scaleBar["style"].isString() )
      props["style"] = scaleBar["style"];
    if ( scaleBar.isMember( "units" ) && scaleBar["units"].isString() )
      props["unit_label"] = scaleBar["units"];
    if ( scaleBar.isMember( "map_ref" ) )
      props["linked_map"] = scaleBar["map_ref"];
    compileItem( layout, "scalebar", scaleBar["id"].asString(), props, nullptr );
  }
  for ( const auto &arrow : itemsOf( "north_arrows" ) )
  {
    Json::Value props = rectToProps( arrow["rect_mm"] );
    if ( arrow.isMember( "svg" ) && arrow["svg"].isString() )
      props["path"] = arrow["svg"];
    if ( arrow.isMember( "map_ref" ) )
      props["linked_map"] = arrow["map_ref"];
    compileItem( layout, "northarrow", arrow["id"].asString(), props, nullptr );
  }

  // --- charts ---------------------------------------------------------------
  QString chartError;
  QString chartPath;
  for ( const auto &chartItem : itemsOf( "charts" ) )
  {
    const Json::Value &chart = chartItem["chart"];
    const std::string mode = chart.isMember( "binding" ) && chart["binding"].isMember( "mode" )
                               ? chart["binding"]["mode"].asString()
                               : "inline";
    Json::Value props = rectToProps( chartItem["rect_mm"] );
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
      chartPath = QDir::temp().filePath( QStringLiteral( "sicnu-chart-%1.png" )
                                           .arg( QString::fromStdString( chartItem["id"].asString() ) ) );
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
  for ( const auto &colorbar : itemsOf( "colorbars" ) )
  {
    const QString path = QDir::temp().filePath( QStringLiteral( "sicnu-colorbar-%1.png" )
                                                  .arg( QString::fromStdString( colorbar["id"].asString() ) ) );
    if ( sicnu::agent::cartography::renderColorbarToFile( colorbar, path ) )
    {
      Json::Value props = rectToProps( colorbar["rect_mm"] );
      props["path"] = path.toStdString();
      compileItem( layout, "picture", colorbar["id"].asString(), props, nullptr );
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
    const QString text = label->text().toLower();
    if ( text.contains( QLatin1String( "source" ) ) ||
         label->text().contains( QStringLiteral( "来源" ) ) )
      return "source_notes";
    if ( label->font().pointSizeF() >= 16.0 || label->font().bold() )
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
