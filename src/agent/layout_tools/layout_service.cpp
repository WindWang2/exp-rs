// src/agent/layout_tools/layout_service.cpp
#include "layout_service.h"

#include <qgsapplication.h>
#include <qgslayout.h>
#include <qgslayoutaligner.h>
#include <qgslayoutexporter.h>
#include <qgslayoutitem.h>
#include <qgslayoutitemchart.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitempage.h>
#include <qgslayoutitempicture.h>
#include <qgslayoutitemregistry.h>
#include <qgslayoutitemscalebar.h>
#include <qgslayoutitemshape.h>
#include <qgslayoutmanager.h>
#include <qgslayoutpagecollection.h>
#include <qgslayoutpoint.h>
#include <qgslayoutsize.h>
#include <qgslayoutundostack.h>
#include <qgsmaplayer.h>
#include <qgsprintlayout.h>
#include <qgsproject.h>
#include <qgsprojectviewsettings.h>
#include <qgsreadwritecontext.h>
#include <qgsrectangle.h>
#include <qgsscalebarrendererregistry.h>

#include <QDomDocument>
#include <QFile>
#include <QFont>
#include <QSet>

#include <algorithm>
#include <functional>

using namespace std::string_literals;

namespace {

// Mirrors the raster export preflight in the GUI designer
// (qgslayoutdesignerdialog.cpp): RGBA buffer plus one full-size copy for the
// encoder stage, with hard limits protecting QImage's int-based dimensions.
constexpr double kMaxImageEdgePixels = 30000.0;

QgsLayoutItemMap *referenceMapFor( QgsLayout *layout )
{
  return layout ? layout->referenceMap() : nullptr;
}

// QgsLayout only exposes the template layoutItems(QList<T*>&); this wrapper
// returns all non-page items (pages are items too and must not appear in
// agent listings).
QList<QgsLayoutItem *> contentItems( QgsLayout *layout )
{
  QList<QgsLayoutItem *> all;
  layout->layoutItems( all );
  QList<QgsLayoutItem *> content;
  content.reserve( all.size() );
  for ( QgsLayoutItem *item : all )
  {
    if ( item->type() != QgsLayoutItemRegistry::LayoutPage )
      content.append( item );
  }
  return content;
}

struct PageSizeDef
{
  const char *name;
  double widthMm;
  double heightMm;
};

// ISO A series + US Letter, portrait dimensions in mm.
const PageSizeDef kPageSizes[] = {
  { "A5", 148.0, 210.0 },   { "A4", 210.0, 297.0 },  { "A3", 297.0, 420.0 },
  { "A2", 420.0, 594.0 },   { "A1", 594.0, 841.0 },  { "A0", 841.0, 1189.0 },
  { "Letter", 215.9, 279.4 },
};

bool lookupPageSize( const QString &name, double *w, double *h )
{
  for ( const auto &def : kPageSizes )
  {
    if ( name.compare( QString::fromLatin1( def.name ), Qt::CaseInsensitive ) == 0 )
    {
      *w = def.widthMm;
      *h = def.heightMm;
      return true;
    }
  }
  return false;
}

QString normalizedType( const QString &type )
{
  QString t = type.toLower().trimmed();
  t.remove( QLatin1Char( '_' ) );
  t.remove( QLatin1Char( '-' ) );
  t.remove( QLatin1Char( ' ' ) );
  return t;
}

double toMm( const Json::Value &v, double fallback )
{
  return ( v.isNumeric() && v.asDouble() >= 0 ) ? v.asDouble() : fallback;
}

std::string colorToString( const QColor &c )
{
  return c.name().toStdString();
}

QColor jsonToColor( const Json::Value &v )
{
  if ( !v.isString() )
    return QColor();
  const QString name = QString::fromStdString( v.asString() ).trimmed();
  if ( !QColor::isValidColor( name ) )
    return QColor();
  return QColor( name );
}

} // namespace

namespace sicnu::agent::layout_tools {

LayoutService &LayoutService::instance()
{
  static LayoutService service;
  return service;
}

bool LayoutService::loadProject( const QString &path, QString *error )
{
  QgsProject *project = QgsProject::instance();
  project->removeAllMapLayers();
  if ( !project->read( path ) )
  {
    // A failed read may have partially populated state; leave a consistent
    // (empty) project rather than a mix of old and new.
    project->layoutManager()->clear();
    project->removeAllMapLayers();
    if ( error )
      *error = QStringLiteral( "Cannot read project file: %1" ).arg( path );
    return false;
  }
  return true;
}

bool LayoutService::saveProject( const QString &path, QString *error )
{
  if ( !QgsProject::instance()->write( path ) )
  {
    if ( error )
      *error = QStringLiteral( "Cannot write project file: %1" ).arg( path );
    return false;
  }
  return true;
}

QStringList LayoutService::layoutNames() const
{
  QStringList names;
  const QList<QgsMasterLayoutInterface *> layouts = QgsProject::instance()->layoutManager()->layouts();
  names.reserve( layouts.size() );
  for ( QgsMasterLayoutInterface *ml : layouts )
    names << ml->name();
  return names;
}

QgsPrintLayout *LayoutService::createLayout( const QString &name, const QString &pageSize, bool landscape,
                                             QString *error )
{
  if ( name.trimmed().isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "Layout name must not be empty" );
    return nullptr;
  }
  if ( findLayout( name ) )
  {
    if ( error )
      *error = QStringLiteral( "Layout already exists: %1" ).arg( name );
    return nullptr;
  }

  auto *layout = new QgsPrintLayout( QgsProject::instance() );
  layout->initializeDefaults();

  double w = 210.0, h = 297.0;
  if ( !lookupPageSize( pageSize, &w, &h ) )
  {
    if ( error )
      *error = QStringLiteral( "Unknown page size '%1' (expected A0-A5 or Letter)" ).arg( pageSize );
    delete layout;
    return nullptr;
  }
  if ( landscape )
    std::swap( w, h );

  if ( QgsLayoutItemPage *page = layout->pageCollection()->pages().value( 0 ) )
    page->setPageSize( QgsLayoutSize( w, h ) );

  layout->setName( name );
  QgsProject::instance()->layoutManager()->addLayout( layout );
  return layout;
}

QgsPrintLayout *LayoutService::findLayout( const QString &name ) const
{
  const QList<QgsPrintLayout *> layouts = QgsProject::instance()->layoutManager()->printLayouts();
  for ( QgsPrintLayout *ml : layouts )
  {
    if ( ml->name() == name )
      return ml;
  }
  return nullptr;
}

bool LayoutService::deleteLayout( const QString &name, QString *error )
{
  QgsPrintLayout *layout = findLayout( name );
  if ( !layout )
  {
    if ( error )
      *error = QStringLiteral( "No layout named '%1'" ).arg( name );
    return false;
  }
  QgsProject::instance()->layoutManager()->removeLayout( layout );
  return true;
}

QString LayoutService::itemTypeToString( QgsLayoutItem *item )
{
  if ( !item )
    return QString();
  switch ( item->type() )
  {
    case QgsLayoutItemRegistry::LayoutMap:
      return QStringLiteral( "map" );
    case QgsLayoutItemRegistry::LayoutLabel:
      return QStringLiteral( "label" );
    case QgsLayoutItemRegistry::LayoutLegend:
      return QStringLiteral( "legend" );
    case QgsLayoutItemRegistry::LayoutScaleBar:
      return QStringLiteral( "scalebar" );
    case QgsLayoutItemRegistry::LayoutPicture:
    {
      // North arrows are pictures synced to a linked map; the resource path
      // points at a north-arrow SVG.
      auto *picture = qobject_cast<QgsLayoutItemPicture *>( item );
      if ( picture && picture->linkedMap() &&
           picture->picturePath().contains( QStringLiteral( "north_arrows" ) ) )
        return QStringLiteral( "northarrow" );
      return QStringLiteral( "picture" );
    }
    case QgsLayoutItemRegistry::LayoutShape:
    {
      auto *shape = qobject_cast<QgsLayoutItemShape *>( item );
      if ( shape )
      {
        switch ( shape->shapeType() )
        {
          case QgsLayoutItemShape::Ellipse:
            return QStringLiteral( "ellipse" );
          case QgsLayoutItemShape::Triangle:
            return QStringLiteral( "triangle" );
          case QgsLayoutItemShape::Rectangle:
            return QStringLiteral( "shape" );
        }
      }
      return QStringLiteral( "shape" );
    }
    case QgsLayoutItemRegistry::LayoutChart:
      return QStringLiteral( "chart" );
    case QgsLayoutItemRegistry::LayoutPage:
      return QStringLiteral( "page" );
    default:
      return QStringLiteral( "item" );
  }
}

Json::Value LayoutService::listItemInfos( QgsLayout *layout ) const
{
  Json::Value items( Json::arrayValue );
  if ( !layout )
    return items;
  const QList<QgsLayoutItem *> layoutItems = contentItems( layout );
  for ( QgsLayoutItem *item : layoutItems )
  {
    Json::Value info( Json::objectValue );
    info["id"] = item->uuid().toStdString();
    info["name"] = item->id().toStdString();
    info["type"] = itemTypeToString( item ).toStdString();
    info["page"] = item->page();
    info["x"] = std::round( item->positionWithUnits().x() * 100 ) / 100;
    info["y"] = std::round( item->positionWithUnits().y() * 100 ) / 100;
    info["width"] = std::round( item->sizeWithUnits().width() * 100 ) / 100;
    info["height"] = std::round( item->sizeWithUnits().height() * 100 ) / 100;
    items.append( info );
  }
  return items;
}

QgsLayoutItem *LayoutService::findItem( QgsLayout *layout, const QString &idOrUuid ) const
{
  if ( !layout || idOrUuid.isEmpty() )
    return nullptr;
  const QList<QgsLayoutItem *> items = contentItems( layout );
  for ( QgsLayoutItem *item : items )
  {
    if ( item->uuid() == idOrUuid )
      return item;
  }
  for ( QgsLayoutItem *item : items )
  {
    if ( item->id() == idOrUuid )
      return item;
  }
  // Lenient fallback; uuids and exact ids always win over this pass.
  for ( QgsLayoutItem *item : items )
  {
    if ( item->id().compare( idOrUuid, Qt::CaseInsensitive ) == 0 )
      return item;
  }
  return nullptr;
}

Json::Value LayoutService::itemProperties( QgsLayoutItem *item ) const
{
  Json::Value p( Json::objectValue );
  if ( !item )
    return p;

  p["id"] = item->uuid().toStdString();
  p["name"] = item->id().toStdString();
  p["type"] = itemTypeToString( item ).toStdString();
  p["page"] = item->page();
  p["x"] = item->positionWithUnits().x();
  p["y"] = item->positionWithUnits().y();
  p["width"] = item->sizeWithUnits().width();
  p["height"] = item->sizeWithUnits().height();
  p["rotation"] = item->itemRotation();
  p["opacity"] = item->itemOpacity() * 100.0;
  p["visible"] = item->isVisible();
  p["locked"] = item->isLocked();
  p["z"] = item->zValue();
  p["exclude_from_exports"] = item->excludeFromExports();
  p["frame_enabled"] = item->frameEnabled();
  p["frame_color"] = colorToString( item->frameStrokeColor() );
  p["frame_width"] = item->frameStrokeWidth().length();
  p["background_enabled"] = item->hasBackground();
  p["background_color"] = colorToString( item->backgroundColor() );

  if ( auto *label = qobject_cast<QgsLayoutItemLabel *>( item ) )
  {
    const QFont font = label->font();
    p["text"] = label->text().toStdString();
    p["font_family"] = font.family().toStdString();
    p["font_size"] = font.pointSizeF();
    p["bold"] = font.bold();
    p["italic"] = font.italic();
    p["color"] = colorToString( label->fontColor() );
  }
  else if ( auto *map = qobject_cast<QgsLayoutItemMap *>( item ) )
  {
    p["map_rotation"] = map->mapRotation();
    p["scale"] = map->scale();
    const QgsRectangle extent = map->extent();
    Json::Value ext( Json::arrayValue );
    ext.append( extent.xMinimum() );
    ext.append( extent.yMinimum() );
    ext.append( extent.xMaximum() );
    ext.append( extent.yMaximum() );
    p["extent"] = ext;
    Json::Value layers( Json::arrayValue );
    const QList<QgsMapLayer *> mapLayers = map->layers();
    for ( QgsMapLayer *layer : mapLayers )
      layers.append( layer->name().toStdString() );
    p["layers"] = layers;
    p["follow_visibility_preset"] = map->followVisibilityPreset();
    if ( map->followVisibilityPreset() )
      p["visibility_preset"] = map->followVisibilityPresetName().toStdString();
  }
  else if ( auto *legend = qobject_cast<QgsLayoutItemLegend *>( item ) )
  {
    p["title"] = legend->title().toStdString();
    if ( QgsLayoutItemMap *linked = legend->linkedMap() )
      p["linked_map"] = linked->id().toStdString();
  }
  else if ( auto *scaleBar = qobject_cast<QgsLayoutItemScaleBar *>( item ) )
  {
    p["style"] = scaleBar->style().toStdString();
    p["unit_label"] = scaleBar->unitLabel().toStdString();
    p["units_per_segment"] = scaleBar->unitsPerSegment();
    if ( QgsLayoutItemMap *linked = scaleBar->linkedMap() )
      p["linked_map"] = linked->id().toStdString();
  }
  else if ( auto *picture = qobject_cast<QgsLayoutItemPicture *>( item ) )
  {
    p["path"] = picture->picturePath().toStdString();
    p["north_mode"] = !picture->linkedMap()
                          ? "default"
                          : ( picture->northMode() == QgsLayoutItemPicture::TrueNorth ? "true" : "grid" );
    if ( QgsLayoutItemMap *linked = picture->linkedMap() )
      p["linked_map"] = linked->id().toStdString();
  }
  else if ( auto *shape = qobject_cast<QgsLayoutItemShape *>( item ) )
  {
    switch ( shape->shapeType() )
    {
      case QgsLayoutItemShape::Rectangle:
        p["shape_type"] = "rectangle";
        break;
      case QgsLayoutItemShape::Ellipse:
        p["shape_type"] = "ellipse";
        break;
      case QgsLayoutItemShape::Triangle:
        p["shape_type"] = "triangle";
        break;
    }
  }
  return p;
}

QgsLayoutItem *LayoutService::addItem( QgsLayout *layout, const QString &type, const Json::Value &props,
                                       QString *error )
{
  if ( !layout )
  {
    if ( error )
      *error = QStringLiteral( "Null layout" );
    return nullptr;
  }

  const QString t = normalizedType( type );
  QgsLayoutItem *item = nullptr;

  if ( t == QStringLiteral( "map" ) )
  {
    item = new QgsLayoutItemMap( layout );
  }
  else if ( t == QStringLiteral( "label" ) || t == QStringLiteral( "title" ) )
  {
    item = new QgsLayoutItemLabel( layout );
  }
  else if ( t == QStringLiteral( "legend" ) )
  {
    item = new QgsLayoutItemLegend( layout );
  }
  else if ( t == QStringLiteral( "scalebar" ) )
  {
    item = new QgsLayoutItemScaleBar( layout );
  }
  else if ( t == QStringLiteral( "northarrow" ) )
  {
    item = new QgsLayoutItemPicture( layout );
  }
  else if ( t == QStringLiteral( "picture" ) || t == QStringLiteral( "image" ) )
  {
    item = new QgsLayoutItemPicture( layout );
  }
  else if ( t == QStringLiteral( "shape" ) || t == QStringLiteral( "rectangle" ) ||
            t == QStringLiteral( "ellipse" ) || t == QStringLiteral( "triangle" ) )
  {
    item = new QgsLayoutItemShape( layout );
  }
  else if ( t == QStringLiteral( "chart" ) )
  {
    item = QgsLayoutItemChart::create( layout );
  }
  else
  {
    if ( error )
      *error = QStringLiteral( "Unsupported item type '%1'" ).arg( type );
    return nullptr;
  }

  // Sensible defaults before the caller-provided properties are applied.
  const double defaultW = ( t == QStringLiteral( "map" ) )       ? 200.0
                          : ( t == QStringLiteral( "label" ) || t == QStringLiteral( "title" ) ) ? 120.0
                          : ( t == QStringLiteral( "legend" ) )   ? 60.0
                          : ( t == QStringLiteral( "scalebar" ) ) ? 50.0
                          : ( t == QStringLiteral( "chart" ) )    ? 80.0
                                                                  : 30.0;
  const double defaultH = ( t == QStringLiteral( "map" ) )       ? 150.0
                          : ( t == QStringLiteral( "label" ) || t == QStringLiteral( "title" ) ) ? 12.0
                          : ( t == QStringLiteral( "legend" ) )   ? 50.0
                          : ( t == QStringLiteral( "scalebar" ) ) ? 6.0
                          : ( t == QStringLiteral( "chart" ) )    ? 60.0
                                                                  : 30.0;
  const double defaultX = props.isMember( "x" ) ? toMm( props["x"], 10.0 ) : 10.0;
  const double defaultY = props.isMember( "y" ) ? toMm( props["y"], 10.0 ) : 10.0;
  item->attemptSetSceneRect( QRectF( defaultX, defaultY, defaultW, defaultH ) );

  if ( t == QStringLiteral( "northarrow" ) )
  {
    auto *picture = qobject_cast<QgsLayoutItemPicture *>( item );
    picture->setPicturePath( QStringLiteral( ":/images/north_arrows/default.svg" ) );
    picture->setNorthMode( QgsLayoutItemPicture::GridNorth );
    if ( QgsLayoutItemMap *map = referenceMapFor( layout ) )
      picture->setLinkedMap( map );
  }
  else if ( t == QStringLiteral( "legend" ) )
  {
    auto *legend = qobject_cast<QgsLayoutItemLegend *>( item );
    if ( QgsLayoutItemMap *map = referenceMapFor( layout ) )
      legend->setLinkedMap( map );
    legend->setTitle( QStringLiteral( "图例" ) );
  }
  else if ( t == QStringLiteral( "scalebar" ) )
  {
    auto *scaleBar = qobject_cast<QgsLayoutItemScaleBar *>( item );
    if ( QgsLayoutItemMap *map = referenceMapFor( layout ) )
      scaleBar->setLinkedMap( map );
    scaleBar->applyDefaultSettings();
  }
  else if ( t == QStringLiteral( "label" ) || t == QStringLiteral( "title" ) )
  {
    auto *label = qobject_cast<QgsLayoutItemLabel *>( item );
    label->setText( t == QStringLiteral( "title" ) ? QStringLiteral( "地图标题" )
                                                   : QStringLiteral( "文本" ) );
    if ( t == QStringLiteral( "title" ) )
    {
      QFont font = label->font();
      font.setPointSizeF( 18.0 );
      font.setBold( true );
      label->setFont( font );
    }
  }
  else if ( t == QStringLiteral( "shape" ) || t == QStringLiteral( "rectangle" ) ||
            t == QStringLiteral( "ellipse" ) || t == QStringLiteral( "triangle" ) )
  {
    auto *shape = qobject_cast<QgsLayoutItemShape *>( item );
    shape->setShapeType( t == QStringLiteral( "ellipse" )   ? QgsLayoutItemShape::Ellipse
                         : t == QStringLiteral( "triangle" ) ? QgsLayoutItemShape::Triangle
                                                             : QgsLayoutItemShape::Rectangle );
  }

  layout->addLayoutItem( item );

  // A scale-only map needs a concrete extent first: setScale() early-returns
  // while the scale is still 0 (uninitialized extent).
  if ( qobject_cast<QgsLayoutItemMap *>( item ) && props.isMember( "scale" ) &&
       !props.isMember( "extent" ) && !QgsProject::instance()->mapLayers().empty() )
  {
    qobject_cast<QgsLayoutItemMap *>( item )->zoomToExtent(
        QgsProject::instance()->viewSettings()->fullExtent() );
  }

  // Apply caller properties through the shared property layer so type
  // specific values (text, extent, style...) are honored too.
  if ( !props.isNull() && props.isObject() && !props.empty() )
  {
    QStringList ignored;
    applyItemProperties( item, props, nullptr, &ignored, nullptr );
  }

  if ( QgsLayoutItemMap *map = qobject_cast<QgsLayoutItemMap *>( item ) )
  {
    // Prefer the project's full extent when the caller did not specify one.
    if ( !props.isMember( "extent" ) && !props.isMember( "scale" ) && !QgsProject::instance()->mapLayers().empty() )
      map->zoomToExtent( QgsProject::instance()->viewSettings()->fullExtent() );
    map->invalidateCache();
  }
  return item;
}

bool LayoutService::removeItem( QgsLayout *layout, const QString &idOrUuid, QString *error )
{
  QgsLayoutItem *item = findItem( layout, idOrUuid );
  if ( !item )
  {
    if ( error )
      *error = QStringLiteral( "No item '%1' in layout" ).arg( idOrUuid );
    return false;
  }
  layout->removeLayoutItem( item );
  return true;
}

bool LayoutService::applyItemProperties( QgsLayoutItem *item, const Json::Value &props, QStringList *applied,
                                         QStringList *ignored, QString *error )
{
  if ( !item || !item->layout() )
  {
    if ( error )
      *error = QStringLiteral( "Null item" );
    return false;
  }

  QgsLayout *layout = item->layout();
  QgsLayoutUndoStack *undoStack = layout->undoStack();
  const auto members = props.getMemberNames();

  // Begin a macro so one agent property edit = one undo step.
  undoStack->beginMacro( QStringLiteral( "Set Item Properties" ) );

  // Mutators return whether the value was actually applied; failures (bad
  // arity, unresolvable references) land in *ignored instead of *applied.
  const auto setProp = [&]( const std::string &key, const std::function<bool()> &mutator ) {
    if ( !props.isMember( key ) )
      return;
    if ( mutator() && applied )
      applied->append( QString::fromStdString( key ) );
  };

  // --- common properties: identical setters + undo ids as the GUI panel ---
  setProp( "name", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Change Item Id" ), QgsLayoutItem::UndoSetId );
    item->setId( QString::fromStdString( props["name"].asString() ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "x", [&]() {
    const double y = props.isMember( "y" ) ? props["y"].asDouble() : item->positionWithUnits().y();
    undoStack->beginCommand( item, QStringLiteral( "Move Item" ), QgsLayoutItem::UndoIncrementalMove );
    item->attemptMove( QgsLayoutPoint( props["x"].asDouble(), y ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "y", [&]() {
    const double x = props.isMember( "x" ) ? props["x"].asDouble() : item->positionWithUnits().x();
    undoStack->beginCommand( item, QStringLiteral( "Move Item" ), QgsLayoutItem::UndoIncrementalMove );
    item->attemptMove( QgsLayoutPoint( x, props["y"].asDouble() ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "width", [&]() {
    const double h = props.isMember( "height" ) ? props["height"].asDouble() : item->sizeWithUnits().height();
    undoStack->beginCommand( item, QStringLiteral( "Resize Item" ), QgsLayoutItem::UndoIncrementalResize );
    item->attemptResize( QgsLayoutSize( props["width"].asDouble(), h ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "height", [&]() {
    const double w = props.isMember( "width" ) ? props["width"].asDouble() : item->sizeWithUnits().width();
    undoStack->beginCommand( item, QStringLiteral( "Resize Item" ), QgsLayoutItem::UndoIncrementalResize );
    item->attemptResize( QgsLayoutSize( w, props["height"].asDouble() ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "rotation", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Rotate" ), QgsLayoutItem::UndoRotation );
    item->setItemRotation( props["rotation"].asDouble(), true );
    undoStack->endCommand();
    return true;
  } );
  setProp( "opacity", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Change Opacity" ), QgsLayoutItem::UndoOpacity );
    item->setItemOpacity( std::clamp( props["opacity"].asDouble(), 0.0, 100.0 ) / 100.0 );
    undoStack->endCommand();
    return true;
  } );
  setProp( "visible", [&]() {
    item->setVisibility( props["visible"].asBool() );
    return true;
  } );
  setProp( "locked", [&]() {
    item->setLocked( props["locked"].asBool() );
    return true;
  } );
  setProp( "z", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Change Z Value" ) );
    item->setZValue( props["z"].asDouble() );
    undoStack->endCommand();
    return true;
  } );
  setProp( "exclude_from_exports", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Change Export Exclusion" ) );
    item->setExcludeFromExports( props["exclude_from_exports"].asBool() );
    undoStack->endCommand();
    return true;
  } );
  setProp( "frame_enabled", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Toggle Frame" ) );
    item->setFrameEnabled( props["frame_enabled"].asBool() );
    undoStack->endCommand();
    return true;
  } );
  setProp( "frame_color", [&]() {
    if ( !QColor::isValidColor( QString::fromStdString( props["frame_color"].asString() ) ) )
      return false;
    undoStack->beginCommand( item, QStringLiteral( "Change Frame Color" ), QgsLayoutItem::UndoStrokeColor );
    item->setFrameStrokeColor( jsonToColor( props["frame_color"] ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "frame_width", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Change Frame Width" ), QgsLayoutItem::UndoStrokeWidth );
    item->setFrameStrokeWidth( QgsLayoutMeasurement( props["frame_width"].asDouble() ) );
    undoStack->endCommand();
    return true;
  } );
  setProp( "background_enabled", [&]() {
    undoStack->beginCommand( item, QStringLiteral( "Toggle Background" ) );
    item->setBackgroundEnabled( props["background_enabled"].asBool() );
    undoStack->endCommand();
    return true;
  } );
  setProp( "background_color", [&]() {
    if ( !QColor::isValidColor( QString::fromStdString( props["background_color"].asString() ) ) )
      return false;
    undoStack->beginCommand( item, QStringLiteral( "Change Background Color" ), QgsLayoutItem::UndoBackgroundColor );
    item->setBackgroundEnabled( true );
    item->setBackgroundColor( jsonToColor( props["background_color"] ) );
    undoStack->endCommand();
    return true;
  } );

  // --- type specific properties -------------------------------------------
  if ( auto *label = qobject_cast<QgsLayoutItemLabel *>( item ) )
  {
    setProp( "text", [&]() {
      undoStack->beginCommand( item, QStringLiteral( "Change Label Text" ) );
      label->setText( QString::fromStdString( props["text"].asString() ) );
      undoStack->endCommand();
      return true;
    } );
    setProp( "font_size", [&]() {
      if ( props["font_size"].asDouble() <= 0 || props["font_size"].asDouble() > 1000 )
        return false;
      QFont font = label->font();
      font.setPointSizeF( props["font_size"].asDouble() );
      undoStack->beginCommand( item, QStringLiteral( "Change Font Size" ) );
      label->setFont( font );
      undoStack->endCommand();
      return true;
    } );
    setProp( "bold", [&]() {
      QFont font = label->font();
      font.setBold( props["bold"].asBool() );
      undoStack->beginCommand( item, QStringLiteral( "Change Font Weight" ) );
      label->setFont( font );
      undoStack->endCommand();
      return true;
    } );
    setProp( "italic", [&]() {
      QFont font = label->font();
      font.setItalic( props["italic"].asBool() );
      undoStack->beginCommand( item, QStringLiteral( "Change Font Style" ) );
      label->setFont( font );
      undoStack->endCommand();
      return true;
    } );
    setProp( "color", [&]() {
      if ( !QColor::isValidColor( QString::fromStdString( props["color"].asString() ) ) )
        return false;
      undoStack->beginCommand( item, QStringLiteral( "Change Font Color" ) );
      label->setFontColor( jsonToColor( props["color"] ) );
      undoStack->endCommand();
      return true;
    } );
  }
  else if ( auto *map = qobject_cast<QgsLayoutItemMap *>( item ) )
  {
    setProp( "map_rotation", [&]() {
      undoStack->beginCommand( item, QStringLiteral( "Change Map Rotation" ), QgsLayoutItem::UndoMapRotation );
      map->setMapRotation( props["map_rotation"].asDouble() );
      undoStack->endCommand();
      return true;
    } );
    setProp( "scale", [&]() {
      if ( props["scale"].asDouble() <= 0 )
        return false;
      undoStack->beginCommand( item, QStringLiteral( "Change Map Scale" ) );
      map->setScale( props["scale"].asDouble() );
      undoStack->endCommand();
      return true;
    } );
    setProp( "extent", [&]() {
      const Json::Value &ext = props["extent"];
      if ( !ext.isArray() || ext.size() != 4 || ext[0].asDouble() >= ext[2].asDouble() ||
           ext[1].asDouble() >= ext[3].asDouble() )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "extent (expected [xmin,ymin,xmax,ymax] with min<max)" ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Change Map Extent" ) );
      map->zoomToExtent( QgsRectangle( ext[0].asDouble(), ext[1].asDouble(), ext[2].asDouble(),
                                       ext[3].asDouble() ) );
      undoStack->endCommand();
      return true;
    } );
    setProp( "layers", [&]() {
      QList<QgsMapLayer *> layers;
      QStringList missing;
      const auto &store = QgsProject::instance()->layerStore();
      for ( const Json::Value &v : props["layers"] )
      {
        const QString ref = QString::fromStdString( v.asString() );
        const QList<QgsMapLayer *> matches = store->mapLayersByName( ref );
        if ( !matches.isEmpty() )
          layers.append( matches.first() );
        else if ( QgsMapLayer *byId = store->mapLayer( ref ) )
          layers.append( byId );
        else
          missing.append( ref );
      }
      if ( layers.isEmpty() )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "layers (none resolved: %1)" ).arg( missing.join( ',' ) ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Change Map Layers" ) );
      map->setLayers( layers );
      undoStack->endCommand();
      return true;
    } );
  }
  else if ( auto *legend = qobject_cast<QgsLayoutItemLegend *>( item ) )
  {
    setProp( "title", [&]() {
      undoStack->beginCommand( item, QStringLiteral( "Change Legend Title" ) );
      legend->setTitle( QString::fromStdString( props["title"].asString() ) );
      undoStack->endCommand();
      return true;
    } );
    setProp( "linked_map", [&]() {
      const QString ref = QString::fromStdString( props["linked_map"].asString() );
      QgsLayoutItemMap *map = qobject_cast<QgsLayoutItemMap *>( findItem( layout, ref ) );
      if ( !map )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "linked_map: %1 (not found)" ).arg( ref ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Link Legend to Map" ) );
      legend->setLinkedMap( map );
      undoStack->endCommand();
      return true;
    } );
  }
  else if ( auto *scaleBar = qobject_cast<QgsLayoutItemScaleBar *>( item ) )
  {
    setProp( "style", [&]() {
      const QString style = QString::fromStdString( props["style"].asString() );
      const QStringList sorted = QgsApplication::scaleBarRendererRegistry()->sortedRendererList();
      QString matched;
      QString styleNormalized = style;
      styleNormalized.remove( ' ' ).replace( '_', ' ' );
      for ( const QString &candidate : sorted )
      {
        QString candidateNormalized = candidate;
        candidateNormalized.remove( ' ' );
        if ( candidate.compare( style, Qt::CaseInsensitive ) == 0 ||
             candidateNormalized.compare( styleNormalized, Qt::CaseInsensitive ) == 0 )
        {
          matched = candidate;
          break;
        }
      }
      if ( matched.isEmpty() )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "style: %1 (unknown; valid: %2)" )
                               .arg( style, sorted.join( ", " ) ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Change Scale Bar Style" ) );
      scaleBar->setStyle( matched );
      scaleBar->update();
      undoStack->endCommand();
      return true;
    } );
    setProp( "unit_label", [&]() {
      undoStack->beginCommand( item, QStringLiteral( "Change Scale Bar Unit Label" ) );
      scaleBar->setUnitLabel( QString::fromStdString( props["unit_label"].asString() ) );
      scaleBar->update();
      undoStack->endCommand();
      return true;
    } );
    setProp( "units_per_segment", [&]() {
      if ( props["units_per_segment"].asDouble() <= 0 )
        return false;
      undoStack->beginCommand( item, QStringLiteral( "Change Scale Bar Segments" ) );
      scaleBar->setUnitsPerSegment( props["units_per_segment"].asDouble() );
      scaleBar->update();
      undoStack->endCommand();
      return true;
    } );
    setProp( "linked_map", [&]() {
      const QString ref = QString::fromStdString( props["linked_map"].asString() );
      QgsLayoutItemMap *map = qobject_cast<QgsLayoutItemMap *>( findItem( layout, ref ) );
      if ( !map )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "linked_map: %1 (not found)" ).arg( ref ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Link Scale Bar to Map" ) );
      scaleBar->setLinkedMap( map );
      scaleBar->update();
      undoStack->endCommand();
      return true;
    } );
  }
  else if ( auto *picture = qobject_cast<QgsLayoutItemPicture *>( item ) )
  {
    setProp( "path", [&]() {
      const QString path = QString::fromStdString( props["path"].asString() );
      if ( !path.startsWith( QLatin1String( ":/" ) ) && !QFile::exists( path ) )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "path: %1 (not found)" ).arg( path ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Change Picture Path" ) );
      picture->setPicturePath( path );
      undoStack->endCommand();
      return true;
    } );
    setProp( "north_mode", [&]() {
      const QString mode = QString::fromStdString( props["north_mode"].asString() ).toLower();
      if ( mode != QStringLiteral( "grid" ) && mode != QStringLiteral( "true" ) &&
           mode != QStringLiteral( "default" ) )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "north_mode: %1 (grid|true|default)" ).arg( mode ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Change North Mode" ) );
      if ( mode == QStringLiteral( "default" ) )
      {
        // No map sync: a picture without a linked map keeps its own rotation.
        picture->setLinkedMap( nullptr );
      }
      else
      {
        picture->setNorthMode( mode == QStringLiteral( "true" ) ? QgsLayoutItemPicture::TrueNorth
                                                                : QgsLayoutItemPicture::GridNorth );
      }
      undoStack->endCommand();
      return true;
    } );
    setProp( "linked_map", [&]() {
      const QString ref = QString::fromStdString( props["linked_map"].asString() );
      QgsLayoutItemMap *map = qobject_cast<QgsLayoutItemMap *>( findItem( layout, ref ) );
      if ( !map )
      {
        if ( ignored )
          ignored->append( QStringLiteral( "linked_map: %1 (not found)" ).arg( ref ) );
        return false;
      }
      undoStack->beginCommand( item, QStringLiteral( "Link North Arrow to Map" ) );
      picture->setLinkedMap( map );
      undoStack->endCommand();
      return true;
    } );
  }

  undoStack->endMacro();

  // Report keys that were not applied — unknown names, or valid names that
  // do not apply to this item type (e.g. "text" on a map).
  static const char *kKnown[] = { "name",  "x",     "y",     "width", "height",  "rotation", "opacity",
                                  "visible", "locked", "z",    "exclude_from_exports", "frame_enabled",
                                  "frame_color", "frame_width", "background_enabled", "background_color",
                                  "text", "font_size", "bold", "italic", "color", "map_rotation", "scale", "extent",
                                  "layers", "title", "linked_map", "style", "unit_label", "units_per_segment",
                                  "path", "north_mode" };
  const QSet<QString> handledSet = applied ? QSet<QString>( applied->cbegin(), applied->cend() ) : QSet<QString>();
  for ( const std::string &key : members )
  {
    const QString qKey = QString::fromStdString( key );
    if ( handledSet.contains( qKey ) )
      continue;
    if ( ignored )
    {
      const bool known = std::find( std::begin( kKnown ), std::end( kKnown ), key ) != std::end( kKnown );
      ignored->append( known ? qKey + QStringLiteral( " (not applicable to this item type)" )
                             : qKey + QStringLiteral( " (unknown property)" ) );
    }
  }

  item->update();
  return true;
}

bool LayoutService::alignItems( QgsLayout *layout, const QStringList &ids, const QString &alignment,
                                QString *error )
{
  QList<QgsLayoutItem *> items;
  for ( const QString &id : ids )
  {
    QgsLayoutItem *item = findItem( layout, id );
    if ( !item )
    {
      if ( error )
        *error = QStringLiteral( "No item '%1'" ).arg( id );
      return false;
    }
    items.append( item );
  }
  if ( items.size() < 2 )
  {
    if ( error )
      *error = QStringLiteral( "Alignment needs at least 2 items" );
    return false;
  }

  QgsLayoutAligner::Alignment a;
  if ( alignment == QStringLiteral( "left" ) )
    a = QgsLayoutAligner::AlignLeft;
  else if ( alignment == QStringLiteral( "hcenter" ) )
    a = QgsLayoutAligner::AlignHCenter;
  else if ( alignment == QStringLiteral( "right" ) )
    a = QgsLayoutAligner::AlignRight;
  else if ( alignment == QStringLiteral( "top" ) )
    a = QgsLayoutAligner::AlignTop;
  else if ( alignment == QStringLiteral( "vcenter" ) )
    a = QgsLayoutAligner::AlignVCenter;
  else if ( alignment == QStringLiteral( "bottom" ) )
    a = QgsLayoutAligner::AlignBottom;
  else
  {
    if ( error )
      *error = QStringLiteral( "Unknown alignment '%1' (left|hcenter|right|top|vcenter|bottom)" ).arg( alignment );
    return false;
  }

  QgsLayoutAligner::alignItems( layout, items, a );
  return true;
}

bool LayoutService::distributeItems( QgsLayout *layout, const QStringList &ids, const QString &distribution,
                                     QString *error )
{
  QList<QgsLayoutItem *> items;
  for ( const QString &id : ids )
  {
    QgsLayoutItem *item = findItem( layout, id );
    if ( !item )
    {
      if ( error )
        *error = QStringLiteral( "No item '%1'" ).arg( id );
      return false;
    }
    items.append( item );
  }
  if ( items.size() < 3 )
  {
    if ( error )
      *error = QStringLiteral( "Distribution needs at least 3 items" );
    return false;
  }

  QgsLayoutAligner::Distribution d;
  if ( distribution == QStringLiteral( "left" ) )
    d = QgsLayoutAligner::DistributeLeft;
  else if ( distribution == QStringLiteral( "hcenter" ) )
    d = QgsLayoutAligner::DistributeHCenter;
  else if ( distribution == QStringLiteral( "hspace" ) )
    d = QgsLayoutAligner::DistributeHSpace;
  else if ( distribution == QStringLiteral( "right" ) )
    d = QgsLayoutAligner::DistributeRight;
  else if ( distribution == QStringLiteral( "top" ) )
    d = QgsLayoutAligner::DistributeTop;
  else if ( distribution == QStringLiteral( "vcenter" ) )
    d = QgsLayoutAligner::DistributeVCenter;
  else if ( distribution == QStringLiteral( "vspace" ) )
    d = QgsLayoutAligner::DistributeVSpace;
  else if ( distribution == QStringLiteral( "bottom" ) )
    d = QgsLayoutAligner::DistributeBottom;
  else
  {
    if ( error )
      *error = QStringLiteral(
                   "Unknown distribution '%1' (left|hcenter|hspace|right|top|vcenter|vspace|bottom)" )
                   .arg( distribution );
    return false;
  }

  QgsLayoutAligner::distributeItems( layout, items, d );
  return true;
}

bool LayoutService::saveTemplate( QgsLayout *layout, const QString &path, QString *error )
{
  if ( !layout )
  {
    if ( error )
      *error = QStringLiteral( "Null layout" );
    return false;
  }
  QgsReadWriteContext context;
  if ( !layout->saveAsTemplate( path, context ) )
  {
    if ( error )
      *error = QStringLiteral( "Failed to write template: %1" ).arg( path );
    return false;
  }
  return true;
}

QgsPrintLayout *LayoutService::loadTemplate( const QString &name, const QString &path, QString *error )
{
  if ( findLayout( name ) )
  {
    if ( error )
      *error = QStringLiteral( "Layout already exists: %1" ).arg( name );
    return nullptr;
  }

  QFile file( path );
  if ( !file.open( QIODevice::ReadOnly ) )
  {
    if ( error )
      *error = QStringLiteral( "Cannot read template file: %1" ).arg( path );
    return nullptr;
  }
  QDomDocument doc;
  if ( !doc.setContent( &file ) )
  {
    if ( error )
      *error = QStringLiteral( "Invalid template file: %1" ).arg( path );
    return nullptr;
  }

  auto *layout = new QgsPrintLayout( QgsProject::instance() );
  layout->initializeDefaults();
  QgsReadWriteContext context;
  bool ok = false;
  layout->loadFromTemplate( doc, context, /*clearExisting=*/true, &ok );
  if ( !ok )
  {
    if ( error )
      *error = QStringLiteral( "Failed to load template: %1" ).arg( path );
    delete layout;
    return nullptr;
  }
  layout->setName( name );
  QgsProject::instance()->layoutManager()->addLayout( layout );
  return layout;
}

bool LayoutService::exportLayout( QgsLayout *layout, const QString &path, const QString &format, double dpi,
                                  qint64 maxBytes, QString *error )
{
  if ( !layout )
  {
    if ( error )
      *error = QStringLiteral( "Null layout" );
    return false;
  }
  if ( layout->pageCollection()->pageCount() == 0 )
  {
    if ( error )
      *error = QStringLiteral( "Layout has no pages" );
    return false;
  }
  if ( dpi <= 0 || dpi > 1200 )
  {
    if ( error )
      *error = QStringLiteral( "DPI must be between 1 and 1200" );
    return false;
  }

  const QString fmt = format.toLower().trimmed();
  const bool isPdf = fmt == QStringLiteral( "pdf" );
  const bool isSvg = fmt == QStringLiteral( "svg" );
  const bool isRaster = !isPdf && !isSvg;
  if ( isRaster && fmt != QStringLiteral( "png" ) && fmt != QStringLiteral( "jpg" ) &&
       fmt != QStringLiteral( "jpeg" ) )
  {
    if ( error )
      *error = QStringLiteral( "Unsupported format '%1' (png|jpg|pdf|svg)" ).arg( format );
    return false;
  }

  if ( isRaster )
  {
    // Same preflight as the GUI: RGBA + one encoder copy, hard pixel edges.
    // Use the largest page so multi-page layouts cannot slip past the guard.
    double maxW = 0.0, maxH = 0.0;
    const QList<QgsLayoutItemPage *> pages = layout->pageCollection()->pages();
    for ( const QgsLayoutItemPage *page : pages )
    {
      maxW = std::max( maxW, page->pageSize().width() );
      maxH = std::max( maxH, page->pageSize().height() );
    }
    const double wPx = maxW / 25.4 * dpi;
    const double hPx = maxH / 25.4 * dpi;
    const qint64 bytes = static_cast<qint64>( wPx ) * static_cast<qint64>( hPx ) * 4 * 2;
    if ( wPx > kMaxImageEdgePixels || hPx > kMaxImageEdgePixels )
    {
      if ( error )
        *error = QStringLiteral( "Export would be %1x%2 px (edge limit %3 px); reduce the DPI" )
                     .arg( qRound( wPx ) )
                     .arg( qRound( hPx ) )
                     .arg( qRound( kMaxImageEdgePixels ) );
      return false;
    }
    if ( maxBytes > 0 && bytes > maxBytes )
    {
      if ( error )
        *error = QStringLiteral( "Export would need ~%1 MB (limit %2 MB); reduce the DPI" )
                     .arg( bytes / 1024.0 / 1024.0, 0, 'f', 1 )
                     .arg( maxBytes / 1024.0 / 1024.0, 0, 'f', 1 );
      return false;
    }
  }

  QgsLayoutExporter exporter( layout );
  QgsLayoutExporter::ExportResult result = QgsLayoutExporter::Success;
  if ( isPdf )
  {
    QgsLayoutExporter::PdfExportSettings settings;
    settings.dpi = dpi;
    result = exporter.exportToPdf( path, settings );
  }
  else if ( isSvg )
  {
    QgsLayoutExporter::SvgExportSettings settings;
    settings.dpi = dpi;
    result = exporter.exportToSvg( path, settings );
  }
  else
  {
    QgsLayoutExporter::ImageExportSettings settings;
    settings.dpi = dpi;
    result = exporter.exportToImage( path, settings );
  }

  if ( result != QgsLayoutExporter::Success )
  {
    if ( error )
      *error = QStringLiteral( "Export failed: %1" ).arg( exporter.errorMessage() );
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Auto layout
// ---------------------------------------------------------------------------

Json::Value LayoutService::autoArrange( QgsLayout *layout, bool apply, QString *error )
{
  Json::Value out( Json::objectValue );
  if ( !layout )
  {
    if ( error )
      *error = QStringLiteral( "Null layout" );
    return out;
  }
  if ( layout->pageCollection()->pageCount() == 0 )
  {
    if ( error )
      *error = QStringLiteral( "Layout has no pages" );
    return out;
  }

  const QgsLayoutSize pageSize = layout->pageCollection()->pages().constFirst()->pageSize();
  const double pageW = std::max( 40.0, pageSize.width() );
  const double pageH = std::max( 40.0, pageSize.height() );
  const double margin = std::clamp( qMin( pageW, pageH ) / 25.0, 4.0, 10.0 );

  // Component regions of the classic thematic composition (mm, page coords).
  // The bottom band stacks scale bar above the source note so the two never
  // overlap on small pages (A5 and below).
  const double titleH = 14.0;
  const double mapX = margin;
  const double mapY = margin + titleH;
  const double legendW = std::clamp( pageW * 0.18, 25.0, 60.0 );
  const double mapW = std::max( 20.0, pageW - margin * 2 - legendW - 4.0 );
  const double mapH = std::max( 20.0, pageH - mapY - margin - 10.0 );
  const double bottomBandY = mapY + mapH + 3.0;

  const auto findAutoItem = [layout]( const QString &id ) -> QgsLayoutItem * {
    const QList<QgsLayoutItem *> items = contentItems( layout );
    for ( QgsLayoutItem *item : items )
    {
      if ( item->id() == id )
        return item;
    }
    return nullptr;
  };

  // Moving an existing auto-managed item is undoable, like every other edit.
  const auto moveItem = [layout]( QgsLayoutItem *item, const QRectF &rect ) {
    layout->undoStack()->beginCommand( item, QStringLiteral( "Auto Arrange Item" ),
                                       QgsLayoutItem::UndoIncrementalMove );
    item->attemptSetSceneRect( rect );
    layout->undoStack()->endCommand();
  };

  const struct
  {
      QString id;
      QRectF rect;
  } componentRects[] = {
    { QStringLiteral( "auto:title" ), QRectF( margin, qMax( 2.0, margin / 2.0 ), pageW - margin * 2, titleH ) },
    { QStringLiteral( "auto:legend" ), QRectF( pageW - margin - legendW, mapY, legendW, mapH * 0.6 ) },
    { QStringLiteral( "auto:scalebar" ), QRectF( margin, bottomBandY, qMin( 60.0, mapW / 2 ), 4.0 ) },
    { QStringLiteral( "auto:northarrow" ), QRectF( mapX + mapW - 12.0, mapY + 2.0, 10.0, 10.0 ) },
    { QStringLiteral( "auto:source" ), QRectF( margin, bottomBandY + 6.0, pageW / 2, 4.0 ) },
  };

  Json::Value arranged( Json::arrayValue );
  const auto record = [&arranged]( QgsLayoutItem *item, bool created ) {
    Json::Value info( Json::objectValue );
    info["id"] = item->id().toStdString();
    info["created"] = created;
    info["x"] = std::round( item->positionWithUnits().x() * 10 ) / 10;
    info["y"] = std::round( item->positionWithUnits().y() * 10 ) / 10;
    info["width"] = std::round( item->sizeWithUnits().width() * 10 ) / 10;
    info["height"] = std::round( item->sizeWithUnits().height() * 10 ) / 10;
    arranged.append( info );
  };

  // Dry-run (suggest) must not mutate: report proposed regions only.
  if ( !apply )
  {
    Json::Value proposed( Json::arrayValue );
    for ( const auto &component : componentRects )
    {
      Json::Value info( Json::objectValue );
      info["id"] = component.id.toStdString();
      info["exists"] = findAutoItem( component.id ) != nullptr;
      info["x"] = component.rect.x();
      info["y"] = component.rect.y();
      info["width"] = component.rect.width();
      info["height"] = component.rect.height();
      proposed.append( info );
    }
    if ( !layout->referenceMap() )
    {
      Json::Value info( Json::objectValue );
      info["id"] = "auto:map";
      info["exists"] = false;
      info["x"] = mapX;
      info["y"] = mapY;
      info["width"] = mapW;
      info["height"] = mapH;
      proposed.append( info );
    }
    if ( QgsPrintLayout *printLayout = qobject_cast<QgsPrintLayout *>( layout ) )
      out["layout"] = printLayout->name().toStdString();
    out["applied"] = false;
    out["components"] = proposed;
    return out;
  }

  // The map anchors everything; reuse the reference map when present.
  QgsLayoutItemMap *map = layout->referenceMap();
  const bool createdMap = !map;
  if ( !map )
  {
    map = new QgsLayoutItemMap( layout );
    map->setId( QStringLiteral( "auto:map" ) );
    map->attemptSetSceneRect( QRectF( mapX, mapY, mapW, mapH ) );
    if ( !QgsProject::instance()->mapLayers().empty() )
      map->zoomToExtent( QgsProject::instance()->viewSettings()->fullExtent() );
    layout->addLayoutItem( map );  // pushes its own undo command
    record( map, createdMap );
  }
  else if ( map->id() == QLatin1String( "auto:map" ) )
  {
    moveItem( map, QRectF( mapX, mapY, mapW, mapH ) );
    record( map, false );
  }

  const auto createItem = [this, layout, &map]( const QString &id ) -> QgsLayoutItem * {
    Q_UNUSED( this );
    if ( id == QLatin1String( "auto:title" ) )
    {
      auto *label = new QgsLayoutItemLabel( layout );
      label->setText( QStringLiteral( "地图标题" ) );
      QFont font = label->font();
      font.setPointSizeF( 18.0 );
      font.setBold( true );
      label->setFont( font );
      label->setHAlign( Qt::AlignHCenter );
      return label;
    }
    if ( id == QLatin1String( "auto:legend" ) )
    {
      auto *legend = new QgsLayoutItemLegend( layout );
      legend->setTitle( QStringLiteral( "图例" ) );
      legend->setLinkedMap( map );
      return legend;
    }
    if ( id == QLatin1String( "auto:scalebar" ) )
    {
      auto *bar = new QgsLayoutItemScaleBar( layout );
      bar->setLinkedMap( map );
      bar->applyDefaultSettings();
      return bar;
    }
    if ( id == QLatin1String( "auto:northarrow" ) )
    {
      auto *picture = new QgsLayoutItemPicture( layout );
      picture->setPicturePath( QStringLiteral( ":/images/north_arrows/default.svg" ) );
      picture->setNorthMode( QgsLayoutItemPicture::GridNorth );
      picture->setLinkedMap( map );
      return picture;
    }
    if ( id == QLatin1String( "auto:source" ) )
    {
      auto *label = new QgsLayoutItemLabel( layout );
      label->setText( QStringLiteral( "数据来源：" ) );
      QFont font = label->font();
      font.setPointSizeF( 7.0 );
      label->setFont( font );
      return label;
    }
    return nullptr;
  };

  for ( const auto &component : componentRects )
  {
    QgsLayoutItem *item = findAutoItem( component.id );
    const bool created = !item;
    if ( !item )
    {
      item = createItem( component.id );
      if ( !item )
        continue;
      item->setId( component.id );
      item->attemptSetSceneRect( component.rect );
      layout->addLayoutItem( item );
      record( item, created );
    }
    else
    {
      moveItem( item, component.rect );
      record( item, created );
    }
  }

  // Size the legend to its content last, then report the final geometry.
  if ( QgsLayoutItem *legendItem = findAutoItem( QStringLiteral( "auto:legend" ) ) )
  {
    if ( auto *legend = qobject_cast<QgsLayoutItemLegend *>( legendItem ) )
      legend->adjustBoxSize();
    record( legendItem, false );
  }

  if ( QgsPrintLayout *printLayout = qobject_cast<QgsPrintLayout *>( layout ) )
    out["layout"] = printLayout->name().toStdString();
  out["applied"] = true;
  out["components"] = arranged;
  return out;
}

} // namespace sicnu::agent::layout_tools
