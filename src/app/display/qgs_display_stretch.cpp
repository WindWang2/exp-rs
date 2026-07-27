// qgs_display_stretch.cpp — QGIS production adapters for display stretch
#include "qgs_display_stretch.h"
#include "piecewise_linear_enhancement.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterrenderer.h>
#include <qgscontrastenhancement.h>
#include <qgsrasterhistogram.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgsmultibandcolorrenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace rs::display {

static QgsRasterLayer *asLayer( void *token )
{
  return static_cast<QgsRasterLayer *>( token );
}

static bool bandRange( QgsRasterDataProvider *provider, int band,
                       double &minimum, double &maximum )
{
  if ( !provider || band < 1 )
    return false;
  const QgsRasterBandStats stats = provider->bandStatistics(
    band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max );
  if ( !std::isfinite( stats.minimumValue ) || !std::isfinite( stats.maximumValue )
       || !( stats.maximumValue > stats.minimumValue ) )
    return false;
  minimum = stats.minimumValue;
  maximum = stats.maximumValue;
  return true;
}

static std::vector<ControlPoint> gammaCurve( double minimum, double maximum,
                                             double gamma )
{
  constexpr int sampleCount = 256;
  std::vector<ControlPoint> curve;
  curve.reserve( sampleCount + 1 );
  const double exponent = 1.0 / std::max( gamma, 0.01 );
  for ( int i = 0; i <= sampleCount; ++i )
  {
    const double t = static_cast<double>( i ) / sampleCount;
    curve.push_back( ControlPoint{
      minimum + t * ( maximum - minimum ),
      255.0 * std::pow( t, exponent )
    } );
  }
  return curve;
}

static std::vector<ControlPoint> equalizationCurve( QgsRasterDataProvider *provider,
                                                    int band,
                                                    double minimum,
                                                    double maximum )
{
  constexpr int binCount = 256;
  const QgsRasterHistogram histogram =
    provider->histogram( band, binCount, minimum, maximum,
                         QgsRectangle(), 0, true );
  if ( !histogram.valid || histogram.histogramVector.size() < 2 )
    return {};

  std::int64_t total = 0;
  for ( int count : histogram.histogramVector )
    total += count;
  if ( total <= 0 )
    return {};

  std::int64_t cumulative = 0;
  std::int64_t firstCumulative = 0;
  for ( int count : histogram.histogramVector )
  {
    cumulative += count;
    if ( firstCumulative == 0 && cumulative > 0 )
      firstCumulative = cumulative;
  }

  const double denominator = static_cast<double>( total - firstCumulative );
  if ( denominator <= 0.0 )
    return {};

  std::vector<ControlPoint> curve;
  curve.reserve( histogram.histogramVector.size() );
  cumulative = 0;
  const int last = histogram.histogramVector.size() - 1;
  for ( int i = 0; i <= last; ++i )
  {
    cumulative += histogram.histogramVector[i];
    const double x = minimum
                     + ( static_cast<double>( i ) / last ) * ( maximum - minimum );
    const double y = 255.0 * std::clamp(
      ( static_cast<double>( cumulative - firstCumulative ) / denominator ),
      0.0, 1.0 );
    curve.push_back( ControlPoint{ x, y } );
  }
  return curve;
}

static bool percentileRange( QgsRasterDataProvider *provider, int band,
                             double minimum, double maximum,
                             double totalClipPercent,
                             double &lower, double &upper )
{
  constexpr int binCount = 1024;
  const QgsRasterHistogram histogram =
    provider->histogram( band, binCount, minimum, maximum,
                         QgsRectangle(), 0, true );
  if ( !histogram.valid || histogram.histogramVector.size() < 2 )
    return false;

  std::int64_t total = 0;
  for ( int count : histogram.histogramVector )
    total += count;
  if ( total <= 0 )
    return false;

  const double tailFraction = totalClipPercent / 200.0;
  const double lowerTarget = total * tailFraction;
  const double upperTarget = total * ( 1.0 - tailFraction );
  std::int64_t cumulative = 0;
  int lowerBin = 0;
  int upperBin = histogram.histogramVector.size() - 1;
  bool lowerFound = false;
  for ( int i = 0; i < histogram.histogramVector.size(); ++i )
  {
    cumulative += histogram.histogramVector[i];
    if ( !lowerFound && cumulative >= lowerTarget )
    {
      lowerBin = i;
      lowerFound = true;
    }
    if ( cumulative >= upperTarget )
    {
      upperBin = i;
      break;
    }
  }

  const double binWidth = ( maximum - minimum ) / histogram.histogramVector.size();
  lower = minimum + lowerBin * binWidth;
  upper = minimum + ( upperBin + 1 ) * binWidth;
  lower = std::clamp( lower, minimum, maximum );
  upper = std::clamp( upper, minimum, maximum );
  return upper > lower;
}

StretchResult<DisplayTargetInfo> QgsRasterDisplayTarget::inspect( void *layerToken ) const
{
  auto *layer = asLayer( layerToken );
  if ( !layer )
    return StretchResult<DisplayTargetInfo>::fail( StretchErrorCode::LayerGone, "layer is null" );
  if ( !layer->isValid() )
    return StretchResult<DisplayTargetInfo>::fail( StretchErrorCode::InvalidLayer, "layer invalid" );

  DisplayTargetInfo info;
  info.valid = true;
  info.bandCount = layer->bandCount();

  QgsRasterRenderer *renderer = layer->renderer();
  if ( !renderer )
  {
    info.renderer = DisplayTargetInfo::RendererKind::Unsupported;
    return StretchResult<DisplayTargetInfo>::ok( info );
  }

  if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( renderer ) )
  {
    info.renderer = DisplayTargetInfo::RendererKind::SingleBandGray;
    info.grayBand = gray->inputBand();
  }
  else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( renderer ) )
  {
    info.renderer = DisplayTargetInfo::RendererKind::MultiBandColor;
    info.redBand = rgb->redBand();
    info.greenBand = rgb->greenBand();
    info.blueBand = rgb->blueBand();
  }
  else
  {
    info.renderer = DisplayTargetInfo::RendererKind::Unsupported;
  }

  return StretchResult<DisplayTargetInfo>::ok( info );
}

ApplyStretchResult QgsRasterDisplayTarget::apply( void *layerToken,
                                                  const ResolvedStretch &resolved )
{
  auto *layer = asLayer( layerToken );
  if ( !layer )
    return ApplyStretchResult::fail( StretchErrorCode::LayerGone, "layer is null" );
  if ( !layer->isValid() )
    return ApplyStretchResult::fail( StretchErrorCode::InvalidLayer, "layer invalid" );

  QgsRasterRenderer *live = layer->renderer();
  if ( !live )
    return ApplyStretchResult::fail( StretchErrorCode::UnsupportedRenderer, "no renderer" );

  QgsRasterDataProvider *provider = layer->dataProvider();
  if ( !provider )
    return ApplyStretchResult::fail( StretchErrorCode::MissingProvider, "no provider" );

  double minV = resolved.displayMin;
  double maxV = resolved.displayMax;
  ensureStrictRange( minV, maxV );

  std::unique_ptr<QgsRasterRenderer> newRenderer( live->clone() );
  if ( !newRenderer )
    return ApplyStretchResult::fail( StretchErrorCode::ApplyFailed, "renderer clone failed" );

  auto createEnhancement = [&]( int band ) -> QgsContrastEnhancement * {
    double bandMin = minV;
    double bandMax = maxV;
    std::vector<ControlPoint> transferCurve = resolved.transferCurve;

    // Master RGB controls are expressed in the reference band's physical
    // range. Preserve the normalized cutoffs/curve when applying to each band.
    if ( resolved.scope == ChannelScope::MasterRgb && band >= 1 )
    {
      double referenceMin = minV;
      double referenceMax = maxV;
      double targetMin = minV;
      double targetMax = maxV;
      if ( bandRange( provider, resolved.referenceBand, referenceMin, referenceMax )
           && bandRange( provider, band, targetMin, targetMax ) )
      {
        const double sourceRange = referenceMax - referenceMin;
        const double targetRange = targetMax - targetMin;
        const double normalizedMin = ( minV - referenceMin ) / sourceRange;
        const double normalizedMax = ( maxV - referenceMin ) / sourceRange;
        bandMin = targetMin + normalizedMin * targetRange;
        bandMax = targetMin + normalizedMax * targetRange;
        for ( ControlPoint &point : transferCurve )
        {
          const double normalizedX = ( point.x - referenceMin ) / sourceRange;
          point.x = targetMin + normalizedX * targetRange;
        }
      }
    }

    if ( resolved.kind == StretchKind::PhotoshopLevels )
      transferCurve = gammaCurve( bandMin, bandMax, resolved.gamma );
    else if ( resolved.kind == StretchKind::HistogramEqualize )
    {
      double histogramMin = bandMin;
      double histogramMax = bandMax;
      if ( bandRange( provider, band, histogramMin, histogramMax ) )
      {
        bandMin = histogramMin;
        bandMax = histogramMax;
      }
      transferCurve = equalizationCurve( provider, band, bandMin, bandMax );
    }
    else if ( resolved.kind == StretchKind::PercentClip )
    {
      double dataMin = bandMin;
      double dataMax = bandMax;
      if ( bandRange( provider, band, dataMin, dataMax ) )
        percentileRange( provider, band, dataMin, dataMax,
                         resolved.clipPercent, bandMin, bandMax );
    }
    else if ( resolved.kind == StretchKind::StdDev )
    {
      const QgsRasterBandStats stats = provider->bandStatistics(
        band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max
              | Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );
      if ( std::isfinite( stats.mean ) && std::isfinite( stats.stdDev )
           && stats.stdDev > 0.0 )
      {
        bandMin = std::max( stats.minimumValue,
                            stats.mean - resolved.stdDevK * stats.stdDev );
        bandMax = std::min( stats.maximumValue,
                            stats.mean + resolved.stdDevK * stats.stdDev );
        ensureStrictRange( bandMin, bandMax );
      }
    }

    Qgis::DataType dataType = Qgis::DataType::Byte;
    if ( band >= 1 )
      dataType = provider->dataType( band );

    auto *ce = new QgsContrastEnhancement( dataType );
    ce->setMinimumValue( bandMin, false );
    ce->setMaximumValue( bandMax, false );

    // True piecewise linear: UserDefined CE with segment interpolation.
    // (Previously only endpoints were applied via StretchToMinimumMaximum — middle
    //  control points had no effect, so 「分段线性拉伸」appeared broken.)
    if ( ( resolved.kind == StretchKind::PiecewiseLinear
           || resolved.kind == StretchKind::PhotoshopLevels
           || resolved.kind == StretchKind::HistogramEqualize )
         && transferCurve.size() >= 2 )
    {
      auto *fn = new PiecewiseLinearEnhancement( dataType, bandMin, bandMax,
                                                 std::move( transferCurve ) );
      ce->setContrastEnhancementFunction( fn ); // takes ownership, builds LUT when possible
      return ce;
    }

    ce->setContrastEnhancementAlgorithm(
      resolved.disableEnhancement
        ? QgsContrastEnhancement::NoEnhancement
        : QgsContrastEnhancement::StretchToMinimumMaximum,
      true );
    return ce;
  };

  if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( newRenderer.get() ) )
  {
    gray->setContrastEnhancement( createEnhancement( gray->inputBand() ) );
  }
  else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( newRenderer.get() ) )
  {
    const bool master = resolved.scope == ChannelScope::MasterRgb;
    if ( master || resolved.scope == ChannelScope::Red
         || ( resolved.scope == ChannelScope::ActiveGrayBand
              && resolved.referenceBand == rgb->redBand() ) )
      rgb->setRedContrastEnhancement( createEnhancement( rgb->redBand() ) );
    if ( master || resolved.scope == ChannelScope::Green
         || ( resolved.scope == ChannelScope::ActiveGrayBand
              && resolved.referenceBand == rgb->greenBand() ) )
      rgb->setGreenContrastEnhancement( createEnhancement( rgb->greenBand() ) );
    if ( master || resolved.scope == ChannelScope::Blue
         || ( resolved.scope == ChannelScope::ActiveGrayBand
              && resolved.referenceBand == rgb->blueBand() ) )
      rgb->setBlueContrastEnhancement( createEnhancement( rgb->blueBand() ) );
  }
  else
  {
    return ApplyStretchResult::fail( StretchErrorCode::UnsupportedRenderer,
                                     "unsupported renderer type" );
  }

  layer->setRenderer( newRenderer.release() );
  layer->triggerRepaint();

  ApplyStretchSuccess ok;
  ok.applied = resolved;
  ok.applied.displayMin = minV;
  ok.applied.displayMax = maxV;
  ok.repaintRequested = true;
  return ApplyStretchResult::ok( ok );
}

StretchResult<BandStats> QgsBandStatsSource::stats( void *layerToken, int band ) const
{
  auto *layer = asLayer( layerToken );
  if ( !layer )
    return StretchResult<BandStats>::fail( StretchErrorCode::LayerGone, "layer is null" );
  if ( !layer->isValid() )
    return StretchResult<BandStats>::fail( StretchErrorCode::InvalidLayer, "layer invalid" );

  QgsRasterDataProvider *provider = layer->dataProvider();
  if ( !provider )
    return StretchResult<BandStats>::fail( StretchErrorCode::MissingProvider, "no provider" );

  if ( band < 1 || band > layer->bandCount() )
    return StretchResult<BandStats>::fail( StretchErrorCode::InvalidSpec, "band out of range" );

  BandStats out;
  const QgsRasterBandStats minMax = provider->bandStatistics(
    band, Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max );
  out.min = minMax.minimumValue;
  out.max = minMax.maximumValue;
  out.hasMinMax = ( out.max > out.min ) || std::isfinite( out.min );

  const QgsRasterBandStats moments = provider->bandStatistics(
    band, Qgis::RasterBandStatistic::Mean | Qgis::RasterBandStatistic::StdDev );
  out.mean = moments.mean;
  out.stdDev = moments.stdDev;
  out.hasMeanStd = std::isfinite( out.mean ) && std::isfinite( out.stdDev );

  if ( !( out.max > out.min ) )
  {
    out.min = 0.0;
    out.max = 255.0;
    out.hasMinMax = true;
  }

  return StretchResult<BandStats>::ok( out );
}

ApplyStretchResult applyToLayer( QgsRasterLayer *layer, const StretchSpec &spec,
                                 int defaultStatsBand )
{
  if ( !layer )
    return ApplyStretchResult::fail( StretchErrorCode::LayerGone, "layer is null" );

  QgsRasterDisplayTarget target;
  QgsBandStatsSource stats;
  return apply( target, stats, layer, spec, defaultStatsBand );
}

} // namespace rs::display
