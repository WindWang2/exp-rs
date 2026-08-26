// src/agent/raster_display_service.cpp
#include "raster_display_service.h"

#include "data/band_role.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "display/display_stretch_types.h"
#include "display/qgis_display_manager.h"
#include "display/qgs_display_stretch.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgscontrastenhancement.h>
#include <qgsmapcanvas.h>
#include <qgsmaplayer.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgsproject.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterlayer.h>
#include <qgsrasterrenderer.h>
#include <qgssinglebandgrayrenderer.h>

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <cmath>
#include <memory>

namespace sicnu::agent {

namespace {

std::unique_ptr<QgsContrastEnhancement> createBandEnhancement( QgsRasterDataProvider *provider, int band )
{
  if ( !provider || band < 1 )
    return nullptr;

  auto ce = std::make_unique<QgsContrastEnhancement>(
    provider->dataType( band ) );
  ce->setContrastEnhancementAlgorithm(
    QgsContrastEnhancement::StretchToMinimumMaximum, true );

  const QgsRasterBandStats stats = provider->bandStatistics(
    band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max,
    QgsRectangle(), 250000 );

  double minVal = 0.0;
  double maxVal = 255.0;
  if ( std::isfinite( stats.minimumValue ) && std::isfinite( stats.maximumValue ) &&
       stats.maximumValue > stats.minimumValue )
  {
    minVal = stats.minimumValue;
    maxVal = stats.maximumValue;
  }
  ce->setMinimumValue( minVal );
  ce->setMaximumValue( maxVal );

  return ce;
}

} // namespace

RasterDisplayService::RasterDisplayService( QObject *parent )
  : QObject( parent )
{
}

RasterDisplayService::RasterDisplayService( sicnu::display::QgisDisplayManager *displayManager,
                                            QgsMapCanvas *canvas,
                                            sicnu::data::DataManager *dataManager,
                                            QObject *parent )
  : QObject( parent )
  , m_displayManager( displayManager )
  , m_canvas( canvas )
  , m_dataManager( dataManager )
{
}

void RasterDisplayService::setDisplayManager( sicnu::display::QgisDisplayManager *dm )
{
  m_displayManager = dm;
}

void RasterDisplayService::setMapCanvas( QgsMapCanvas *canvas )
{
  m_canvas = canvas;
}

void RasterDisplayService::setDataManager( sicnu::data::DataManager *dm )
{
  m_dataManager = dm;
}

void RasterDisplayService::setActiveLayerName( const QString &name )
{
  m_activeLayerName = name;
}

QString RasterDisplayService::activeLayerName() const
{
  return m_activeLayerName;
}

void RasterDisplayService::incrementRevision()
{
  ++m_displayRevision;
  emit displayChanged( m_displayRevision );
}

QgsRasterLayer *RasterDisplayService::findRasterLayer( const QString &identifier, QString *errorOut ) const
{
  QString targetId = identifier.trimmed();

  // If identifier is empty, fallback to canvas active layer or m_activeLayerName
  if ( targetId.isEmpty() )
  {
    if ( m_canvas && m_canvas->currentLayer() )
    {
      if ( auto *rl = qobject_cast<QgsRasterLayer *>( m_canvas->currentLayer() ) )
        return rl;
    }
    targetId = m_activeLayerName;
  }

  if ( targetId.isEmpty() )
  {
    if ( errorOut )
      *errorOut = QStringLiteral( "No active or specified raster layer." );
    return nullptr;
  }

  // 1. Try display manager DisplayLayerId
  if ( m_displayManager )
  {
    if ( auto layerIdOpt = display::DisplayLayerId::fromString( targetId ) )
    {
      if ( QgsMapLayer *ml = m_displayManager->mapLayer( *layerIdOpt ) )
      {
        if ( auto *rl = qobject_cast<QgsRasterLayer *>( ml ) )
          return rl;
      }
    }
  }

  // 2. Try map canvas layers
  if ( m_canvas )
  {
    const QList<QgsMapLayer *> layers = m_canvas->layers();
    for ( QgsMapLayer *layer : layers )
    {
      if ( layer && ( layer->id() == targetId || layer->name() == targetId ) )
      {
        if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
          return rl;
      }
    }
  }

  // 3. Try QgsProject map layers
  if ( QgsProject::instance() )
  {
    if ( QgsMapLayer *ml = QgsProject::instance()->mapLayer( targetId ) )
    {
      if ( auto *rl = qobject_cast<QgsRasterLayer *>( ml ) )
        return rl;
    }

    const auto projectLayers = QgsProject::instance()->mapLayersByName( targetId );
    if ( !projectLayers.isEmpty() )
    {
      for ( QgsMapLayer *ml : projectLayers )
      {
        if ( auto *rl = qobject_cast<QgsRasterLayer *>( ml ) )
          return rl;
      }
    }
  }

  // 4. Try DataManager asset matching
  if ( m_dataManager )
  {
    for ( const auto &asset : m_dataManager->assets() )
    {
      if ( asset.id().toString() == targetId || asset.displayName() == targetId )
      {
        // Find layer corresponding to asset's path
        const QString assetPath = asset.source().canonicalSource;
        if ( m_canvas )
        {
          for ( QgsMapLayer *layer : m_canvas->layers() )
          {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
            {
              if ( rl->source() == assetPath )
                return rl;
            }
          }
        }
        if ( QgsProject::instance() )
        {
          for ( auto *layer : QgsProject::instance()->mapLayers().values() )
          {
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( layer ) )
            {
              if ( rl->source() == assetPath )
                return rl;
            }
          }
        }
      }
    }
  }

  if ( errorOut )
    *errorOut = QString( "Raster layer '%1' not found or is not a valid raster layer." ).arg( targetId );
  return nullptr;
}

std::pair<int, QString> RasterDisplayService::resolveBand( QgsRasterLayer *layer, const Json::Value &bandVal ) const
{
  if ( !layer || !layer->isValid() )
    return { 0, QStringLiteral( "Invalid raster layer." ) };

  const int totalBands = layer->bandCount();
  if ( totalBands <= 0 )
    return { 0, QStringLiteral( "Raster layer has no bands." ) };

  // Integer band index
  if ( bandVal.isInt() || bandVal.isUInt() )
  {
    const int b = bandVal.asInt();
    if ( b < 1 || b > totalBands )
      return { 0, QString( "Band index %1 is out of range (1..%2)." ).arg( b ).arg( totalBands ) };
    return { b, QString() };
  }

  if ( bandVal.isDouble() )
  {
    const int b = static_cast<int>( bandVal.asDouble() );
    if ( b < 1 || b > totalBands )
      return { 0, QString( "Band index %1 is out of range (1..%2)." ).arg( b ).arg( totalBands ) };
    return { b, QString() };
  }

  if ( bandVal.isString() )
  {
    const QString str = QString::fromStdString( bandVal.asString() ).trimmed();
    if ( str.isEmpty() )
      return { 0, QStringLiteral( "Band identifier cannot be empty." ) };

    bool isNumeric = false;
    const int num = str.toInt( &isNumeric );
    if ( isNumeric )
    {
      if ( num < 1 || num > totalBands )
        return { 0, QString( "Band index %1 is out of range (1..%2)." ).arg( num ).arg( totalBands ) };
      return { num, QString() };
    }

    // Semantic BandRole resolution
    const data::BandRole role = data::bandRoleFromString( str );
    if ( role == data::BandRole::Unknown )
    {
      return { 0, QString( "Unrecognized band role or invalid band specification: '%1'." ).arg( str ) };
    }

    int matchedBand = 0;

    // 1. Search in DataManager asset structures
    if ( m_dataManager )
    {
      for ( const auto &asset : m_dataManager->assets() )
      {
        if ( asset.source().canonicalSource == layer->source() ||
             asset.displayName() == layer->name() )
        {
          const auto &structure = asset.structure();
          if ( const auto *raster = std::get_if<data::RasterStructure>( &structure ) )
          {
            for ( int i = 0; i < raster->bands.size(); ++i )
            {
              if ( raster->bands[i].role == role )
              {
                matchedBand = raster->bands[i].number > 0 ? raster->bands[i].number : ( i + 1 );
                break;
              }
            }
          }
        }
        if ( matchedBand > 0 )
          break;
      }
    }

    // 2. Search in GDAL metadata (SICNU_BAND_ROLE)
    if ( matchedBand == 0 && !layer->source().isEmpty() )
    {
      GdalDatasetWrapper ds;
      if ( ds.open( layer->source() ) )
      {
        for ( int b = 1; b <= ds.bandCount(); ++b )
        {
          const QString roleStr = ds.bandMetadataItem( b, "SICNU_BAND_ROLE" );
          if ( !roleStr.isEmpty() && data::bandRoleFromString( roleStr ) == role )
          {
            matchedBand = b;
            break;
          }
        }
      }
    }

    if ( matchedBand >= 1 && matchedBand <= totalBands )
    {
      return { matchedBand, QString() };
    }

    return { 0, QString( "Semantic band role '%1' not found in raster layer '%2'." ).arg( str, layer->name() ) };
  }

  return { 0, QStringLiteral( "Invalid band value format (expected integer or role string)." ) };
}

Json::Value RasterDisplayService::getDisplay( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    QString layerId;
    if ( params.isObject() )
    {
      if ( params.isMember( "layer" ) && params["layer"].isString() )
        layerId = QString::fromStdString( params["layer"].asString() );
      else if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
        layerId = QString::fromStdString( params["layer_id"].asString() );
      else if ( params.isMember( "layer_name" ) && params["layer_name"].isString() )
        layerId = QString::fromStdString( params["layer_name"].asString() );
    }

    QString findErr;
    QgsRasterLayer *layer = findRasterLayer( layerId, &findErr );
    if ( !layer )
    {
      result["status"] = "error";
      result["errorMessage"] = findErr.toStdString();
      return result;
    }

    result["status"] = "success";
    result["layer"] = layer->name().toStdString();
    result["opacity"] = layer->opacity();
    result["displayRevision"] = static_cast<Json::UInt64>( m_displayRevision );

    QgsRasterRenderer *renderer = layer->renderer();
    if ( !renderer )
    {
      result["renderer"] = "None";
      result["bands"] = Json::Value( Json::objectValue );
      result["stretch"] = Json::Value( Json::objectValue );
      return result;
    }

    result["renderer"] = renderer->type().toStdString();

    Json::Value bandsObj( Json::objectValue );
    Json::Value stretchObj( Json::objectValue );

    if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( renderer ) )
    {
      bandsObj["gray"] = gray->inputBand();
      if ( const auto *ce = gray->contrastEnhancement() )
      {
        stretchObj["algorithm"] = QgsContrastEnhancement::contrastEnhancementAlgorithmString(
                                    ce->contrastEnhancementAlgorithm() )
                                    .toStdString();
        stretchObj["displayMin"] = ce->minimumValue();
        stretchObj["displayMax"] = ce->maximumValue();
      }
    }
    else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( renderer ) )
    {
      bandsObj["red"] = rgb->redBand();
      bandsObj["green"] = rgb->greenBand();
      bandsObj["blue"] = rgb->blueBand();
      if ( const auto *ce = rgb->redContrastEnhancement() )
      {
        stretchObj["algorithm"] = QgsContrastEnhancement::contrastEnhancementAlgorithmString(
                                    ce->contrastEnhancementAlgorithm() )
                                    .toStdString();
        stretchObj["displayMin"] = ce->minimumValue();
        stretchObj["displayMax"] = ce->maximumValue();
      }
    }

    result["bands"] = bandsObj;
    result["stretch"] = stretchObj;
    return result;
  } );
}

Json::Value RasterDisplayService::setBandComposite( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !params.isObject() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Parameters must be a JSON object.";
      return result;
    }

    QString layerId;
    if ( params.isMember( "layer" ) && params["layer"].isString() )
      layerId = QString::fromStdString( params["layer"].asString() );
    else if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
      layerId = QString::fromStdString( params["layer_id"].asString() );
    else if ( params.isMember( "layer_name" ) && params["layer_name"].isString() )
      layerId = QString::fromStdString( params["layer_name"].asString() );

    QString findErr;
    QgsRasterLayer *layer = findRasterLayer( layerId, &findErr );
    if ( !layer )
    {
      result["status"] = "error";
      result["errorMessage"] = findErr.toStdString();
      return result;
    }

    if ( !layer->dataProvider() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Raster layer has no data provider.";
      return result;
    }

    // Check if RGB composition or SingleBandGray composition is requested
    const bool hasRgb = params.isMember( "red" ) || params.isMember( "green" ) || params.isMember( "blue" ) ||
                        params.isMember( "red_band" ) || params.isMember( "green_band" ) || params.isMember( "blue_band" );
    const bool hasGray = params.isMember( "gray" ) || params.isMember( "band" ) || params.isMember( "gray_band" );

    if ( !hasRgb && !hasGray )
    {
      result["status"] = "error";
      result["errorMessage"] = "Must specify RGB channels ('red', 'green', 'blue') or single channel ('gray').";
      return result;
    }

    QgsRasterRenderer* renderer = nullptr;

    if ( hasRgb )
    {
      // Default missing channels if at least one is provided
      const Json::Value redVal = params.isMember( "red" ) ? params["red"] : ( params.isMember( "red_band" ) ? params["red_band"] : Json::Value( 1 ) );
      const Json::Value greenVal = params.isMember( "green" ) ? params["green"] : ( params.isMember( "green_band" ) ? params["green_band"] : ( layer->bandCount() >= 2 ? Json::Value( 2 ) : redVal ) );
      const Json::Value blueVal = params.isMember( "blue" ) ? params["blue"] : ( params.isMember( "blue_band" ) ? params["blue_band"] : ( layer->bandCount() >= 3 ? Json::Value( 3 ) : greenVal ) );

      const auto redRes = resolveBand( layer, redVal );
      if ( !redRes.second.isEmpty() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid red band: " + redRes.second.toStdString();
        return result;
      }
      const auto greenRes = resolveBand( layer, greenVal );
      if ( !greenRes.second.isEmpty() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid green band: " + greenRes.second.toStdString();
        return result;
      }
      const auto blueRes = resolveBand( layer, blueVal );
      if ( !blueRes.second.isEmpty() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid blue band: " + blueRes.second.toStdString();
        return result;
      }

      const int redBand = redRes.first;
      const int greenBand = greenRes.first;
      const int blueBand = blueRes.first;

      auto r = std::make_unique<QgsMultiBandColorRenderer>(
        layer->dataProvider(), redBand, greenBand, blueBand );

      r->setRedContrastEnhancement( createBandEnhancement( layer->dataProvider(), redBand ).release() );
      r->setGreenContrastEnhancement( createBandEnhancement( layer->dataProvider(), greenBand ).release() );
      r->setBlueContrastEnhancement( createBandEnhancement( layer->dataProvider(), blueBand ).release() );
      renderer = r.release();

      result["renderer"] = "MultiBandColor";
      Json::Value bandsObj( Json::objectValue );
      bandsObj["red"] = redBand;
      bandsObj["green"] = greenBand;
      bandsObj["blue"] = blueBand;
      result["bands"] = bandsObj;
      result["red_band"] = redBand;
      result["green_band"] = greenBand;
      result["blue_band"] = blueBand;
    }
    else
    {
      const Json::Value grayVal = params.isMember( "gray" ) ? params["gray"] : ( params.isMember( "gray_band" ) ? params["gray_band"] : params["band"] );
      const auto grayRes = resolveBand( layer, grayVal );
      if ( !grayRes.second.isEmpty() )
      {
        result["status"] = "error";
        result["errorMessage"] = "Invalid gray band: " + grayRes.second.toStdString();
        return result;
      }

      const int grayBand = grayRes.first;
      auto r = std::make_unique<QgsSingleBandGrayRenderer>(
        layer->dataProvider(), grayBand );
      r->setContrastEnhancement( createBandEnhancement( layer->dataProvider(), grayBand ).release() );
      renderer = r.release();

      result["renderer"] = "SingleBandGray";
      Json::Value bandsObj( Json::objectValue );
      bandsObj["gray"] = grayBand;
      result["bands"] = bandsObj;
      result["gray_band"] = grayBand;
    }

    layer->setRenderer( renderer );

    if ( params.isMember( "opacity" ) && params["opacity"].isNumeric() )
    {
      layer->setOpacity( std::clamp( params["opacity"].asDouble(), 0.0, 1.0 ) );
    }

    layer->triggerRepaint();
    if ( m_canvas )
      m_canvas->refresh();

    incrementRevision();

    result["status"] = "success";
    result["layer"] = layer->name().toStdString();
    result["opacity"] = layer->opacity();
    result["displayRevision"] = static_cast<Json::UInt64>( m_displayRevision );
    return result;
  } );
}

Json::Value RasterDisplayService::setStretch( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    if ( !params.isObject() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Parameters must be a JSON object.";
      return result;
    }

    QString layerId;
    if ( params.isMember( "layer" ) && params["layer"].isString() )
      layerId = QString::fromStdString( params["layer"].asString() );
    else if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
      layerId = QString::fromStdString( params["layer_id"].asString() );
    else if ( params.isMember( "layer_name" ) && params["layer_name"].isString() )
      layerId = QString::fromStdString( params["layer_name"].asString() );

    QString findErr;
    QgsRasterLayer *layer = findRasterLayer( layerId, &findErr );
    if ( !layer )
    {
      result["status"] = "error";
      result["errorMessage"] = findErr.toStdString();
      return result;
    }

    if ( !layer->isValid() || !layer->renderer() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Raster layer has no valid renderer.";
      return result;
    }

    std::string method = "";
    if ( params.isMember( "method" ) && params["method"].isString() )
      method = params["method"].asString();
    else if ( params.isMember( "type" ) && params["type"].isString() )
      method = params["type"].asString();
    else if ( params.isMember( "stretch_type" ) && params["stretch_type"].isString() )
      method = params["stretch_type"].asString();
    else
      method = "minimum_maximum";

    if ( method == "std_dev_2" )
      method = "stddev";
    else if ( method == "percentile_2_98" )
      method = "percent_clip";

    rs::display::StretchSpec spec = rs::display::StretchSpec::realDataRange();

    if ( method == "minimum_maximum" || method == "min_max" || method == "minmax" || method == "linear" )
    {
      double minVal = 0.0, maxVal = 0.0;
      bool hasMinMax = false;
      if ( params.isMember( "min" ) && params.isMember( "max" ) &&
           params["min"].isNumeric() && params["max"].isNumeric() )
      {
        minVal = params["min"].asDouble();
        maxVal = params["max"].asDouble();
        hasMinMax = true;
      }
      else if ( params.isMember( "min_val" ) && params.isMember( "max_val" ) &&
                params["min_val"].isNumeric() && params["max_val"].isNumeric() )
      {
        minVal = params["min_val"].asDouble();
        maxVal = params["max_val"].asDouble();
        hasMinMax = true;
      }

      if ( hasMinMax )
      {
        spec = rs::display::StretchSpec::linearMinMax( minVal, maxVal );
      }
      else
      {
        spec = rs::display::StretchSpec::realDataRange();
      }
    }
    else if ( method == "percent_clip" || method == "percentclip" || method == "percent" )
    {
      double lower = params.isMember( "lower" ) && params["lower"].isNumeric()
                       ? params["lower"].asDouble()
                       : 2.0;
      double upper = params.isMember( "upper" ) && params["upper"].isNumeric()
                       ? params["upper"].asDouble()
                       : 98.0;
      if ( upper <= lower )
      {
        result["status"] = "error";
        result["errorMessage"] = "Upper percentile must be greater than lower percentile.";
        return result;
      }
      const double totalClip = lower + ( 100.0 - upper );
      spec = rs::display::StretchSpec::percentClip( totalClip );
    }
    else if ( method == "stddev" || method == "std_dev" || method == "standard_deviation" )
    {
      double factor = 2.0;
      if ( params.isMember( "factor" ) && params["factor"].isNumeric() )
        factor = params["factor"].asDouble();
      else if ( params.isMember( "stddev_factor" ) && params["stddev_factor"].isNumeric() )
        factor = params["stddev_factor"].asDouble();
      else if ( params.isMember( "k" ) && params["k"].isNumeric() )
        factor = params["k"].asDouble();
      spec = rs::display::StretchSpec::stdDev( factor );
    }
    else if ( method == "none" || method == "no_enhancement" )
    {
      spec = rs::display::StretchSpec::noEnhancement();
    }
    else
    {
      result["status"] = "error";
      result["errorMessage"] = "Unsupported stretch method: " + method +
                               " (supported: 'minimum_maximum', 'percent_clip', 'stddev', 'none')";
      return result;
    }

    const auto applyResult = rs::display::applyToLayer( layer, spec );
    if ( !applyResult )
    {
      result["status"] = "error";
      result["errorMessage"] = applyResult.error().message;
      return result;
    }

    if ( m_canvas )
      m_canvas->refresh();

    incrementRevision();

    result["status"] = "success";
    result["layer"] = layer->name().toStdString();
    result["method"] = method;
    result["displayMin"] = applyResult.value().applied.displayMin;
    result["displayMax"] = applyResult.value().applied.displayMax;
    result["displayRevision"] = static_cast<Json::UInt64>( m_displayRevision );
    return result;
  } );
}

Json::Value RasterDisplayService::resetDisplay( const Json::Value &params )
{
  return executeOnGuiThread( [&]() -> Json::Value {
    Json::Value result( Json::objectValue );

    QString layerId;
    if ( params.isObject() )
    {
      if ( params.isMember( "layer" ) && params["layer"].isString() )
        layerId = QString::fromStdString( params["layer"].asString() );
      else if ( params.isMember( "layer_id" ) && params["layer_id"].isString() )
        layerId = QString::fromStdString( params["layer_id"].asString() );
      else if ( params.isMember( "layer_name" ) && params["layer_name"].isString() )
        layerId = QString::fromStdString( params["layer_name"].asString() );
    }

    QString findErr;
    QgsRasterLayer *layer = findRasterLayer( layerId, &findErr );
    if ( !layer )
    {
      result["status"] = "error";
      result["errorMessage"] = findErr.toStdString();
      return result;
    }

    if ( !layer->dataProvider() )
    {
      result["status"] = "error";
      result["errorMessage"] = "Raster layer has no data provider.";
      return result;
    }

    const int bandCount = layer->bandCount();
    if ( bandCount <= 0 )
    {
      result["status"] = "error";
      result["errorMessage"] = "Raster layer has no bands.";
      return result;
    }

    if ( bandCount >= 3 )
    {
      auto renderer = std::make_unique<QgsMultiBandColorRenderer>(
        layer->dataProvider(), 1, 2, 3 );
      renderer->setRedContrastEnhancement( createBandEnhancement( layer->dataProvider(), 1 ).release() );
      renderer->setGreenContrastEnhancement( createBandEnhancement( layer->dataProvider(), 2 ).release() );
      renderer->setBlueContrastEnhancement( createBandEnhancement( layer->dataProvider(), 3 ).release() );
      layer->setRenderer( renderer.release() );
    }
    else
    {
      auto renderer = std::make_unique<QgsSingleBandGrayRenderer>(
        layer->dataProvider(), 1 );
      renderer->setContrastEnhancement( createBandEnhancement( layer->dataProvider(), 1 ).release() );
      layer->setRenderer( renderer.release() );
    }

    layer->setOpacity( 1.0 );
    layer->triggerRepaint();
    if ( m_canvas )
      m_canvas->refresh();

    incrementRevision();

    result["status"] = "success";
    result["layer"] = layer->name().toStdString();
    result["renderer"] = bandCount >= 3 ? "MultiBandColor" : "SingleBandGray";
    result["opacity"] = 1.0;
    result["displayRevision"] = static_cast<Json::UInt64>( m_displayRevision );
    return result;
  } );
}

} // namespace sicnu::agent
