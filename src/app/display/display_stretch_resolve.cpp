// display_stretch_resolve.cpp — pure validate + resolve
#include "display_stretch.h"

#include <algorithm>
#include <cmath>

namespace rs::display {

bool needsBandStats( const StretchSpec &spec )
{
  switch ( spec.kind() )
  {
    case StretchKind::PercentClip:
    case StretchKind::StdDev:
    case StretchKind::NoEnhancement:
    case StretchKind::HistogramEqualize:
      return true;
    case StretchKind::LinearMinMax:
    case StretchKind::PhotoshopLevels:
      return !spec.minValue().has_value() || !spec.maxValue().has_value();
    case StretchKind::PiecewiseLinear:
      return false;
  }
  return false;
}

bool needsMeanStd( const StretchSpec &spec )
{
  return spec.kind() == StretchKind::StdDev;
}

std::optional<StretchError> validate( const StretchSpec &spec )
{
  switch ( spec.kind() )
  {
    case StretchKind::LinearMinMax:
    case StretchKind::PhotoshopLevels:
      if ( spec.minValue().has_value() && spec.maxValue().has_value() )
      {
        if ( !std::isfinite( *spec.minValue() ) || !std::isfinite( *spec.maxValue() ) )
          return StretchError{ StretchErrorCode::InvalidSpec, "min/max must be finite" };
      }
      if ( spec.kind() == StretchKind::PhotoshopLevels && spec.gamma().has_value() )
      {
        if ( !std::isfinite( *spec.gamma() ) || *spec.gamma() <= 0.0 )
          return StretchError{ StretchErrorCode::InvalidSpec, "gamma must be > 0" };
      }
      break;

    case StretchKind::PercentClip:
      if ( !spec.clipPercent().has_value() || !std::isfinite( *spec.clipPercent() ) )
        return StretchError{ StretchErrorCode::InvalidSpec, "clip percent required" };
      if ( *spec.clipPercent() < 0.0 || *spec.clipPercent() >= 100.0 )
        return StretchError{ StretchErrorCode::InvalidSpec, "clip percent must be in [0, 100)" };
      break;

    case StretchKind::StdDev:
      if ( !spec.stdDevK().has_value() || !std::isfinite( *spec.stdDevK() ) )
        return StretchError{ StretchErrorCode::InvalidSpec, "stdDev k required" };
      if ( *spec.stdDevK() <= 0.0 )
        return StretchError{ StretchErrorCode::InvalidSpec, "stdDev k must be > 0" };
      break;

    case StretchKind::PiecewiseLinear:
      if ( spec.points().size() < 2 )
        return StretchError{ StretchErrorCode::InvalidSpec, "piecewise needs ≥ 2 points" };
      for ( const auto &p : spec.points() )
      {
        if ( !std::isfinite( p.x ) || !std::isfinite( p.y ) )
          return StretchError{ StretchErrorCode::InvalidSpec, "piecewise points must be finite" };
      }
      break;

    case StretchKind::NoEnhancement:
    case StretchKind::HistogramEqualize:
      break;
  }
  return std::nullopt;
}

static BandStats fallbackStats()
{
  BandStats s;
  s.min = 0.0;
  s.max = 255.0;
  s.mean = 127.5;
  s.stdDev = 1.0;
  s.hasMinMax = true;
  s.hasMeanStd = true;
  return s;
}

ResolveStretchResult resolve( const StretchSpec &spec, const BandStats &statsIn )
{
  if ( auto err = validate( spec ) )
    return ResolveStretchResult::fail( *err );

  BandStats stats = statsIn;
  if ( !stats.hasMinMax && needsBandStats( spec ) && spec.kind() != StretchKind::PiecewiseLinear )
  {
    // Allow resolve with incomplete stats only when min/max provided on Spec
    if ( !( spec.minValue().has_value() && spec.maxValue().has_value() ) )
      return ResolveStretchResult::fail( StretchErrorCode::StatsUnavailable,
                                         "band min/max unavailable" );
  }
  if ( !stats.hasMinMax )
  {
    stats = fallbackStats();
    stats.hasMinMax = true;
  }

  ResolvedStretch out;
  out.kind = spec.kind();
  out.scope = spec.scope();
  out.gamma = spec.gamma().value_or( 1.0 );
  out.clipPercent = spec.clipPercent().value_or( 2.0 );
  out.stdDevK = spec.stdDevK().value_or( 2.0 );
  out.disableEnhancement = false;
  out.approximated = false;

  switch ( spec.kind() )
  {
    case StretchKind::NoEnhancement:
      out.displayMin = stats.min;
      out.displayMax = stats.max;
      out.disableEnhancement = true;
      break;

    case StretchKind::LinearMinMax:
      out.displayMin = spec.minValue().value_or( stats.min );
      out.displayMax = spec.maxValue().value_or( stats.max );
      break;

    case StretchKind::PhotoshopLevels:
      out.displayMin = spec.minValue().value_or( stats.min );
      out.displayMax = spec.maxValue().value_or( stats.max );
      out.gamma = spec.gamma().value_or( 1.0 );
      break;

    case StretchKind::PercentClip:
    {
      const double total = spec.clipPercent().value_or( 2.0 );
      const double range = stats.max - stats.min;
      const double half = range * ( total / 100.0 ) / 2.0;
      out.displayMin = stats.min + half;
      out.displayMax = stats.max - half;
      break;
    }

    case StretchKind::StdDev:
    {
      if ( !stats.hasMeanStd )
        return ResolveStretchResult::fail( StretchErrorCode::StatsUnavailable,
                                           "mean/stddev unavailable" );
      const double k = spec.stdDevK().value_or( 2.0 );
      out.displayMin = stats.mean - k * stats.stdDev;
      out.displayMax = stats.mean + k * stats.stdDev;
      break;
    }

    case StretchKind::PiecewiseLinear:
    {
      auto pts = spec.points();
      std::sort( pts.begin(), pts.end(),
                 []( const ControlPoint &a, const ControlPoint &b ) { return a.x < b.x; } );
      out.displayMin = pts.front().x;
      out.displayMax = pts.back().x;
      out.transferCurve = std::move( pts );
      // Full transfer curve is applied via PiecewiseLinearEnhancement (not approximated).
      out.approximated = false;
      break;
    }

    case StretchKind::HistogramEqualize:
      out.displayMin = stats.min;
      out.displayMax = stats.max;
      break;
  }

  ensureStrictRange( out.displayMin, out.displayMax );
  return ResolveStretchResult::ok( out );
}

} // namespace rs::display
