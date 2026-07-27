// display_stretch_apply.cpp — resolve + apply through ports
#include "display_stretch.h"

namespace rs::display {

StretchResult<DisplayTargetInfo> RecordingDisplayTarget::inspect( void *layerToken ) const
{
  (void) layerToken;
  calls.push_back( Call{ Call::Op::Inspect, {} } );
  if ( failInspect )
    return StretchResult<DisplayTargetInfo>::fail( *failInspect );
  if ( !cannedInfo.valid )
    return StretchResult<DisplayTargetInfo>::fail( StretchErrorCode::InvalidLayer,
                                                   "recording target: invalid canned info" );
  return StretchResult<DisplayTargetInfo>::ok( cannedInfo );
}

ApplyStretchResult RecordingDisplayTarget::apply( void *layerToken,
                                                  const ResolvedStretch &resolved )
{
  (void) layerToken;
  calls.push_back( Call{ Call::Op::Apply, resolved } );
  if ( failApply )
    return ApplyStretchResult::fail( *failApply );
  ApplyStretchSuccess ok;
  ok.applied = resolved;
  ok.repaintRequested = true;
  return ApplyStretchResult::ok( ok );
}

StretchResult<BandStats> RecordingBandStats::stats( void *layerToken, int band ) const
{
  (void) layerToken;
  lastBand = band;
  if ( fail )
    return StretchResult<BandStats>::fail( *fail );
  return StretchResult<BandStats>::ok( canned );
}

ApplyStretchResult apply( RasterDisplayTarget &target,
                          BandStatsSource &statsSource,
                          void *layerToken,
                          const StretchSpec &spec,
                          int defaultStatsBand )
{
  if ( !layerToken )
    return ApplyStretchResult::fail( StretchErrorCode::LayerGone, "layer is null" );

  if ( auto err = validate( spec ) )
    return ApplyStretchResult::fail( *err );

  auto infoResult = target.inspect( layerToken );
  if ( !infoResult )
    return ApplyStretchResult::fail( infoResult.error() );

  const DisplayTargetInfo &info = infoResult.value();
  if ( !info.valid )
    return ApplyStretchResult::fail( StretchErrorCode::InvalidLayer, "layer invalid" );
  if ( info.renderer == DisplayTargetInfo::RendererKind::Unsupported )
    return ApplyStretchResult::fail( StretchErrorCode::UnsupportedRenderer,
                                     "renderer is not gray or multi-band color" );

  BandStats stats;
  stats.hasMinMax = false;
  const int band = spec.statsBand().value_or( defaultStatsBand );

  if ( needsBandStats( spec ) || needsMeanStd( spec ) ||
       ( spec.kind() == StretchKind::LinearMinMax && !spec.minValue().has_value() ) )
  {
    auto statsResult = statsSource.stats( layerToken, band );
    if ( !statsResult )
    {
      // Piecewise with explicit points may still resolve without stats
      if ( spec.kind() == StretchKind::PiecewiseLinear )
        stats = BandStats{};
      else
        return ApplyStretchResult::fail( statsResult.error() );
    }
    else
    {
      stats = statsResult.value();
    }
  }

  auto resolvedResult = resolve( spec, stats );
  if ( !resolvedResult )
    return ApplyStretchResult::fail( resolvedResult.error() );

  // Align scope with actual renderer when MasterRgb on gray
  ResolvedStretch resolved = resolvedResult.value();
  resolved.referenceBand = band;
  if ( info.renderer == DisplayTargetInfo::RendererKind::SingleBandGray )
    resolved.scope = ChannelScope::ActiveGrayBand;

  return target.apply( layerToken, resolved );
}

} // namespace rs::display
