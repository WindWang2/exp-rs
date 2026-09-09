// src/agent/cartography/style_compiler.cpp
#include "style_compiler.h"

#include "design_tokens.h"
#include "style_spec.h"

#include <qgscolorramp.h>
#include <qgscolorrampshader.h>
#include <qgscategorizedsymbolrenderer.h>
#include <qgscontrastenhancement.h>
#include <qgsfillsymbollayer.h>
#include <qgsfillsymbol.h>
#include <qgsgraduatedsymbolrenderer.h>
#include <qgslinesymbollayer.h>
#include <qgslinesymbol.h>
#include <qgsmaplayer.h>
#include <qgsmarkersymbollayer.h>
#include <qgsmarkersymbol.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgspalettedrasterrenderer.h>
#include <qgspallabeling.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgsrastershader.h>
#include <qgsrulebasedrenderer.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgssinglebandpseudocolorrenderer.h>
#include <qgssinglesymbolrenderer.h>
#include <qgssymbol.h>
#include <qgsvectorlayer.h>
#include <qgsvectorlayerlabeling.h>

#include <QColor>
#include <QFont>

#include <functional>

namespace sicnu::agent::cartography {

namespace {

QString colorString( const Json::Value &value, const QString &fallback )
{
  return value.isString() ? QString::fromStdString( value.asString() ) : fallback;
}

/// Palette stops for a raster classification `ramp` entry: either a
/// token-resolved hex array or a palette name from the style's token set.
Json::Value rampStops( const Json::Value &rampEntry, const Json::Value &tokenSet )
{
  if ( rampEntry.isArray() )
    return rampEntry;
  if ( !rampEntry.isString() || rampEntry.asString().empty() )
    return Json::Value();
  const std::string name = rampEntry.asString();
  if ( tokenSet.isObject() )
  {
    const Json::Value palette = tokenPalette( tokenSet, name );
    if ( palette.isArray() && !palette.empty() )
      return palette;
  }
  return Json::Value();
}

QgsSymbol *buildSymbol( const Json::Value &entry, const QString &colorFallback,
                        Qgis::GeometryType geometry )
{
  const QColor color( colorString( entry.get( "color", Json::Value() ), colorFallback ) );
  const Json::Value symbol = entry.get( "symbol", Json::Value() );
  const double widthMm = symbol.isMember( "width_mm" ) && symbol["width_mm"].isNumeric()
                           ? symbol["width_mm"].asDouble()
                           : 0.3;
  const std::string shape = symbol.isMember( "shape" ) && symbol["shape"].isString()
                              ? symbol["shape"].asString()
                              : "circle";
  switch ( geometry )
  {
    case Qgis::GeometryType::Line:
    {
      QVariantMap props;
      props[QStringLiteral( "color" )] = color.name();
      props[QStringLiteral( "width" )] = QString::number( widthMm );
      props[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
      return QgsLineSymbol::createSimple( props ).release();
    }
    case Qgis::GeometryType::Point:
    {
      QVariantMap props;
      props[QStringLiteral( "name" )] = QString::fromStdString( shape );
      props[QStringLiteral( "color" )] = color.name();
      props[QStringLiteral( "outline_color" )] = color.darker( 130 ).name();
      return QgsMarkerSymbol::createSimple( props ).release();
    }
    default:
    {
      QVariantMap props;
      props[QStringLiteral( "color" )] = color.name();
      props[QStringLiteral( "outline_color" )] = color.darker( 130 ).name();
      props[QStringLiteral( "outline_width" )] = QString::number( widthMm );
      props[QStringLiteral( "width_unit" )] = QStringLiteral( "MM" );
      return QgsFillSymbol::createSimple( props ).release();
    }
  }
}

/// Builds classification entries when the style names a ramp instead of
/// explicit classes: the ramp stops sample across the stretch min/max.
QList<QgsColorRampShader::ColorRampItem> rampShaderItems( const Json::Value &rasterBlock,
                                                          const Json::Value &tokenSet,
                                                          const std::string &mode )
{
  const Json::Value classification = rasterBlock.get( "classification", Json::Value() );
  Json::Value classes = classification.get( "classes", Json::Value() );
  if ( !classes.isArray() || classes.empty() )
  {
    const Json::Value stretch = rasterBlock.get( "stretch", Json::Value() );
    const double min = stretch.isMember( "min" ) && stretch["min"].isNumeric() ? stretch["min"].asDouble() : 0.0;
    const double max = stretch.isMember( "max" ) && stretch["max"].isNumeric() ? stretch["max"].asDouble() : 1.0;
    Json::Value stops = rampStops( classification.get( "ramp", Json::Value() ), tokenSet );
    if ( !stops.isArray() || stops.empty() )
    {
      const char *fallback[] = { "#440154", "#3b528b", "#21918c", "#5ec962", "#fde725" };
      stops = Json::Value( Json::arrayValue );
      for ( const char *hex : fallback )
        stops.append( hex );
    }
    classes = Json::Value( Json::arrayValue );
    const int stopCount = static_cast<int>( stops.size() );
    for ( int i = 0; i < stopCount; ++i )
    {
      Json::Value entry( Json::objectValue );
      const double from = min + ( max - min ) * i / static_cast<double>( stopCount );
      const double to = min + ( max - min ) * ( i + 1 ) / static_cast<double>( stopCount );
      entry["min"] = from;
      entry["max"] = mode == "continuous" ? max : to;
      entry["color"] = stops[i];
      entry["label"] = QStringLiteral( "%1-%2" ).arg( from, 0, 'f', 2 ).arg( to, 0, 'f', 2 ).toStdString();
      classes.append( entry );
    }
  }
  QList<QgsColorRampShader::ColorRampItem> items;
  for ( const auto &entry : classes )
  {
    if ( !entry.isObject() || !entry.isMember( "color" ) )
      continue;
    QgsColorRampShader::ColorRampItem item;
    item.value = entry.isMember( "max" ) && entry["max"].isNumeric() ? entry["max"].asDouble() : 0.0;
    item.color = QColor( colorString( entry["color"], QStringLiteral( "#000000" ) ) );
    item.label = entry.isMember( "label" ) && entry["label"].isString()
                   ? QString::fromStdString( entry["label"].asString() )
                   : QString();
    items << item;
  }
  return items;
}

bool applyRasterBlock( QgsRasterLayer *raster, const Json::Value &rasterBlock,
                       const Json::Value &tokenSet, QStringList &applied, QStringList &problems )
{
  const std::string renderertype =
    rasterBlock.isMember( "renderertype" ) && rasterBlock["renderertype"].isString()
      ? rasterBlock["renderertype"].asString()
      : "";
  const int band = rasterBlock.isMember( "band" ) && rasterBlock["band"].isIntegral()
                     ? rasterBlock["band"].asInt()
                     : 1;
  if ( band < 1 || band > raster->bandCount() )
  {
    problems << QStringLiteral( "raster.band %1 out of range (1-%2)" )
                  .arg( band )
                  .arg( raster->bandCount() );
    return false;
  }

  if ( renderertype == "singleband_gray" )
  {
    auto *renderer = new QgsSingleBandGrayRenderer( raster->dataProvider(), band );
    const Json::Value stretch = rasterBlock.get( "stretch", Json::Value() );
    if ( stretch.isMember( "min" ) && stretch["min"].isNumeric() && stretch.isMember( "max" ) &&
         stretch["max"].isNumeric() )
    {
      const Qgis::DataType bandType =
        raster->dataProvider() ? raster->dataProvider()->dataType( band ) : Qgis::DataType::Float32;
      auto *enhancement = new QgsContrastEnhancement( bandType );
      enhancement->setMinimumValue( stretch["min"].asDouble() );
      enhancement->setMaximumValue( stretch["max"].asDouble() );
      enhancement->setContrastEnhancementAlgorithm( QgsContrastEnhancement::StretchToMinimumMaximum );
      renderer->setContrastEnhancement( enhancement );
    }
    raster->setRenderer( renderer );
    applied << QStringLiteral( "raster:singleband_gray(band=%1)" ).arg( band );
    return true;
  }
  if ( renderertype == "singleband_pseudocolor" || renderertype == "paletted" )
  {
    const Json::Value classification = rasterBlock.get( "classification", Json::Value() );
    const std::string mode = classification.isMember( "mode" ) && classification["mode"].isString()
                               ? classification["mode"].asString()
                               : "discrete";
    if ( renderertype == "paletted" )
    {
      // Paletted: classes keyed by their integer `min` value.
      const Json::Value classes = classification.get( "classes", Json::Value() );
      if ( !classes.isArray() || classes.empty() )
      {
        problems << QStringLiteral( "paletted rendering needs explicit classes" );
        return false;
      }
      QgsPalettedRasterRenderer::ClassData paletteClasses;
      int index = 0;
      for ( const auto &entry : classes )
      {
        if ( !entry.isObject() || !entry.isMember( "color" ) )
          continue;
        const double value =
          entry.isMember( "min" ) && entry["min"].isNumeric() ? entry["min"].asDouble() : index;
        paletteClasses << QgsPalettedRasterRenderer::Class(
          value, QColor( colorString( entry["color"], QStringLiteral( "#000000" ) ) ),
          entry.isMember( "label" ) && entry["label"].isString()
            ? QString::fromStdString( entry["label"].asString() )
            : QString::number( static_cast<int>( value ) ) );
        ++index;
      }
      raster->setRenderer(
        new QgsPalettedRasterRenderer( raster->dataProvider(), band, paletteClasses ) );
      applied << QStringLiteral( "raster:paletted(band=%1,classes=%2)" )
                   .arg( band )
                   .arg( paletteClasses.size() );
      return true;
    }
    // setRasterShaderFunction takes ownership (SIP_TRANSFER): the function
    // must be heap-allocated or the shader dangles after this scope.
    auto *shader = new QgsColorRampShader();
    shader->setColorRampType( mode == "continuous" ? Qgis::ShaderInterpolationMethod::Linear
                                                   : Qgis::ShaderInterpolationMethod::Discrete );
    shader->setColorRampItemList( rampShaderItems( rasterBlock, tokenSet, mode ) );
    auto *rampShader = new QgsRasterShader();
    rampShader->setRasterShaderFunction( shader );
    raster->setRenderer(
      new QgsSingleBandPseudoColorRenderer( raster->dataProvider(), band, rampShader ) );
    applied << QStringLiteral( "raster:singleband_pseudocolor(band=%1,mode=%2)" )
                 .arg( band )
                 .arg( QString::fromStdString( mode ) );
    return true;
  }
  if ( renderertype == "multiband_color" )
  {
    const Json::Value bands = rasterBlock.get( "bands", Json::Value() );
    const int red = bands.isMember( "red" ) && bands["red"].isIntegral() ? bands["red"].asInt() : 3;
    const int green =
      bands.isMember( "green" ) && bands["green"].isIntegral() ? bands["green"].asInt() : 2;
    const int blue = bands.isMember( "blue" ) && bands["blue"].isIntegral() ? bands["blue"].asInt() : 1;
    // Platform 6.0 (Milestone E): refuse band assignments beyond the real
    // band count — a silently clamped/empty multiband composite is a
    // semantically wrong render.
    const int bandCount = raster->bandCount();
    if ( red < 1 || red > bandCount || green < 1 || green > bandCount || blue < 1 ||
         blue > bandCount )
    {
      problems << QStringLiteral( "multiband_color bands (%1,%2,%3) out of range (1-%4)" )
                    .arg( red )
                    .arg( green )
                    .arg( blue )
                    .arg( bandCount );
      return false;
    }
    raster->setRenderer( new QgsMultiBandColorRenderer( raster->dataProvider(), red, green, blue ) );
    applied << QStringLiteral( "raster:multiband_color(%1,%2,%3)" ).arg( red ).arg( green ).arg( blue );
    return true;
  }
  problems << QStringLiteral( "unsupported raster renderertype '%1'" )
                .arg( QString::fromStdString( renderertype ) );
  return false;
}

void applyScaleVisibility( QgsMapLayer *layer, const Json::Value &scale )
{
  if ( !scale.isObject() )
    return;
  const bool hasMin = scale.isMember( "min" ) && scale["min"].isNumeric() && scale["min"].asDouble() > 0;
  const bool hasMax = scale.isMember( "max" ) && scale["max"].isNumeric() && scale["max"].asDouble() > 0;
  if ( !hasMin && !hasMax )
    return;
  layer->setScaleBasedVisibility( true );
  if ( hasMin )
    layer->setMinimumScale( scale["min"].asDouble() );
  if ( hasMax )
    layer->setMaximumScale( scale["max"].asDouble() );
}

void applyLabels( QgsVectorLayer *layer, const Json::Value &labels, const Json::Value &tokenSet,
                  QStringList &applied, QStringList &problems )
{
  if ( !labels.isObject() || !( labels.isMember( "enabled" ) && labels["enabled"].isBool() &&
                                labels["enabled"].asBool() ) )
    return;
  if ( !labels.isMember( "field" ) || !labels["field"].isString() ||
       labels["field"].asString().empty() )
  {
    problems << QStringLiteral( "labels enabled but no field declared" );
    return;
  }
  QgsPalLayerSettings settings;
  settings.fieldName = QString::fromStdString( labels["field"].asString() );
  settings.placement = Qgis::LabelPlacement::OverPoint;
  QgsTextFormat format;
  double sizePt = 9.0;
  if ( labels.isMember( "size_pt" ) )
  {
    if ( labels["size_pt"].isNumeric() )
      sizePt = labels["size_pt"].asDouble();
    else if ( labels["size_pt"].isString() )
    {
      // Token reference resolved against the style's token set.
      const Json::Value resolved = tokenValue( tokenSet, tokenReferencePath( labels["size_pt"].asString() ) );
      if ( resolved.isNumeric() )
        sizePt = resolved.asDouble();
    }
  }
  const std::string fontFamily = tokenString( tokenSet, "typography.font_family" );
  QFont font = fontFamily.empty() ? QFont() : QFont( QString::fromStdString( fontFamily ) );
  format.setFont( font );
  format.setSize( sizePt );
  format.setSizeUnit( Qgis::RenderUnit::Points );
  if ( labels.isMember( "color" ) && labels["color"].isString() )
    format.setColor( QColor( colorString( labels["color"], QStringLiteral( "#000000" ) ) ) );
  if ( labels.isMember( "halo_mm" ) && labels["halo_mm"].isNumeric() && labels["halo_mm"].asDouble() > 0 )
  {
    QgsTextBufferSettings buffer;
    buffer.setEnabled( true );
    buffer.setSize( labels["halo_mm"].asDouble() );
    buffer.setSizeUnit( Qgis::RenderUnit::Millimeters );
    buffer.setColor( QColor( Qt::white ) );
    format.setBuffer( buffer );
  }
  settings.setFormat( format );
  layer->setLabeling( new QgsVectorLayerSimpleLabeling( settings ) );
  layer->setLabelsEnabled( true );
  applied << QStringLiteral( "labels(field=%1,size=%2pt)" )
               .arg( settings.fieldName )
               .arg( sizePt );
}

Qgis::GeometryType layerGeometry( const QgsVectorLayer *layer )
{
  switch ( layer->geometryType() )
  {
    case Qgis::GeometryType::Line:
      return Qgis::GeometryType::Line;
    case Qgis::GeometryType::Point:
      return Qgis::GeometryType::Point;
    default:
      return Qgis::GeometryType::Polygon;
  }
}

bool applyVectorBlock( QgsVectorLayer *vector, const Json::Value &vectorBlock,
                       const Json::Value &tokenSet, QStringList &applied, QStringList &problems )
{
  const std::string renderertype =
    vectorBlock.isMember( "renderertype" ) && vectorBlock["renderertype"].isString()
      ? vectorBlock["renderertype"].asString()
      : "";
  const QString defaultColor =
    tokenString( tokenSet, "colors.accent" ).empty()
      ? QStringLiteral( "#0072b2" )
      : QString::fromStdString( tokenString( tokenSet, "colors.accent" ) );
  const Qgis::GeometryType geometry = layerGeometry( vector );

  if ( renderertype == "simple" )
  {
    Json::Value entry( Json::objectValue );
    entry["color"] = vectorBlock.get( "color", Json::Value() );
    entry["symbol"] = vectorBlock.get( "symbols", Json::Value() );
    vector->setRenderer( new QgsSingleSymbolRenderer(
      buildSymbol( entry, defaultColor, geometry ) ) );
    applied << QStringLiteral( "vector:simple" );
  }
  else if ( renderertype == "categorized" )
  {
    const std::string field =
      vectorBlock.isMember( "field" ) && vectorBlock["field"].isString() ? vectorBlock["field"].asString()
                                                                        : "";
    if ( field.empty() )
    {
      problems << QStringLiteral( "categorized renderer needs vector.field" );
      return false;
    }
    QgsCategoryList categories;
    int index = 0;
    for ( const auto &entry : vectorBlock.get( "categories", Json::Value() ) )
    {
      if ( !entry.isObject() )
        continue;
      const QString value =
        entry.isMember( "value" )
          ? ( entry["value"].isString() ? QString::fromStdString( entry["value"].asString() )
                                        : QString::number( entry["value"].asDouble() ) )
          : QString::number( index );
      const QString label = entry.isMember( "label" ) && entry["label"].isString()
                              ? QString::fromStdString( entry["label"].asString() )
                              : value;
      categories << QgsRendererCategory(
        value, buildSymbol( entry, defaultColor, geometry ), label, true );
      ++index;
    }
    if ( categories.isEmpty() )
    {
      problems << QStringLiteral( "categorized renderer needs vector.categories" );
      return false;
    }
    vector->setRenderer( new QgsCategorizedSymbolRenderer( QString::fromStdString( field ), categories ) );
    applied << QStringLiteral( "vector:categorized(field=%1,categories=%2)" )
                 .arg( QString::fromStdString( field ) )
                 .arg( categories.size() );
  }
  else if ( renderertype == "graduated" )
  {
    const std::string field =
      vectorBlock.isMember( "field" ) && vectorBlock["field"].isString() ? vectorBlock["field"].asString()
                                                                        : "";
    const Json::Value classes = vectorBlock.get( "categories", Json::Value() );
    if ( field.empty() || !classes.isArray() || classes.empty() )
    {
      problems << QStringLiteral( "graduated renderer needs vector.field and categories ranges" );
      return false;
    }
    QgsGraduatedSymbolRenderer *renderer = new QgsGraduatedSymbolRenderer(
      QString::fromStdString( field ), QgsRangeList() );
    int index = 0;
    for ( const auto &entry : classes )
    {
      if ( !entry.isObject() || !entry.isMember( "min" ) || !entry.isMember( "max" ) )
        continue;
      const double lower = entry["min"].asDouble();
      const double upper = entry["max"].asDouble();
      const QString label = entry.isMember( "label" ) && entry["label"].isString()
                              ? QString::fromStdString( entry["label"].asString() )
                              : QStringLiteral( "%1-%2" ).arg( lower ).arg( upper );
      renderer->addClass( QgsRendererRange(
        lower, upper, buildSymbol( entry, defaultColor, geometry ), label ) );
      ++index;
    }
    if ( renderer->ranges().isEmpty() )
    {
      delete renderer;
      problems << QStringLiteral( "graduated renderer needs category min/max ranges" );
      return false;
    }
    renderer->setMode( QgsGraduatedSymbolRenderer::Custom );
    vector->setRenderer( renderer );
    applied << QStringLiteral( "vector:graduated(field=%1,classes=%2)" )
                 .arg( QString::fromStdString( field ) )
                 .arg( renderer->ranges().size() );
  }
  else if ( renderertype == "rule_based" )
  {
    // Issue #782: the renderer's root must be a symbol-less group rule with
    // the declared rules as its *children* (siblings of each other). The
    // previous build used the first rule as the root and appended the rest
    // as its children, so rules 2..n were only evaluated when rule 1 matched.
    QgsRuleBasedRenderer::Rule *root = new QgsRuleBasedRenderer::Rule( nullptr );
    int rules = 0;
    // Bounded nesting for declared sub-rules (validation caps flat rules at
    // 64; nesting depth gets its own budget so a hostile document cannot
    // recurse unboundeded).
    std::function<void( const Json::Value &, QgsRuleBasedRenderer::Rule *, int )> buildRules =
      [&]( const Json::Value &rulesJson, QgsRuleBasedRenderer::Rule *parent, int depth ) {
        if ( depth > 4 )
        {
          // Review P2: silently dropping declared sub-rules would change the
          // render without a trace — report the truncation.
          problems << QStringLiteral( "rule_based sub-rules below nesting depth 4 dropped" );
          return;
        }
        for ( const auto &rule : rulesJson )
        {
          if ( !rule.isObject() || !rule.isMember( "expression" ) || !rule["expression"].isString() )
            continue;
          Json::Value entry( Json::objectValue );
          entry["color"] = rule.get( "color", Json::Value() );
          entry["symbol"] = rule.get( "symbol", Json::Value() );
          QgsSymbol *symbol = buildSymbol( entry, defaultColor, geometry );
          const QString label = rule.isMember( "label" ) && rule["label"].isString()
                                  ? QString::fromStdString( rule["label"].asString() )
                                  : QString();
          QgsRuleBasedRenderer::Rule *child =
            new QgsRuleBasedRenderer::Rule( symbol, 0, 0,
                                            QString::fromStdString( rule["expression"].asString() ),
                                            label );
          parent->appendChild( child );
          ++rules;
          if ( rule.isMember( "rules" ) && rule["rules"].isArray() )
            buildRules( rule["rules"], child, depth + 1 );
        }
      };
    buildRules( vectorBlock.get( "rules", Json::Value() ), root, 0 );
    if ( rules == 0 )
    {
      delete root;
      problems << QStringLiteral( "rule_based renderer needs vector.rules with expressions" );
      return false;
    }
    vector->setRenderer( new QgsRuleBasedRenderer( root ) );
    applied << QStringLiteral( "vector:rule_based(rules=%1)" ).arg( rules );
  }
  else
  {
    problems << QStringLiteral( "unsupported vector renderertype '%1'" )
                  .arg( QString::fromStdString( renderertype ) );
    return false;
  }

  // Shared vector surface: opacity, blend, scale visibility, labels.
  if ( vectorBlock.isMember( "opacity" ) && vectorBlock["opacity"].isNumeric() )
    vector->setOpacity( vectorBlock["opacity"].asDouble() );
  applyScaleVisibility( vector, vectorBlock.get( "scaledenominator", Json::Value() ) );
  applyLabels( vector, vectorBlock.get( "labels", Json::Value() ), tokenSet, applied, problems );
  vector->triggerRepaint();
  return true;
}

} // namespace

QgsRasterRenderer *buildRasterRenderer( const Json::Value &rasterBlock, int bandCount, QString *error )
{
  if ( !rasterBlock.isObject() )
  {
    if ( error )
      *error = QStringLiteral( "raster block must be an object" );
    return nullptr;
  }
  const int band = rasterBlock.isMember( "band" ) && rasterBlock["band"].isIntegral()
                     ? rasterBlock["band"].asInt()
                     : 1;
  if ( band < 1 || band > bandCount )
  {
    if ( error )
      *error = QStringLiteral( "band %1 out of range" ).arg( band );
    return nullptr;
  }
  const std::string renderertype =
    rasterBlock.isMember( "renderertype" ) && rasterBlock["renderertype"].isString()
      ? rasterBlock["renderertype"].asString()
      : "";
  if ( renderertype == "singleband_gray" )
    return new QgsSingleBandGrayRenderer( nullptr, band );
  if ( renderertype == "multiband_color" )
    return new QgsMultiBandColorRenderer( nullptr, 3, 2, 1 );
  if ( renderertype == "singleband_pseudocolor" || renderertype == "paletted" )
  {
    auto *shader = new QgsColorRampShader();
    shader->setColorRampType( Qgis::ShaderInterpolationMethod::Discrete );
    shader->setColorRampItemList( rampShaderItems( rasterBlock, Json::Value(), "discrete" ) );
    auto *rampShader = new QgsRasterShader();
    rampShader->setRasterShaderFunction( shader );
    return new QgsSingleBandPseudoColorRenderer( nullptr, band, rampShader );
  }
  if ( error )
    *error = QStringLiteral( "unsupported renderertype" );
  return nullptr;
}

bool applyStyleSpecToLayer( QgsMapLayer *layer, const Json::Value &styleSpecIn, QString *error,
                            QStringList *problemsOut )
{
  QStringList applied;
  QStringList problems;
  if ( !layer )
  {
    if ( error )
      *error = QStringLiteral( "no layer" );
    return false;
  }
  // StyleSpecs name their token set through token_set_ref (resolveTokenSet
  // reads style.token_set), so repackage the reference for resolution.
  Json::Value styleObject( Json::objectValue );
  if ( styleSpecIn.isMember( "token_set_ref" ) && styleSpecIn["token_set_ref"].isString() )
    styleObject["token_set"] = styleSpecIn["token_set_ref"];
  const Json::Value tokenSet = resolveTokenSet( styleObject );

  // Platform 5.0 review fix: the apply path must see RESOLVED token values.
  // Raw "token:*" ramps previously fell back to the qualitative palette and
  // "token:colors.*" class colors became invalid QColors — silently wrong.
  std::vector<std::string> tokenProblems;
  const Json::Value styleSpec = resolveStyleTokens( styleSpecIn, tokenSet, &tokenProblems );
  for ( const std::string &problem : tokenProblems )
    problems << QString::fromStdString( problem );

  bool appliedAny = false;
  if ( styleSpec.isMember( "raster" ) && styleSpec["raster"].isObject() )
  {
    if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
    {
      if ( styleSpec["raster"].isMember( "opacity" ) && styleSpec["raster"]["opacity"].isNumeric() )
        raster->setOpacity( styleSpec["raster"]["opacity"].asDouble() );
      appliedAny = applyRasterBlock( raster, styleSpec["raster"], tokenSet, applied, problems ) ||
                   appliedAny;
    }
    else
    {
      problems << QStringLiteral( "raster style block on a non-raster layer" );
    }
  }
  if ( styleSpec.isMember( "vector" ) && styleSpec["vector"].isObject() )
  {
    if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
    {
      appliedAny = applyVectorBlock( vector, styleSpec["vector"], tokenSet, applied, problems ) ||
                   appliedAny;
    }
    else
    {
      problems << QStringLiteral( "vector style block on a non-vector layer" );
    }
  }
  if ( !appliedAny && error )
    *error = problems.isEmpty() ? QStringLiteral( "no applicable style block" ) : problems.first();
  if ( problemsOut )
    *problemsOut = problems;
  return appliedAny;
}

Json::Value styleApplicationReport( const QString &layerId, const QStringList &applied,
                                    const QStringList &problems )
{
  Json::Value report( Json::objectValue );
  report["layer"] = layerId.toStdString();
  Json::Value appliedJson( Json::arrayValue );
  for ( const QString &entry : applied )
    appliedJson.append( entry.toStdString() );
  report["applied"] = appliedJson;
  Json::Value problemsJson( Json::arrayValue );
  for ( const QString &entry : problems )
    problemsJson.append( entry.toStdString() );
  report["problems"] = problemsJson;
  return report;
}

} // namespace sicnu::agent::cartography
