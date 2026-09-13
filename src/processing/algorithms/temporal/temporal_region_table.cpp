// src/processing/algorithms/temporal/temporal_region_table.cpp
#include "temporal_region_table.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();

/// Even-odd ray casting on a map point against one polygon. Textual twin of
/// the kernel in rs_temporal_extract_series_operator.cpp (hoisting it into a
/// shared detail header is a recorded follow-up): keep the two in sync so
/// both surfaces implement one membership rule.
bool pointInPolygon( double px, double py,
                     const std::vector<std::array<double, 2>> &poly )
{
  bool inside = false;
  const size_t n = poly.size();
  for ( size_t i = 0, j = n - 1; i < n; j = i++ )
  {
    const double xi = poly[i][0];
    const double yi = poly[i][1];
    const double xj = poly[j][0];
    const double yj = poly[j][1];
    if ( ( yi > py ) != ( yj > py ) &&
         px < ( xj - xi ) * ( py - yi ) / ( yj - yi ) + xi )
      inside = !inside;
  }
  return inside;
}

/// Map -> pixel for a north-up affine geotransform.
bool mapToPixel( const std::array<double, 6> &gt, double mx, double my,
                 double *col, double *row )
{
  if ( std::abs( gt[1] ) < 1e-15 || std::abs( gt[5] ) < 1e-15 )
    return false;
  if ( !std::isfinite( mx ) || !std::isfinite( my ) )
    return false;
  *col = ( mx - gt[0] ) / gt[1];
  *row = ( my - gt[3] ) / gt[5];
  return std::isfinite( *col ) && std::isfinite( *row );
}
} // namespace

bool parseRegionsJson( const Json::Value &regionsJson, int maxRegions,
                       QVector<RegionRef> *out, QString *error )
{
  out->clear();
  if ( !regionsJson.isArray() )
  {
    if ( error )
      *error = QStringLiteral( "regions must be a JSON array" );
    return false;
  }
  QSet<QString> ids;
  for ( const Json::Value &entry : regionsJson )
  {
    if ( out->size() >= maxRegions )
    {
      if ( error )
        *error = QStringLiteral( "regions count exceeds max_regions (%1)" )
                   .arg( maxRegions );
      return false;
    }
    if ( !entry.isObject() )
    {
      if ( error )
        *error = QStringLiteral( "regions[%1] must be an object" ).arg( out->size() );
      return false;
    }
    RegionRef region;
    // Type-guarded before asString(): a non-string id must surface as the
    // contracted parse error, never as a jsoncpp LogicError from run().
    const Json::Value &idValue = entry["id"];
    if ( !idValue.isString() )
    {
      if ( error )
        *error = QStringLiteral( "regions[%1].id must be a non-empty string" )
                   .arg( out->size() );
      return false;
    }
    region.id = QString::fromStdString( idValue.asString() );
    if ( region.id.isEmpty() )
    {
      if ( error )
        *error = QStringLiteral( "regions[%1].id must be a non-empty string" )
                   .arg( out->size() );
      return false;
    }
    // Region ids become CSV join keys: refuse separators that would corrupt
    // the table (RFC 4180 specials + newlines) instead of escaping silently.
    if ( region.id.contains( ',' ) || region.id.contains( '"' ) ||
         region.id.contains( '\n' ) || region.id.contains( '\r' ) )
    {
      if ( error )
        *error = QStringLiteral( "regions[%1].id must not contain ',', quote "
                                 "or newline characters" )
                   .arg( out->size() );
      return false;
    }
    if ( ids.contains( region.id ) )
    {
      if ( error )
        *error = QStringLiteral( "duplicate region id '%1'" ).arg( region.id );
      return false;
    }
    ids.insert( region.id );

    const Json::Value &point = entry["point"];
    const Json::Value &polygon = entry["polygon"];
    const bool hasPoint = point.isArray() && point.size() >= 2 &&
                          point[0].isNumeric() && point[1].isNumeric();
    if ( hasPoint && polygon.isArray() )
    {
      if ( error )
        *error = QStringLiteral( "regions[%1] declares both point and polygon" )
                   .arg( out->size() );
      return false;
    }
    if ( hasPoint )
    {
      region.isPoint = true;
      region.x = point[0].asDouble();
      region.y = point[1].asDouble();
      if ( !std::isfinite( region.x ) || !std::isfinite( region.y ) )
      {
        if ( error )
          *error = QStringLiteral( "regions[%1].point must be finite" ).arg( out->size() );
        return false;
      }
    }
    else if ( polygon.isArray() && polygon.size() >= 3 )
    {
      region.isPoint = false;
      for ( const Json::Value &pt : polygon )
      {
        if ( !pt.isArray() || pt.size() < 2 || !pt[0].isNumeric() || !pt[1].isNumeric() ||
             !std::isfinite( pt[0].asDouble() ) || !std::isfinite( pt[1].asDouble() ) )
        {
          if ( error )
            *error = QStringLiteral( "regions[%1].polygon entries must be finite [x, y] pairs" )
                       .arg( out->size() );
          return false;
        }
        region.polygon.push_back( { pt[0].asDouble(), pt[1].asDouble() } );
      }
    }
    else
    {
      if ( error )
        *error = QStringLiteral( "regions[%1] needs 'point': [x, y] or "
                                 "'polygon': [[x, y], ...] (>= 3 vertices)" )
                   .arg( out->size() );
      return false;
    }
    out->push_back( region );
  }
  if ( out->isEmpty() )
  {
    if ( error )
      *error = QStringLiteral( "regions array is empty" );
    return false;
  }
  return true;
}

bool buildRegionGeometry( const RegionRef &region, int regionIndex,
                          const std::array<double, 6> &geoTransform,
                          int gridWidth, int gridHeight,
                          RegionGeometry *out, QString *error )
{
  out->regionIndex = regionIndex;
  out->isPoint = region.isPoint;
  out->insideOffsets.clear();

  if ( region.isPoint )
  {
    double col = 0.0;
    double row = 0.0;
    if ( !mapToPixel( geoTransform, region.x, region.y, &col, &row ) )
    {
      if ( error )
        *error = QStringLiteral( "region '%1': point is not mappable into the grid" )
                   .arg( region.id );
      return false;
    }
    const int c = static_cast<int>( std::floor( col ) );
    const int r = static_cast<int>( std::floor( row ) );
    if ( c < 0 || r < 0 || c >= gridWidth || r >= gridHeight )
    {
      if ( error )
        *error = QStringLiteral( "region '%1': point [%2, %3] falls outside the "
                                 "collection grid" )
                   .arg( region.id ).arg( region.x ).arg( region.y );
      return false;
    }
    out->xOff = c;
    out->yOff = r;
    out->w = 1;
    out->h = 1;
    return true;
  }

  // Polygon: bbox window clamped to the grid, membership by pixel center.
  double minX = region.polygon.front()[0];
  double maxX = minX;
  double minY = region.polygon.front()[1];
  double maxY = minY;
  for ( const auto &v : region.polygon )
  {
    minX = std::min( minX, v[0] );
    maxX = std::max( maxX, v[0] );
    minY = std::min( minY, v[1] );
    maxY = std::max( maxY, v[1] );
  }
  double c0 = 0.0;
  double r0 = 0.0;
  double c1 = 0.0;
  double r1 = 0.0;
  if ( !mapToPixel( geoTransform, minX, maxY, &c0, &r0 ) ||
       !mapToPixel( geoTransform, maxX, minY, &c1, &r1 ) )
  {
    if ( error )
      *error = QStringLiteral( "region '%1': polygon is not mappable into the grid" )
                 .arg( region.id );
    return false;
  }
  int xOff = static_cast<int>( std::floor( std::min( c0, c1 ) ) );
  int yOff = static_cast<int>( std::floor( std::min( r0, r1 ) ) );
  int xEnd = static_cast<int>( std::ceil( std::max( c0, c1 ) ) );
  int yEnd = static_cast<int>( std::ceil( std::max( r0, r1 ) ) );
  xOff = std::clamp( xOff, 0, gridWidth );
  xEnd = std::clamp( xEnd, 0, gridWidth );
  yOff = std::clamp( yOff, 0, gridHeight );
  yEnd = std::clamp( yEnd, 0, gridHeight );
  const int w = xEnd - xOff;
  const int h = yEnd - yOff;
  if ( w <= 0 || h <= 0 )
  {
    if ( error )
      *error = QStringLiteral( "region '%1': polygon lies outside the collection grid" )
                 .arg( region.id );
    return false;
  }
  out->xOff = xOff;
  out->yOff = yOff;
  out->w = w;
  out->h = h;
  for ( int r = 0; r < h; ++r )
  {
    for ( int c = 0; c < w; ++c )
    {
      // Pixel center in map coordinates.
      const double mx = geoTransform[0] + ( xOff + c + 0.5 ) * geoTransform[1];
      const double my = geoTransform[3] + ( yOff + r + 0.5 ) * geoTransform[5];
      if ( pointInPolygon( mx, my, region.polygon ) )
        out->insideOffsets.push_back( r * w + c );
    }
  }
  if ( out->insideOffsets.empty() )
  {
    // A polygon whose bbox intersects the grid but holds no pixel center is
    // degenerate at this resolution — refuse (never silently empty stats).
    if ( error )
      *error = QStringLiteral( "region '%1': polygon contains no pixel center" )
                 .arg( region.id );
    return false;
  }
  return true;
}

RegionDateReducer::RegionDateReducer( int regionCount, size_t medianScratchFloats,
                                      const std::vector<size_t> &regionInsideCounts )
{
  const size_t n = static_cast<size_t>( std::max( 0, regionCount ) );
  m_stats.resize( n );
  m_mean.assign( n, 0.0 );
  m_m2.assign( n, 0.0 );
  m_min.assign( n, kNanF );
  m_max.assign( n, kNanF );
  m_count.assign( n, 0 );
  m_regionStarts.assign( n + 1, 0 );
  for ( size_t i = 0; i < n; ++i )
    m_regionStarts[i + 1] = m_regionStarts[i] + regionInsideCounts[i];
  m_medianEnabled = medianScratchFloats > 0 &&
                    m_regionStarts[n] <= medianScratchFloats;
  if ( m_medianEnabled )
    m_medianScratch.assign( m_regionStarts[n], kNanF );
  m_regionWrites.assign( n, 0 );
  beginDate();
}

void RegionDateReducer::beginDate()
{
  for ( size_t i = 0; i < m_stats.size(); ++i )
  {
    m_stats[i] = RegionDateStats{};
    m_stats[i].mean = kNanF;
    m_stats[i].median = kNanF;
    m_mean[i] = 0.0;
    m_m2[i] = 0.0;
    m_min[i] = kNanF;
    m_max[i] = kNanF;
    m_count[i] = 0;
    m_regionWrites[i] = 0;
  }
  m_totalSamples = 0;
}

void RegionDateReducer::addSample( int regionIndex, float value )
{
  const size_t i = static_cast<size_t>( regionIndex );
  if ( i >= m_stats.size() || !std::isfinite( value ) )
    return;
  ++m_count[i];
  ++m_totalSamples;
  const double v = static_cast<double>( value );
  const double delta = v - m_mean[i];
  m_mean[i] += delta / static_cast<double>( m_count[i] );
  m_m2[i] += delta * ( v - m_mean[i] );
  if ( !std::isfinite( m_min[i] ) || value < m_min[i] )
    m_min[i] = value;
  if ( !std::isfinite( m_max[i] ) || value > m_max[i] )
    m_max[i] = value;
  if ( m_medianEnabled )
  {
    const size_t slot = m_regionStarts[i] + m_regionWrites[i];
    if ( slot < m_medianScratch.size() )
      m_medianScratch[slot] = value;
    ++m_regionWrites[i];
  }
}

void RegionDateReducer::endDate()
{
  for ( size_t i = 0; i < m_stats.size(); ++i )
  {
    RegionDateStats &s = m_stats[i];
    s.validCount = m_count[i];
    if ( m_count[i] > 0 )
    {
      s.mean = m_mean[i];
      s.min = m_min[i];
      s.max = m_max[i];
      s.stddev = std::sqrt( m_m2[i] / static_cast<double>( m_count[i] ) );
      if ( m_medianEnabled && m_regionWrites[i] > 0 )
      {
        const size_t lo = m_regionStarts[i];
        // Defensive clamp: writes can never exceed the declared region count,
        // but a mis-declared regionInsideCounts must not sort out of bounds.
        const size_t cnt = std::min( m_regionWrites[i], m_medianScratch.size() - lo );
        std::sort( m_medianScratch.begin() + static_cast<std::ptrdiff_t>( lo ),
                   m_medianScratch.begin() + static_cast<std::ptrdiff_t>( lo + cnt ) );
        s.median = cnt % 2 == 1
                     ? m_medianScratch[lo + cnt / 2]
                     : 0.5f * ( m_medianScratch[lo + cnt / 2 - 1] +
                                m_medianScratch[lo + cnt / 2] );
      }
    }
    else
    {
      s.mean = kNanF;
      s.min = kNanF;
      s.max = kNanF;
      s.stddev = kNanF;
      s.median = kNanF;
    }
  }
}

} // namespace sicnu::temporal
