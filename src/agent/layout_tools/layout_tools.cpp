// src/agent/layout_tools/layout_tools.cpp
#include "layout_tools.h"

#include "layout_service.h"

#include <qgslayout.h>
#include <qgslayoutitem.h>
#include <qgsprintlayout.h>

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
    std::make_shared<LayoutExportTool>(),
  };
  auto &registry = sicnu::agent::spatial_tools::SpatialToolRegistry::instance();
  for ( const auto &tool : kLayoutTools )
    registry.registerTool( tool );
}

} // namespace sicnu::agent::layout_tools
