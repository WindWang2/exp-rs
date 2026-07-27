// display_stretch_types.h — Display-only stretch value types (no QGIS dependency)
#pragma once

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace rs::display {

enum class StretchKind
{
  NoEnhancement,
  LinearMinMax,
  PercentClip,
  StdDev,
  PhotoshopLevels,
  PiecewiseLinear,
  HistogramEqualize
};

enum class ChannelScope
{
  ActiveGrayBand,
  MasterRgb,
  Red,
  Green,
  Blue
};

struct ControlPoint
{
  double x = 0.0;
  double y = 0.0;
};

/**
 * Immutable intent for one display stretch.
 * Irrelevant fields are ignored per kind (validated only when required).
 */
class StretchSpec
{
public:
  static StretchSpec noEnhancement( ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec linearMinMax( double minV, double maxV,
                                   ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec percentClip( double totalClipPercent,
                                  ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec stdDev( double k, ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec levels( double black, double white, double gamma,
                             ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec piecewise( std::vector<ControlPoint> points,
                                ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec histogramEqualize( ChannelScope scope = ChannelScope::MasterRgb );
  static StretchSpec realDataRange( ChannelScope scope = ChannelScope::MasterRgb );

  StretchKind kind() const { return m_kind; }
  ChannelScope scope() const { return m_scope; }
  std::optional<int> statsBand() const { return m_statsBand; }
  std::optional<double> minValue() const { return m_min; }
  std::optional<double> maxValue() const { return m_max; }
  std::optional<double> clipPercent() const { return m_clipPercent; }
  std::optional<double> stdDevK() const { return m_stdDevK; }
  std::optional<double> gamma() const { return m_gamma; }
  const std::vector<ControlPoint> &points() const { return m_points; }

  StretchSpec withStatsBand( int band ) const;

private:
  StretchKind m_kind = StretchKind::LinearMinMax;
  ChannelScope m_scope = ChannelScope::MasterRgb;
  std::optional<int> m_statsBand;
  std::optional<double> m_min;
  std::optional<double> m_max;
  std::optional<double> m_clipPercent;
  std::optional<double> m_stdDevK;
  std::optional<double> m_gamma;
  std::vector<ControlPoint> m_points;
};

struct BandStats
{
  double min = 0.0;
  double max = 255.0;
  double mean = 0.0;
  double stdDev = 0.0;
  bool hasMinMax = false;
  bool hasMeanStd = false;
};

struct ResolvedStretch
{
  StretchKind kind = StretchKind::LinearMinMax;
  ChannelScope scope = ChannelScope::MasterRgb;
  double displayMin = 0.0;
  double displayMax = 255.0;
  double gamma = 1.0;
  double clipPercent = 2.0;
  double stdDevK = 2.0;
  bool disableEnhancement = false;
  /** True when named intent was approximated (e.g. HistogramEq → linear range). */
  bool approximated = false;
  int referenceBand = 1;
  std::vector<ControlPoint> transferCurve;
};

enum class StretchErrorCode
{
  LayerGone,
  InvalidLayer,
  UnsupportedRenderer,
  MissingProvider,
  InvalidSpec,
  StatsUnavailable,
  ApplyFailed
};

struct StretchError
{
  StretchErrorCode code = StretchErrorCode::ApplyFailed;
  std::string message;
};

template<typename T>
class StretchResult
{
public:
  static StretchResult ok( T value )
  {
    StretchResult r;
    r.m_storage = std::move( value );
    return r;
  }

  static StretchResult fail( StretchError error )
  {
    StretchResult r;
    r.m_storage = std::move( error );
    return r;
  }

  static StretchResult fail( StretchErrorCode code, std::string message )
  {
    return fail( StretchError{ code, std::move( message ) } );
  }

  bool isOk() const { return std::holds_alternative<T>( m_storage ); }
  explicit operator bool() const { return isOk(); }

  const T &value() const { return std::get<T>( m_storage ); }
  T &value() { return std::get<T>( m_storage ); }

  const StretchError &error() const { return std::get<StretchError>( m_storage ); }

private:
  std::variant<T, StretchError> m_storage;
};

struct ApplyStretchSuccess
{
  ResolvedStretch applied;
  bool repaintRequested = false;
};

using ResolveStretchResult = StretchResult<ResolvedStretch>;
using ApplyStretchResult = StretchResult<ApplyStretchSuccess>;

struct DisplayTargetInfo
{
  enum class RendererKind
  {
    SingleBandGray,
    MultiBandColor,
    Unsupported
  };

  RendererKind renderer = RendererKind::Unsupported;
  int grayBand = 1;
  int redBand = 1;
  int greenBand = 2;
  int blueBand = 3;
  int bandCount = 0;
  bool valid = false;
};

/** Ensure max > min using a magnitude-aware epsilon. Returns whether clamping occurred. */
inline bool ensureStrictRange( double &minV, double &maxV )
{
  if ( maxV > minV )
    return false;
  const double eps = std::max( 1.0, std::abs( minV ) * 1e-6 );
  maxV = minV + eps;
  return true;
}

} // namespace rs::display
