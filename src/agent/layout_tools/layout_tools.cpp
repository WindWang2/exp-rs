// src/agent/layout_tools/layout_tools.cpp
#include "layout_tools.h"

#include "layout_service.h"

#include <qgslayout.h>
#include <qgslayoutitem.h>
#include <qgslayoutitemregistry.h>
#include <qgslayoutitemlabel.h>
#include <qgslayoutitemlegend.h>
#include <qgslayoutitemmap.h>
#include <qgslayoutitemscalebar.h>
#include <qgspagescollection.h>
#include <qgslayoutitempage.h>
#include <qgsprintlayout.h>
#include <qgsmaplayer.h>

#include <QFont>

#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <memory>

using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolPtr;
using sicnu::agent::spatial_tools::SpatialToolResult;

namespace sicnu::agent::layout_tools {

namespace {

QString requireString( const Json::Value &input, const char *key, std::string *error )
{
  if ( !input.isMember( key ) || !input[key].isString() || input[key].asString().empty() )
  {
    *error = std::string( "Missing required string parameter '" ) + key + "'";
    return QString();
  }
  return QString::fromStdString( input[key].asString() );
}

const Json::Value emptyObject( Json::objectValue );

const Json::Value &optionalObject( const Json::Value &input, const char *key )
{
  if ( input.isMember( key ) && input[key].isObject() )
    return input[key];
  return emptyObject;
}

Json::Value strArray( const QStringList &values )
{
  Json::Value arr( Json::arrayValue );
  for ( const QString &v : values )
    arr.append( v.toStdString() );
  return arr;
}

// ---------------------------------------------------------------------------
// Project-level tools
// ---------------------------------------------------------------------------

class LayoutListTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:list"; }
    std::string displayName() const override { return "List layouts"; }
    std::string description() const override
    {
      return "List all print layouts in the current project with their page counts.";
    }
    std::vector<std::string> tags() const override { return { "layout", "cartography", "list" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"] = Json::Value( Json::objectValue );
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["layouts"] = Json::Value( Json::arrayValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      (void)input;
      Json::Value out( Json::objectValue );
      Json::Value layouts( Json::arrayValue );
      const QStringList names = LayoutService::instance().layoutNames();
      for ( const QString &n : names )
        layouts.append( n.toStdString() );
      out["layouts"] = layouts;
      return SpatialToolResult::ok( out );
    }
};

class LayoutCreateTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:create"; }
    std::string displayName() const override { return "Create layout"; }
    std::string description() const override
    {
      return "Create a new print layout. page_size: A0|A1|A2|A3|A4|A5|Letter; "
             "orientation: portrait|landscape.";
    }
    std::vector<std::string> tags() const override { return { "layout", "cartography", "create" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["name"] = Json::Value( Json::objectValue );
      props["name"]["type"] = "string";
      props["page_size"] = Json::Value( Json::objectValue );
      props["page_size"]["type"] = "string";
      props["orientation"] = Json::Value( Json::objectValue );
      props["orientation"]["type"] = "string";
      props["orientation"]["enum"] = strArray( { "portrait", "landscape" } );
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "name" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["name"] = Json::Value( Json::objectValue );
      schema["properties"]["name"]["type"] = "string";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString name = requireString( input, "name", &err );
      if ( err.empty() == false )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      const QString pageSize =
          input.isMember( "page_size" ) ? QString::fromStdString( input["page_size"].asString() ) : "A4";
      const bool landscape = input.isMember( "orientation" ) &&
                             input["orientation"].asString() == "landscape";

      QString error;
      QgsPrintLayout *layout = LayoutService::instance().createLayout( name, pageSize, landscape, &error );
      if ( !layout )
        return SpatialToolResult::failure( error.toStdString(), "INVALID_PARAMETER", "validation" );

      Json::Value out( Json::objectValue );
      out["name"] = name.toStdString();
      out["page_size"] = pageSize.toLower().toStdString();
      out["orientation"] = landscape ? "landscape" : "portrait";
      return SpatialToolResult::ok( out );
    }
};

class LayoutProjectTool final : public SpatialTool
{
  public:
    explicit LayoutProjectTool( bool save )
        : mSave( save )
    {
    }
    std::string name() const override { return mSave ? "layout:save_project" : "layout:load_project"; }
    std::string displayName() const override { return mSave ? "Save project" : "Load project"; }
    std::string description() const override
    {
      return mSave ? "Write the current project (all layouts, layers, styles) to a .qgs file."
                   : "Read a .qgs/.qgz project (replaces the current project and its layouts).";
    }
    std::vector<std::string> tags() const override { return { "layout", "project", mSave ? "save" : "load" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["path"] = Json::Value( Json::objectValue );
      props["path"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "path" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["path"] = Json::Value( Json::objectValue );
      schema["properties"]["path"]["type"] = "string";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString path = requireString( input, "path", &err );
      if ( err.empty() == false )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QString error;
      const bool ok = mSave ? LayoutService::instance().saveProject( path, &error )
                            : LayoutService::instance().loadProject( path, &error );
      if ( !ok )
        return SpatialToolResult::failure( error.toStdString(), "DATA_IO", "io" );

      Json::Value out( Json::objectValue );
      out["path"] = path.toStdString();
      return SpatialToolResult::ok( out );
    }

  private:
    bool mSave;
};

// ---------------------------------------------------------------------------
// Item tools
// ---------------------------------------------------------------------------

class LayoutListItemsTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:list_items"; }
    std::string displayName() const override { return "List layout items"; }
    std::string description() const override
    {
      return "Compact listing of all items in a layout (id, type, name, page, x/y/width/height in mm).";
    }
    std::vector<std::string> tags() const override { return { "layout", "items", "list" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["items"] = Json::Value( Json::arrayValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( err.empty() == false )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      Json::Value out( Json::objectValue );
      out["layout"] = layoutName.toStdString();
      out["items"] = LayoutService::instance().listItemInfos( layout );
      return SpatialToolResult::ok( out );
    }
};

class LayoutGetItemPropertiesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:get_item_properties"; }
    std::string displayName() const override { return "Get layout item properties"; }
    std::string description() const override
    {
      return "Full property map (common geometry/appearance + type specific) for one layout item.";
    }
    std::vector<std::string> tags() const override { return { "layout", "items", "properties" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["item"] = Json::Value( Json::objectValue );
      props["item"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "item" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["properties"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString itemRef = requireString( input, "item", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );
      QgsLayoutItem *item = LayoutService::instance().findItem( layout, itemRef );
      if ( !item )
        return SpatialToolResult::failure( "No item '" + itemRef.toStdString() + "' in layout",
                                           "NOT_FOUND", "validation" );

      Json::Value out( Json::objectValue );
      out["properties"] = LayoutService::instance().itemProperties( item );
      return SpatialToolResult::ok( out );
    }
};

class LayoutAddItemTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:add_item"; }
    std::string displayName() const override { return "Add layout item"; }
    std::string description() const override
    {
      return "Add an item to a layout. type: map|label|title|legend|scalebar|northarrow|picture|shape|"
             "rectangle|ellipse|triangle|chart. properties: x, y, width, height (mm), plus type "
             "specific keys (text for label/title, title for legend, style/unit_label for scalebar, "
             "path for picture, extent/scale/layers for map).";
    }
    std::vector<std::string> tags() const override { return { "layout", "items", "create" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["type"] = Json::Value( Json::objectValue );
      props["type"]["type"] = "string";
      props["properties"] = Json::Value( Json::objectValue );
      props["properties"]["type"] = "object";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "type" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["id"] = Json::Value( Json::objectValue );
      schema["properties"]["id"]["type"] = "string";
      schema["properties"]["item"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString type = requireString( input, "type", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      QString error;
      QgsLayoutItem *item = LayoutService::instance().addItem( layout, type,
                                                               optionalObject( input, "properties" ), &error );
      if ( !item )
        return SpatialToolResult::failure( error.toStdString(), "INVALID_PARAMETER", "validation" );

      Json::Value out( Json::objectValue );
      out["id"] = item->uuid().toStdString();
      out["item"] = LayoutService::instance().itemProperties( item );
      return SpatialToolResult::ok( out );
    }
};

class LayoutSetItemPropertiesTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:set_item_properties"; }
    std::string displayName() const override { return "Set layout item properties"; }
    std::string description() const override
    {
      return "Apply a property map to a layout item as one undoable step. Geometry keys x, y, width, "
             "height are millimeters relative to the item page, measured at the item reference point "
             "(upper-left by default). Common keys: rotation (deg), opacity (0-100), visible, locked, "
             "z, frame_enabled, frame_color, frame_width, background_enabled, background_color, name. "
             "Type specific: text, font_size, bold, italic, color (labels); title (legend); style "
             "(scale bar, e.g. 'Single Box', 'Line Ticks Up', 'Numeric'), unit_label, "
             "units_per_segment; path, north_mode grid|true|default (pictures); extent "
             "[xmin,ymin,xmax,ymax] or scale, layers (by name), map_rotation (maps). extent takes "
             "precedence over scale when both are given.";
    }
    std::vector<std::string> tags() const override { return { "layout", "items", "properties", "edit" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["item"] = Json::Value( Json::objectValue );
      props["item"]["type"] = "string";
      props["properties"] = Json::Value( Json::objectValue );
      props["properties"]["type"] = "object";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "item" );
      required.append( "properties" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["applied"] = Json::Value( Json::arrayValue );
      schema["properties"]["ignored"] = Json::Value( Json::arrayValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString itemRef = requireString( input, "item", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      if ( !input.isMember( "properties" ) || !input["properties"].isObject() ||
           input["properties"].empty() )
      {
        return SpatialToolResult::failure( "properties must be a non-empty object", "INVALID_PARAMETER",
                                          "validation" );
      }

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );
      QgsLayoutItem *item = LayoutService::instance().findItem( layout, itemRef );
      if ( !item )
        return SpatialToolResult::failure( "No item '" + itemRef.toStdString() + "' in layout",
                                           "NOT_FOUND", "validation" );

      QStringList applied, ignored;
      QString error;
      if ( !LayoutService::instance().applyItemProperties( item, input["properties"], &applied, &ignored,
                                                           &error ) )
      {
        return SpatialToolResult::failure( error.toStdString(), "RUNTIME_ERROR", "runtime" );
      }

      Json::Value out( Json::objectValue );
      out["applied"] = strArray( applied );
      out["ignored"] = strArray( ignored );
      out["item"] = LayoutService::instance().itemProperties( item );
      return SpatialToolResult::ok( out );
    }
};

class LayoutRemoveItemTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:remove_item"; }
    std::string displayName() const override { return "Remove layout item"; }
    std::string description() const override { return "Remove (undoably) one item from a layout."; }
    std::vector<std::string> tags() const override { return { "layout", "items", "delete" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["item"] = Json::Value( Json::objectValue );
      props["item"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "item" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["removed"] = Json::Value( true );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString itemRef = requireString( input, "item", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      QString error;
      if ( !LayoutService::instance().removeItem( layout, itemRef, &error ) )
        return SpatialToolResult::failure( error.toStdString(), "NOT_FOUND", "validation" );

      Json::Value out( Json::objectValue );
      out["removed"] = true;
      return SpatialToolResult::ok( out );
    }
};

class LayoutAlignDistributeTool final : public SpatialTool
{
  public:
    explicit LayoutAlignDistributeTool( bool align )
        : mAlign( align )
    {
    }
    std::string name() const override
    {
      return mAlign ? "layout:align_items" : "layout:distribute_items";
    }
    std::string displayName() const override { return mAlign ? "Align items" : "Distribute items"; }
    std::string description() const override
    {
      return mAlign ? "Align items: alignment = left|hcenter|right|top|vcenter|bottom (needs ≥2 items)."
                    : "Distribute items: distribution = "
                      "left|hcenter|hspace|right|top|vcenter|vspace|bottom (needs ≥3 items).";
    }
    std::vector<std::string> tags() const override
    {
      return { "layout", "items", mAlign ? "align" : "distribute" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["items"] = Json::Value( Json::objectValue );
      props["items"]["type"] = "array";
      props["items"]["items"] = Json::Value( Json::objectValue );
      props["items"]["items"]["type"] = "string";
      props[mAlign ? "alignment" : "distribution"] = Json::Value( Json::objectValue );
      props[mAlign ? "alignment" : "distribution"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "items" );
      required.append( mAlign ? "alignment" : "distribution" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["count"] = Json::Value( Json::objectValue );
      schema["properties"]["count"]["type"] = "integer";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      if ( !input.isMember( "items" ) || !input["items"].isArray() || input["items"].empty() )
        return SpatialToolResult::failure( "items must be a non-empty array", "INVALID_PARAMETER",
                                          "validation" );
      const QString mode =
          requireString( input, mAlign ? "alignment" : "distribution", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      QStringList ids;
      for ( const Json::Value &v : input["items"] )
        ids << QString::fromStdString( v.asString() );

      QString error;
      const bool ok = mAlign ? LayoutService::instance().alignItems( layout, ids, mode, &error )
                             : LayoutService::instance().distributeItems( layout, ids, mode, &error );
      if ( !ok )
        return SpatialToolResult::failure( error.toStdString(), "INVALID_PARAMETER", "validation" );

      Json::Value out( Json::objectValue );
      out["count"] = static_cast<Json::Int>( ids.size() );
      return SpatialToolResult::ok( out );
    }

  private:
    bool mAlign;
};

// ---------------------------------------------------------------------------
// Template / export tools
// ---------------------------------------------------------------------------

class LayoutTemplateTool final : public SpatialTool
{
  public:
    explicit LayoutTemplateTool( bool save )
        : mSave( save )
    {
    }
    std::string name() const override
    {
      return mSave ? "layout:save_template" : "layout:apply_template";
    }
    std::string displayName() const override
    {
      return mSave ? "Save layout as template" : "Create layout from template";
    }
    std::string description() const override
    {
      return mSave ? "Save a layout as a QGIS .qpt template file."
                   : "Create a new layout from a .qpt template file (QGIS-native round trip).";
    }
    std::vector<std::string> tags() const override
    {
      return { "layout", "template", mSave ? "save" : "load" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      // For save: the source layout. For load: the new layout name.
      props[mSave ? "layout" : "name"] = Json::Value( Json::objectValue );
      props[mSave ? "layout" : "name"]["type"] = "string";
      props["path"] = Json::Value( Json::objectValue );
      props["path"]["type"] = "string";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( mSave ? "layout" : "name" );
      required.append( "path" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["path"] = Json::Value( Json::objectValue );
      schema["properties"]["path"]["type"] = "string";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutOrName = requireString( input, mSave ? "layout" : "name", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString path = requireString( input, "path", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QString error;
      if ( mSave )
      {
        QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutOrName );
        if ( !layout )
          return SpatialToolResult::failure( "No layout named '" + layoutOrName.toStdString() + "'",
                                             "NOT_FOUND", "validation" );
        if ( !LayoutService::instance().saveTemplate( layout, path, &error ) )
          return SpatialToolResult::failure( error.toStdString(), "DATA_IO", "io" );
      }
      else
      {
        if ( LayoutService::instance().findLayout( layoutOrName ) )
          return SpatialToolResult::failure( "Layout already exists: " + layoutOrName.toStdString(),
                                             "INVALID_PARAMETER", "validation" );
        QgsPrintLayout *created = LayoutService::instance().loadTemplate( layoutOrName, path, &error );
        if ( !created )
          return SpatialToolResult::failure( error.toStdString(), "DATA_IO", "io" );

        Json::Value out( Json::objectValue );
        out["path"] = path.toStdString();
        // uuids are regenerated on template load; hand the caller fresh ids.
        out["items"] = LayoutService::instance().listItemInfos( created );
        return SpatialToolResult::ok( out );
      }

      Json::Value out( Json::objectValue );
      out["path"] = path.toStdString();
      return SpatialToolResult::ok( out );
    }

  private:
    bool mSave;
};

class LayoutExportTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:export"; }
    std::string displayName() const override { return "Export layout"; }
    std::string description() const override
    {
      return "Export a layout to png|jpg|pdf|svg at a given DPI. Raster exports are preflighted: "
             "requests whose RGBA buffers would exceed the memory guard are rejected with a hint "
             "to lower the DPI.";
    }
    std::vector<std::string> tags() const override { return { "layout", "export", "cartography" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["path"] = Json::Value( Json::objectValue );
      props["path"]["type"] = "string";
      props["format"] = Json::Value( Json::objectValue );
      props["format"]["type"] = "string";
      Json::Value formats( Json::arrayValue );
      formats.append( "png" );
      formats.append( "jpg" );
      formats.append( "pdf" );
      formats.append( "svg" );
      props["format"]["enum"] = formats;
      props["dpi"] = Json::Value( Json::objectValue );
      props["dpi"]["type"] = "number";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      required.append( "path" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["path"] = Json::Value( Json::objectValue );
      schema["properties"]["path"]["type"] = "string";
      schema["properties"]["bytes"] = Json::Value( Json::objectValue );
      schema["properties"]["bytes"]["type"] = "integer";
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      const QString path = requireString( input, "path", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      const QString format = input.isMember( "format" )
                                 ? QString::fromStdString( input["format"].asString() ).toLower()
                                 : QFileInfo( path ).suffix().toLower();
      const double dpi = input.isMember( "dpi" ) && input["dpi"].isNumeric() ? input["dpi"].asDouble()
                                                                            : 300.0;

      // Memory guard: hard cap identical to the GUI export preflight.
      constexpr qint64 kMaxExportBytes = 4LL * 1024 * 1024 * 1024;
      QString error;
      if ( !LayoutService::instance().exportLayout( layout, path, format, dpi, kMaxExportBytes, &error ) )
        return SpatialToolResult::failure( error.toStdString(), "EXPORT_FAILED", "runtime", true );

      Json::Value out( Json::objectValue );
      out["path"] = path.toStdString();
      out["format"] = format.toStdString();
      out["dpi"] = dpi;
      out["bytes"] = static_cast<Json::Int64>( QFileInfo( path ).size() );
      return SpatialToolResult::ok( out );
    }
};


// ---------------------------------------------------------------------------
// Cartographic output preflight (goal §10): the compose -> inspect ->
// PREFLIGHT -> repair -> export loop's gate. One place, shared vocabulary
// with the GUI export guard, so an agent can self-repair before exporting.
// ---------------------------------------------------------------------------
class LayoutPreflightTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:preflight"; }
    std::string displayName() const override { return "Preflight layout for export"; }
    std::string description() const override
    {
      return "Run cartographic output checks on a layout before export: missing title/legend/"
             "scale bar/north arrow, off-page items, overlapping items, empty maps, broken layer "
             "references, tiny fonts, legend/map mismatch, extreme export size, missing source "
             "note. Returns every issue with a severity and the offending item id — repair with "
             "layout:add_item / layout:set_item_properties, then layout:export.";
    }
    std::vector<std::string> tags() const override
    {
      return { "layout", "cartography", "preflight", "qa", "export" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["layout"]["description"] = "Layout name (see layout:list).";
      props["format"] = Json::Value( Json::objectValue );
      props["format"]["type"] = "string";
      props["format"]["description"] = "Export format for the size check: png|jpg|pdf|svg (default png).";
      props["dpi"] = Json::Value( Json::objectValue );
      props["dpi"]["type"] = "number";
      props["dpi"]["description"] = "Export DPI for the size check (default 300).";
      schema["properties"] = props;
      schema["required"] = Json::Value( Json::arrayValue );
      schema["required"].append( "layout" );
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["passed"] = Json::Value( Json::objectValue );
      schema["properties"]["issues"] = Json::Value( Json::arrayValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      const QString format = input.isMember( "format" )
                                 ? QString::fromStdString( input["format"].asString() ).toLower()
                                 : QStringLiteral( "png" );
      const double dpi = input.isMember( "dpi" ) && input["dpi"].isNumeric() ? input["dpi"].asDouble()
                                                                            : 300.0;

      Json::Value issues( Json::arrayValue );
      const auto addIssue = [&issues]( const char *check, const char *severity, const QString &item,
                                       const std::string &message ) {
        Json::Value issue( Json::objectValue );
        issue["check"] = check;
        issue["severity"] = severity;
        if ( !item.isEmpty() )
          issue["item"] = item.toStdString();
        issue["message"] = message;
        issues.append( issue );
      };
      const auto itemId = []( const QgsLayoutItem *item ) {
        const QString id = item->id();
        return id.isEmpty() ? QStringLiteral( "<unnamed %1>" ).arg( item->stringType() ) : id;
      };

      // Inventory by item type.
      QList<QgsLayoutItemLabel *> labels;
      QList<QgsLayoutItemLegend *> legends;
      QList<QgsLayoutItemMap *> maps;
      QList<QgsLayoutItemScaleBar *> scaleBars;
      QList<QgsLayoutItem *> pictures;
      const QList<QgsLayoutItem *> items = layout->items();
      for ( QgsLayoutItem *item : items )
      {
        if ( auto *label = dynamic_cast<QgsLayoutItemLabel *>( item ) )
          labels.append( label );
        else if ( auto *legend = dynamic_cast<QgsLayoutItemLegend *>( item ) )
          legends.append( legend );
        else if ( auto *map = dynamic_cast<QgsLayoutItemMap *>( item ) )
          maps.append( map );
        else if ( auto *bar = dynamic_cast<QgsLayoutItemScaleBar *>( item ) )
          scaleBars.append( bar );
        else
          pictures.append( item ); // north-arrow candidates (pictures) — refined below
      }
      QList<QgsLayoutItem *> northCandidates;
      for ( QgsLayoutItem *item : items )
      {
        const QString id = item->id().toLower();
        const QString strType = item->stringType().toLower();
        if ( id.contains( QStringLiteral( "north" ) ) || strType.contains( QStringLiteral( "arrow" ) )
             || ( item->type() == QgsLayoutItemRegistry::LayoutPicture
                  && id.contains( QStringLiteral( "north" ) ) ) )
          northCandidates.append( item );
      }

      // --- Empty layout / empty map -------------------------------------
      if ( items.isEmpty() )
      {
        addIssue( "empty_layout", "error", QString(), "The layout has no items at all." );
        Json::Value out( Json::objectValue );
        out["passed"] = false;
        out["issues"] = issues;
        return SpatialToolResult::ok( out );
      }
      if ( maps.isEmpty() )
        addIssue( "missing_map", "error", QString(), "No map frame on the layout." );
      for ( const QgsLayoutItemMap *map : maps )
      {
        if ( map->layers().isEmpty() )
          addIssue( "empty_map", "error", itemId( map ),
                    "Map frame has no layers assigned (nothing will render)." );
        int broken = 0;
        for ( const QgsMapLayer *layer : map->layers() )
          if ( !layer || !layer->isValid() )
            ++broken;
        if ( broken > 0 )
          addIssue( "broken_layer_reference", "error", itemId( map ),
                    "Map references " + std::to_string( broken )
                      + " layer(s) that are broken or no longer in the project." );
      }

      // --- Cartographic furniture (applicable only with a map) -----------
      if ( !maps.isEmpty() )
      {
        bool hasTitle = false;
        for ( const QgsLayoutItemLabel *label : labels )
          hasTitle = hasTitle || label->font().pointSizeF() >= 16.0;
        if ( !hasTitle )
          addIssue( "missing_title", "warning", QString(),
                    "No title-like label found (no text item with font >= 16 pt)." );
        if ( legends.isEmpty() )
          addIssue( "missing_legend", "warning", QString(), "No legend item on the layout." );
        if ( scaleBars.isEmpty() )
          addIssue( "missing_scale_bar", "warning", QString(), "No scale bar on the layout." );
        if ( northCandidates.isEmpty() )
          addIssue( "missing_north_arrow", "warning", QString(),
                    "No north arrow found (item whose id mentions 'north' / an arrow item)." );
        if ( legends.size() > maps.size() )
          addIssue( "legend_map_mismatch", "warning", QString(),
                    "More legends (" + std::to_string( legends.size() ) + ") than maps ("
                      + std::to_string( maps.size() ) + ")." ) );
      }

      // --- Source note ----------------------------------------------------
      bool hasSourceNote = false;
      for ( const QgsLayoutItemLabel *label : labels )
      {
        const QString text = label->text().toLower();
        if ( text.contains( QStringLiteral( "source" ) ) || text.contains( QStringLiteral( "来源" ) )
             || text.contains( QStringLiteral( "data source" ) ) )
          hasSourceNote = true;
      }
      if ( !hasSourceNote )
        addIssue( "missing_source_note", "warning", QString(),
                  "No source/data-source note found (labels containing 'source' or '来源')." );

      // --- Tiny fonts -----------------------------------------------------
      for ( const QgsLayoutItemLabel *label : labels )
      {
        if ( label->font().pointSizeF() > 0 && label->font().pointSizeF() < 6.0 )
          addIssue( "tiny_font", "warning", itemId( label ),
                    "Label font is below 6 pt — likely unreadable at export size." );
      }
      for ( const QgsLayoutItemLegend *legend : legends )
      {
        const QFont titleFont = legend->style( Qgis::LegendComponent::Title ).font();
        if ( titleFont.pointSizeF() > 0 && titleFont.pointSizeF() < 5.0 )
          addIssue( "tiny_font", "warning", itemId( legend ),
                    "Legend title font is below 5 pt — likely unreadable at export size." );
      }

      // --- Off-page and overlap --------------------------------------------
      QgsLayoutItemPage *page = layout->pageCollection()->page( 0 );
      if ( page )
      {
        const QRectF pageRect = page->mapToScene( page->rect() ).boundingRect();
        for ( QgsLayoutItem *item : items )
        {
          if ( item->type() == QgsLayoutItemRegistry::LayoutPage )
            continue;
          const QRectF sceneRect = item->mapToScene( item->rect() ).boundingRect();
          if ( !pageRect.intersects( sceneRect ) )
            addIssue( "off_page_item", "error", itemId( item ),
                      "Item lies entirely outside the first page." );
          else if ( !pageRect.contains( sceneRect ) )
            addIssue( "off_page_item", "warning", itemId( item ),
                      "Item partially exceeds the page bounds and may clip." );
        }
      }
      for ( int i = 0; i < items.size(); ++i )
      {
        for ( int j = i + 1; j < items.size(); ++j )
        {
          QgsLayoutItem *a = items[i];
          QgsLayoutItem *b = items[j];
          if ( a->type() == QgsLayoutItemRegistry::LayoutPage || b->type() == QgsLayoutItemRegistry::LayoutPage )
            continue;
          // Overlays on top of a map frame are legitimate cartography
          // (legends/labels/scale bars live ON the map): only flag
          // non-furniture pairs or furniture overlapping non-map items.
          const bool aIsMap = a->type() == QgsLayoutItemRegistry::LayoutMap;
          const bool bIsMap = b->type() == QgsLayoutItemRegistry::LayoutMap;
          if ( aIsMap || bIsMap )
            continue;
          const QRectF ra = a->mapToScene( a->rect() ).boundingRect();
          const QRectF rb = b->mapToScene( b->rect() ).boundingRect();
          if ( ra.intersects( rb ) )
            addIssue( "overlap", "warning", itemId( a ),
                      "Item overlaps '" + itemId( b ).toStdString()
                        + "' (both are non-map items)." );
        }
      }

      // --- Extreme export size (same hard cap as layout:export) -----------
      if ( page && ( format == "png" || format == "jpg" ) )
      {
        constexpr qint64 kMaxExportBytes = 4LL * 1024 * 1024 * 1024;
        const double pxW = page->rect().width() / 25.4 * dpi;
        const double pxH = page->rect().height() / 25.4 * dpi;
        const qint64 bytes = static_cast<qint64>( pxW ) * static_cast<qint64>( pxH ) * 4;
        if ( bytes > kMaxExportBytes )
          addIssue( "extreme_export_size", "error", QString(),
                    "Raster export at " + std::to_string( static_cast<int>( dpi ) )
                      + " DPI would need ~"
                      + std::to_string( bytes / ( 1024 * 1024 ) ) + " MiB (cap "
                      + std::to_string( kMaxExportBytes / ( 1024 * 1024 ) ) + " MiB)." );
        else if ( bytes > kMaxExportBytes / 4 )
          addIssue( "extreme_export_size", "warning", QString(),
                    "Raster export at this DPI is unusually large (~"
                      + std::to_string( bytes / ( 1024 * 1024 ) ) + " MiB)." );
      }

      bool hasError = false;
      for ( const Json::Value &issue : issues )
        hasError = hasError || issue["severity"].asString() == "error";
      Json::Value out( Json::objectValue );
      out["passed"] = !hasError;
      out["error_count"] = static_cast<Json::Int>(
        std::count_if( issues.begin(), issues.end(), []( const Json::Value &issue ) {
          return issue["severity"].asString() == "error";
        } ) );
      out["warning_count"] = static_cast<Json::Int>( issues.size() - out["error_count"].asInt() );
      out["issues"] = issues;
      out["layout"] = layoutName.toStdString();
      return SpatialToolResult::ok( out );
    }
};

class LayoutAutoArrangeTool final : public SpatialTool
{
  public:
    std::string name() const override { return "layout:auto_arrange"; }
    std::string displayName() const override { return "Auto arrange thematic composition"; }
    std::string description() const override
    {
      return "Create/arrange a classic thematic composition on a layout: title top, map primary "
             "region, legend right, scale bar bottom, north arrow top-right, source note bottom. "
             "Items with non-auto ids are never moved (suggest/auto-arrange only).";
    }
    std::vector<std::string> tags() const override { return { "layout", "cartography", "auto" }; }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      props["layout"] = Json::Value( Json::objectValue );
      props["layout"]["type"] = "string";
      props["apply"] = Json::Value( Json::objectValue );
      props["apply"]["type"] = "boolean";
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "layout" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["components"] = Json::Value( Json::arrayValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      std::string err;
      const QString layoutName = requireString( input, "layout", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );

      QgsPrintLayout *layout = LayoutService::instance().findLayout( layoutName );
      if ( !layout )
        return SpatialToolResult::failure( "No layout named '" + layoutName.toStdString() + "'",
                                           "NOT_FOUND", "validation" );

      const bool apply = !input.isMember( "apply" ) || input["apply"].asBool();
      QString error;
      Json::Value out = LayoutService::instance().autoArrange( layout, apply, &error );
      if ( !error.isEmpty() )
        return SpatialToolResult::failure( error.toStdString(), "NOT_FOUND", "validation" );
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerBuiltinLayoutTools()
{
  static const std::vector<SpatialToolPtr> kLayoutTools = {
    std::make_shared<LayoutListTool>(),
    std::make_shared<LayoutCreateTool>(),
    std::make_shared<LayoutProjectTool>( true ),
    std::make_shared<LayoutProjectTool>( false ),
    std::make_shared<LayoutListItemsTool>(),
    std::make_shared<LayoutGetItemPropertiesTool>(),
    std::make_shared<LayoutAddItemTool>(),
    std::make_shared<LayoutSetItemPropertiesTool>(),
    std::make_shared<LayoutRemoveItemTool>(),
    std::make_shared<LayoutAlignDistributeTool>( true ),
    std::make_shared<LayoutAlignDistributeTool>( false ),
    std::make_shared<LayoutTemplateTool>( true ),
    std::make_shared<LayoutTemplateTool>( false ),
    std::make_shared<LayoutAutoArrangeTool>(),
    std::make_shared<LayoutPreflightTool>(),
    std::make_shared<LayoutExportTool>(),
  };
  auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
  for ( const auto &tool : kLayoutTools )
    registry.registerTool( tool );
}

} // namespace sicnu::agent::layout_tools
