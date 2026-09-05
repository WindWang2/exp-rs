// src/agent/symbology/symbology_tools.cpp
#include "symbology_tools.h"

#include "../commands/workspace_commands.h"

#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgssinglebandpseudocolorrenderer.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgscategorizedsymbolrenderer.h>
#include <qgsgraduatedsymbolrenderer.h>
#include <qgsvectorlayer.h>
#include <qgsfeatureiterator.h>
#include <qgsproject.h>
#include <qgsrastershader.h>
#include <qgscolorrampshader.h>
#include <qgssymbol.h>
#include <qgsrendererregistry.h>
#include <qgsrasterrendererregistry.h>
#include <qgsrastershader.h>
#include <qgscolorrampimpl.h>

#include <algorithm>
#include <map>
#include <set>

namespace sicnu::agent::symbology {

using sicnu::agent::commands::WorkspaceCommand;
using sicnu::agent::commands::WorkspaceCommandStack;
using sicnu::agent::spatial_tools::SpatialTool;
using sicnu::agent::spatial_tools::SpatialToolRegistry;
using sicnu::agent::spatial_tools::SpatialToolResult;
using sicnu::agent::spatial_tools::requireStringField;

namespace {

constexpr int kMaxCategories = 64;
constexpr int kMaxScanFeatures = 20000;

/// Resolves a target (workspace layer id, uuid, or layer name) to a project
/// layer. Tolerates the headless case (no project layers).
QgsMapLayer *resolveTarget( const QString &target )
{
  QgsProject *project = QgsProject::instance();
  if ( !project || target.isEmpty() )
    return nullptr;
  const QList<QgsMapLayer *> byName = project->mapLayersByName( target );
  if ( !byName.isEmpty() )
    return byName.first();
  return project->mapLayer( target );
}

/// Bounded renderer summary for describe/apply responses.
Json::Value rendererSummary( QgsMapLayer *layer )
{
  Json::Value summary( Json::objectValue );
  if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
  {
    summary["type"] = "raster";
    if ( const QgsRasterRenderer *renderer = raster->renderer() )
      summary["renderer"] = renderer->type().toStdString();
    return summary;
  }
  if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
  {
    summary["type"] = "vector";
    if ( const QgsFeatureRenderer *renderer = vector->renderer() )
    {
      summary["renderer"] = renderer->type().toStdString();
      if ( auto *categorized = dynamic_cast<const QgsCategorizedSymbolRenderer *>( renderer ) )
      {
        summary["field"] = categorized->classAttribute().toStdString();
        summary["categories"] =
          static_cast<Json::Int>( std::min<qsizetype>( categorized->categories().size(),
                                                       static_cast<qsizetype>( kMaxCategories ) ) );
      }
      else if ( auto *graduated = dynamic_cast<const QgsGraduatedSymbolRenderer *>( renderer ) )
      {
        summary["field"] = graduated->classAttribute().toStdString();
        summary["classes"] =
          static_cast<Json::Int>( std::min<qsizetype>( graduated->ranges().size(),
                                                       static_cast<qsizetype>( kMaxCategories ) ) );
      }
    }
  }
  return summary;
}

std::pair<QColor, QColor> rampPair( const QString &rampName )
{
  static const std::map<QString, std::pair<QColor, QColor>> kRamps = {
    { "spectral", { QColor( 0xd7, 0x30, 0x27 ), QColor( 0x1a, 0x96, 0x41 ) } },
    { "heat", { QColor( 0xff, 0xff, 0xcc ), QColor( 0x80, 0x00, 0x26 ) } },
    { "blue", { QColor( 0xf7, 0xfb, 0xff ), QColor( 0x08, 0x30, 0x6b ) } },
    { "sequential", { QColor( 0xf7, 0xfb, 0xf5 ), QColor( 0x00, 0x44, 0x1b ) } },
  };
  const auto it = kRamps.find( rampName.toLower() );
  return it != kRamps.end() ? it->second : kRamps.at( QStringLiteral( "spectral" ) );
}

QgsColorRamp *makeRamp( const QString &rampName )
{
  const auto [ from, to ] = rampPair( rampName );
  return new QgsGradientColorRamp( from, to );
}

QgsMapLayer *requireLayer( const Json::Value &input, SpatialToolResult *result )
{
  std::string error;
  const QString target = requireStringField( input, "target", &error );
  if ( !error.empty() )
  {
    *result = SpatialToolResult::failure( error, "INVALID_PARAMETER", "validation" );
    return nullptr;
  }
  QgsMapLayer *layer = resolveTarget( target );
  if ( !layer )
  {
    *result = SpatialToolResult::failure( "Cannot resolve target layer: " + target.toStdString(),
                                          "NOT_FOUND", "validation", false );
    return nullptr;
  }
  return layer;
}

class DescribeSymbologyTool final : public SpatialTool
{
  public:
    std::string name() const override { return "symbology:describe"; }
    std::string displayName() const override { return "Describe symbology"; }
    std::string description() const override
    {
      return "Reads the current renderer of a workspace layer (raster renderer type, or vector "
             "renderer kind + class field + class count) as structured JSON — the read side of "
             "the symbology apply/rollback cycle.";
    }
    std::vector<std::string> tags() const override
    {
      return { "symbology", "renderer", "style", "describe" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      target["description"] = "Layer name, uuid, or workspace layer id";
      props["target"] = target;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["renderer"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult pre;
      QgsMapLayer *layer = requireLayer( input, &pre );
      if ( !layer )
        return pre;
      Json::Value out( Json::objectValue );
      out["target"] = input["target"].asString();
      out["layer_name"] = layer->name().toStdString();
      out["renderer"] = rendererSummary( layer );
      return SpatialToolResult::ok( out );
    }
};

class ApplyCategoricalTool final : public SpatialTool
{
  public:
    std::string name() const override { return "symbology:apply_categorical"; }
    std::string displayName() const override { return "Apply categorical symbology"; }
    std::string description() const override
    {
      return "Applies a field-driven categorized (unique-value) renderer to a vector layer "
             "(≤ 64 categories from a bounded feature scan), with full rollback via "
             "workspace:undo. Input: {target, field, palette?, max_categories?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "symbology", "categorical", "renderer", "style" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      props["target"] = target;
      Json::Value field( Json::objectValue );
      field["type"] = "string";
      props["field"] = field;
      Json::Value palette( Json::objectValue );
      palette["type"] = "string";
      palette["description"] = "Ramp name: spectral|heat|blue|sequential (default spectral)";
      props["palette"] = palette;
      Json::Value maxCategories( Json::objectValue );
      maxCategories["type"] = "integer";
      props["max_categories"] = maxCategories;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      required.append( "field" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["categories"] = Json::Value( Json::objectValue );
      schema["properties"]["undoable"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult pre;
      QgsMapLayer *layer = requireLayer( input, &pre );
      if ( !layer )
        return pre;
      auto *vector = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vector )
        return SpatialToolResult::failure( "Categorical symbology needs a vector layer",
                                           "INVALID_PARAMETER", "validation" );
      std::string err;
      const QString field = requireStringField( input, "field", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      if ( vector->fields().lookupField( field ) < 0 )
        return SpatialToolResult::failure( "Field not found: " + field.toStdString(),
                                           "INVALID_PARAMETER", "validation" );
      const QString rampName = input.isMember( "palette" ) && input["palette"].isString()
                                 ? QString::fromStdString( input["palette"].asString() )
                                 : QStringLiteral( "spectral" );
      int maxCategories = kMaxCategories;
      if ( input.isMember( "max_categories" ) && input["max_categories"].isInt() )
        maxCategories = std::clamp( input["max_categories"].asInt(), 2, kMaxCategories );

      // Bounded unique-value scan.
      std::set<QString> values;
      QgsFeatureRequest request;
      request.setLimit( kMaxScanFeatures );
      request.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
      request.setSubsetOfAttributes( QStringList{ field }, vector->fields() );
      QgsFeatureIterator it = vector->getFeatures( request );
      QgsFeature feature;
      while ( it.nextFeature( feature ) )
      {
        const QVariant value = feature.attribute( field );
        if ( value.isValid() && !value.isNull() )
          values.insert( value.toString() );
        if ( static_cast<int>( values.size() ) >= maxCategories )
          break;
      }
      it.close();
      if ( values.empty() )
        return SpatialToolResult::failure( "No non-null values found for field",
                                           "EMPTY_DATA", "runtime", false );

      // Build the renderer.
      QgsCategoryList categories;
      int index = 0;
      QgsColorRamp *ramp = makeRamp( rampName );
      for ( const QString &value : values )
      {
        QgsSymbol *symbol = QgsSymbol::defaultSymbol( vector->geometryType() );
        symbol->setColor( ramp->color( values.size() > 1
                                         ? static_cast<double>( index ) / ( values.size() - 1 )
                                         : 0.0 ) );
        categories.append( QgsRendererCategory( value, symbol, value ) );
        ++index;
      }
      delete ramp;
      auto *renderer = new QgsCategorizedSymbolRenderer( field, categories );

      // Previous renderer snapshot for undo.
      QgsFeatureRenderer *previous = vector->renderer() ? vector->renderer()->clone() : nullptr;
      const QString label =
        QStringLiteral( "Apply categorical symbology on %1" ).arg( vector->name() );
      const QString txn = WorkspaceCommandStack::instance().beginTransaction( label );
      QgsVectorLayer *vectorLayer = vector;
      WorkspaceCommandStack::instance().addCommand(
        txn, WorkspaceCommand{ QStringLiteral( "set categorized renderer" ),
                               [ vectorLayer, renderer ] {
                                 vectorLayer->setRenderer( renderer );
                                 vectorLayer->triggerRepaint();
                                 return true;
                               },
                               [ vectorLayer, previous ] {
                                 if ( previous )
                                 {
                                   vectorLayer->setRenderer( previous );
                                   vectorLayer->triggerRepaint();
                                   return true;
                                 }
                                 return false;
                               } } );
      QString commitError;
      if ( !WorkspaceCommandStack::instance().commit( txn, &commitError ) )
        return SpatialToolResult::failure( commitError.toStdString(), "COMMIT_FAILED", "runtime", false );

      Json::Value out( Json::objectValue );
      out["applied"] = true;
      out["categories"] = static_cast<Json::Int>( categories.size() );
      out["truncated"] = values.size() >= static_cast<size_t>( maxCategories );
      out["renderer"] = rendererSummary( vector );
      out["undoable"] = previous != nullptr;
      return SpatialToolResult::ok( out );
    }
};

class ApplyGraduatedTool final : public SpatialTool
{
  public:
    std::string name() const override { return "symbology:apply_graduated"; }
    std::string displayName() const override { return "Apply graduated symbology"; }
    std::string description() const override
    {
      return "Applies an equal-interval graduated renderer to a numeric field of a vector layer "
             "(bounded scan for min/max), with rollback via workspace:undo. Input: {target, "
             "field, classes=5, palette?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "symbology", "graduated", "renderer", "style" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      props["target"] = target;
      Json::Value field( Json::objectValue );
      field["type"] = "string";
      props["field"] = field;
      Json::Value classes( Json::objectValue );
      classes["type"] = "integer";
      classes["description"] = "Class count (default 5, range 2-16)";
      props["classes"] = classes;
      Json::Value palette( Json::objectValue );
      palette["type"] = "string";
      props["palette"] = palette;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      required.append( "field" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["classes"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult pre;
      QgsMapLayer *layer = requireLayer( input, &pre );
      if ( !layer )
        return pre;
      auto *vector = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vector )
        return SpatialToolResult::failure( "Graduated symbology needs a vector layer",
                                           "INVALID_PARAMETER", "validation" );
      std::string err;
      const QString field = requireStringField( input, "field", &err );
      if ( !err.empty() )
        return SpatialToolResult::failure( err, "INVALID_PARAMETER", "validation" );
      if ( vector->fields().lookupField( field ) < 0 )
        return SpatialToolResult::failure( "Field not found: " + field.toStdString(),
                                           "INVALID_PARAMETER", "validation" );
      int classCount = 5;
      if ( input.isMember( "classes" ) && input["classes"].isInt() )
        classCount = std::clamp( input["classes"].asInt(), 2, 16 );
      const QString rampName = input.isMember( "palette" ) && input["palette"].isString()
                                 ? QString::fromStdString( input["palette"].asString() )
                                 : QStringLiteral( "spectral" );

      // Bounded min/max scan.
      double minValue = std::numeric_limits<double>::infinity();
      double maxValue = -std::numeric_limits<double>::infinity();
      QgsFeatureRequest request;
      request.setLimit( kMaxScanFeatures );
      request.setFlags( Qgis::FeatureRequestFlag::NoGeometry );
      request.setSubsetOfAttributes( QStringList{ field }, vector->fields() );
      QgsFeatureIterator it = vector->getFeatures( request );
      QgsFeature feature;
      while ( it.nextFeature( feature ) )
      {
        const QVariant value = feature.attribute( field );
        if ( !value.isValid() || value.isNull() )
          continue;
        bool ok = false;
        const double numeric = value.toDouble( &ok );
        if ( !ok || !std::isfinite( numeric ) )
          continue;
        minValue = std::min( minValue, numeric );
        maxValue = std::max( maxValue, numeric );
      }
      it.close();
      if ( !std::isfinite( minValue ) || !std::isfinite( maxValue ) )
        return SpatialToolResult::failure( "No numeric values found for field", "EMPTY_DATA",
                                           "runtime", false );
      if ( maxValue <= minValue )
        maxValue = minValue + 1.0;

      const double interval = ( maxValue - minValue ) / classCount;
      QgsRangeList ranges;
      QgsColorRamp *ramp = makeRamp( rampName );
      for ( int i = 0; i < classCount; ++i )
      {
        const double lower = minValue + i * interval;
        const double upper = i == classCount - 1 ? maxValue + 1e-9 : minValue + ( i + 1 ) * interval;
        QgsSymbol *symbol = QgsSymbol::defaultSymbol( vector->geometryType() );
        symbol->setColor( ramp->color( static_cast<double>( i ) / ( classCount - 1 ) ) );
        ranges.append( QgsRendererRange( lower, upper, symbol, QString::number( i + 1 ) ) );
      }
      delete ramp;
      auto *renderer = new QgsGraduatedSymbolRenderer( field, ranges );

      QgsFeatureRenderer *previous = vector->renderer() ? vector->renderer()->clone() : nullptr;
      const QString label = QStringLiteral( "Apply graduated symbology on %1" ).arg( vector->name() );
      const QString txn = WorkspaceCommandStack::instance().beginTransaction( label );
      QgsVectorLayer *vectorLayer = vector;
      WorkspaceCommandStack::instance().addCommand(
        txn, WorkspaceCommand{ QStringLiteral( "set graduated renderer" ),
                               [ vectorLayer, renderer ] {
                                 vectorLayer->setRenderer( renderer );
                                 vectorLayer->triggerRepaint();
                                 return true;
                               },
                               [ vectorLayer, previous ] {
                                 if ( previous )
                                 {
                                   vectorLayer->setRenderer( previous );
                                   vectorLayer->triggerRepaint();
                                   return true;
                                 }
                                 return false;
                               } } );
      QString commitError;
      if ( !WorkspaceCommandStack::instance().commit( txn, &commitError ) )
        return SpatialToolResult::failure( commitError.toStdString(), "COMMIT_FAILED", "runtime", false );

      Json::Value out( Json::objectValue );
      out["applied"] = true;
      out["classes"] = classCount;
      Json::Value range( Json::objectValue );
      range["min"] = minValue;
      range["max"] = maxValue;
      out["value_range"] = range;
      out["renderer"] = rendererSummary( vector );
      out["undoable"] = previous != nullptr;
      return SpatialToolResult::ok( out );
    }
};

class ApplyRasterRampTool final : public SpatialTool
{
  public:
    std::string name() const override { return "symbology:apply_raster_ramp"; }
    std::string displayName() const override { return "Apply raster color ramp"; }
    std::string description() const override
    {
      return "Applies a single-band pseudo-color ramp to a raster layer (optionally with "
             "explicit min/max, otherwise a bounded decimated scan), with rollback via "
             "workspace:undo. Input: {target, band=1, palette?, min?, max?}.";
    }
    std::vector<std::string> tags() const override
    {
      return { "symbology", "raster", "ramp", "stretch", "style" };
    }
    Json::Value inputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      Json::Value props( Json::objectValue );
      Json::Value target( Json::objectValue );
      target["type"] = "string";
      props["target"] = target;
      Json::Value band( Json::objectValue );
      band["type"] = "integer";
      props["band"] = band;
      Json::Value palette( Json::objectValue );
      palette["type"] = "string";
      props["palette"] = palette;
      Json::Value min( Json::objectValue );
      min["type"] = "number";
      props["min"] = min;
      Json::Value max( Json::objectValue );
      max["type"] = "number";
      props["max"] = max;
      schema["properties"] = props;
      Json::Value required( Json::arrayValue );
      required.append( "target" );
      schema["required"] = required;
      return schema;
    }
    Json::Value outputSchema() const override
    {
      Json::Value schema( Json::objectValue );
      schema["type"] = "object";
      schema["properties"]["applied"] = Json::Value( Json::objectValue );
      return schema;
    }
    SpatialToolResult execute( const Json::Value &input ) override
    {
      SpatialToolResult pre;
      QgsMapLayer *layer = requireLayer( input, &pre );
      if ( !layer )
        return pre;
      auto *raster = qobject_cast<QgsRasterLayer *>( layer );
      if ( !raster )
        return SpatialToolResult::failure( "Raster ramp needs a raster layer", "INVALID_PARAMETER",
                                           "validation" );
      const int band = input.isMember( "band" ) && input["band"].isInt()
                         ? std::clamp( input["band"].asInt(), 1, raster->bandCount() )
                         : 1;
      double minValue = input.isMember( "min" ) && input["min"].isNumeric() ? input["min"].asDouble()
                                                                            : 0.0;
      double maxValue = input.isMember( "max" ) && input["max"].isNumeric() ? input["max"].asDouble()
                                                                            : 0.0;
      const bool explicitRange = input.isMember( "min" ) && input.isMember( "max" );
      if ( input.isMember( "min" ) && !input.isMember( "max" ) )
        return SpatialToolResult::failure( "min and max must be provided together",
                                           "INVALID_PARAMETER", "validation" );
      if ( !explicitRange )
      {
        // Bounded decimated scan via the band statistics interface (cached when
        // available, approximate otherwise) — never a full-res GDAL sweep.
        const QgsRasterBandStats stats = raster->dataProvider()->bandStatistics(
          band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max, QgsRectangle(),
          512 * 512 );
        minValue = stats.minimumValue;
        maxValue = stats.maximumValue;
        if ( !std::isfinite( minValue ) || !std::isfinite( maxValue ) )
          return SpatialToolResult::failure( "Statistics unavailable for band", "EMPTY_DATA",
                                             "runtime", true );
        if ( maxValue <= minValue )
          maxValue = minValue + 1.0;
      }
      else if ( maxValue <= minValue )
      {
        return SpatialToolResult::failure( "max must be greater than min", "INVALID_PARAMETER",
                                           "validation" );
      }

      const QString rampName = input.isMember( "palette" ) && input["palette"].isString()
                                 ? QString::fromStdString( input["palette"].asString() )
                                 : QStringLiteral( "heat" );
      QgsColorRamp *ramp = makeRamp( rampName );
      QgsRasterShader *shader = new QgsRasterShader();
      shader->setRasterShaderFunction( new QgsColorRampShader( minValue, maxValue, ramp ) );
      auto *renderer = new QgsSingleBandPseudoColorRenderer( raster->dataProvider(), band, shader );

      QgsRasterRenderer *previous = raster->renderer() ? raster->renderer()->clone() : nullptr;
      const QString label = QStringLiteral( "Apply raster ramp on %1" ).arg( raster->name() );
      const QString txn = WorkspaceCommandStack::instance().beginTransaction( label );
      QgsRasterLayer *rasterLayer = raster;
      WorkspaceCommandStack::instance().addCommand(
        txn, WorkspaceCommand{ QStringLiteral( "set pseudo-color renderer" ),
                               [ rasterLayer, renderer ] {
                                 rasterLayer->setRenderer( renderer );
                                 rasterLayer->triggerRepaint();
                                 return true;
                               },
                               [ rasterLayer, previous ] {
                                 if ( previous )
                                 {
                                   rasterLayer->setRenderer( previous );
                                   rasterLayer->triggerRepaint();
                                   return true;
                                 }
                                 return false;
                               } } );
      QString commitError;
      if ( !WorkspaceCommandStack::instance().commit( txn, &commitError ) )
        return SpatialToolResult::failure( commitError.toStdString(), "COMMIT_FAILED", "runtime", false );

      Json::Value out( Json::objectValue );
      out["applied"] = true;
      out["band"] = band;
      Json::Value range( Json::objectValue );
      range["min"] = minValue;
      range["max"] = maxValue;
      out["value_range"] = range;
      out["renderer"] = rendererSummary( raster );
      out["undoable"] = previous != nullptr;
      return SpatialToolResult::ok( out );
    }
};

} // namespace

void registerSymbologyTools()
{
  static const bool registered = [] {
    SpatialToolRegistry::instance().registerTool( std::make_shared<DescribeSymbologyTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<ApplyCategoricalTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<ApplyGraduatedTool>() );
    SpatialToolRegistry::instance().registerTool( std::make_shared<ApplyRasterRampTool>() );
    return true;
  }();
  Q_UNUSED( registered );
}

} // namespace sicnu::agent::symbology
