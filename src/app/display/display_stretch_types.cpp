// display_stretch_types.cpp
#include "display_stretch_types.h"

namespace rs::display {

StretchSpec StretchSpec::noEnhancement( ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::NoEnhancement;
  s.m_scope = scope;
  return s;
}

StretchSpec StretchSpec::linearMinMax( double minV, double maxV, ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::LinearMinMax;
  s.m_scope = scope;
  s.m_min = minV;
  s.m_max = maxV;
  return s;
}

StretchSpec StretchSpec::percentClip( double totalClipPercent, ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::PercentClip;
  s.m_scope = scope;
  s.m_clipPercent = totalClipPercent;
  return s;
}

StretchSpec StretchSpec::stdDev( double k, ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::StdDev;
  s.m_scope = scope;
  s.m_stdDevK = k;
  return s;
}

StretchSpec StretchSpec::levels( double black, double white, double gamma, ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::PhotoshopLevels;
  s.m_scope = scope;
  s.m_min = black;
  s.m_max = white;
  s.m_gamma = gamma;
  return s;
}

StretchSpec StretchSpec::piecewise( std::vector<ControlPoint> points, ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::PiecewiseLinear;
  s.m_scope = scope;
  s.m_points = std::move( points );
  return s;
}

StretchSpec StretchSpec::histogramEqualize( ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::HistogramEqualize;
  s.m_scope = scope;
  return s;
}

StretchSpec StretchSpec::realDataRange( ChannelScope scope )
{
  StretchSpec s;
  s.m_kind = StretchKind::LinearMinMax;
  s.m_scope = scope;
  // min/max empty → resolve fills from BandStats
  return s;
}

StretchSpec StretchSpec::withStatsBand( int band ) const
{
  StretchSpec s = *this;
  s.m_statsBand = band;
  return s;
}

} // namespace rs::display
