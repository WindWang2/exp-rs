// src/processing/algorithms/temporal/temporal_stream.cpp
#include "temporal_stream.h"

#include "processing/algorithms/math_utils.h"
#include "processing/algorithms/qa_mask.h"
#include "processing/algorithms/temporal/temporal_band_roles.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace sicnu::temporal
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

TemporalTileReader::TemporalTileReader( const TemporalCollection &collection,
                                        const TemporalPreflightReport &radiometry,
                                        const TemporalStreamOptions &options,
                                        QString *errorMessage )
  : m_scenes( collection.scenes() ),
    m_radiometry( radiometry.radiometry ),
    m_options( options ),
    m_report( radiometry )
{
  const QString failurePrefix = QStringLiteral( "temporal reader: " );
  auto fail = [&]( const QString &msg ) {
    if ( errorMessage && errorMessage->isEmpty() )
      *errorMessage = failurePrefix + msg;
    m_scenes.clear();
    m_radiometry.clear();
    close();
  };
  ensureGdalInit();
  m_options.tileWidth = std::max( 16, m_options.tileWidth );
  m_options.tileHeight = std::max( 16, m_options.tileHeight );

  const int sceneCount = m_scenes.size();
  m_datasets.resize( sceneCount );
  for ( int i = 0; i < sceneCount; ++i )
  {
    m_datasets[i] = std::make_unique<GdalDatasetWrapper>();
    if ( !m_datasets[i]->open( m_scenes.at( i ).path ) )
    {
      m_datasets[i].reset();
      fail( QStringLiteral( "cannot open scene: %1" ).arg( m_scenes.at( i ).path ) );
      return;
    }
  }

  // Reference grid: scene 0 of the chronologically sorted collection.
  const GdalDatasetWrapper *ref = m_datasets.at( 0 ).get();
  m_width = ref->width();
  m_height = ref->height();
  m_geoTransform = ref->geoTransform();
  m_projection = ref->projection();
  m_referenceTime = m_scenes.at( 0 ).time;

  // Guard the same-grid contract even when preflight was skipped by a caller.
  for ( int i = 1; i < sceneCount; ++i )
  {
    if ( m_datasets.at( i )->width() != m_width || m_datasets.at( i )->height() != m_height )
    {
      fail( QStringLiteral( "scene grid mismatch: %1" ).arg( m_scenes.at( i ).path ) );
      return;
    }
  }

  ensureScratch( static_cast<size_t>( m_options.tileWidth ) * m_options.tileHeight );
  m_valid = true;
}

void TemporalTileReader::ensureScratch( size_t samples, size_t maskElemSize )
{
  if ( m_maskFloat.size() < samples )
    m_maskFloat.resize( samples );
  if ( m_maskBytes.size() < samples )
    m_maskBytes.resize( samples );
  if ( m_qaU16.size() < samples )
    m_qaU16.resize( samples );
  if ( m_sclU8.size() < samples )
    m_sclU8.resize( samples );
  // reader scratch bytes: mask float (4) + mask bytes (1) + qa (2) + scl (1)
  // = 8 B/pixel; maskElemSize is informational (float read converts).
  (void)maskElemSize;
  const std::uint64_t bytes = static_cast<std::uint64_t>( samples ) * 8;
  m_peakScratchSlots = std::max( m_peakScratchSlots, ( bytes + 3 ) / 4 );
}

TemporalTileReader::~TemporalTileReader()
{
  close();
}

void TemporalTileReader::close()
{
  m_datasets.clear();
}

int TemporalTileReader::tileCountX() const
{
  return ( m_width + m_options.tileWidth - 1 ) / m_options.tileWidth;
}

int TemporalTileReader::tileCountY() const
{
  return ( m_height + m_options.tileHeight - 1 ) / m_options.tileHeight;
}

int TemporalTileReader::totalTileCount() const
{
  return tileCountX() * tileCountY();
}

void TemporalTileReader::tileRect( int tileIndex, int *x, int *y, int *w, int *h ) const
{
  const int tx = tileIndex % tileCountX();
  const int ty = tileIndex / tileCountX();
  *x = tx * m_options.tileWidth;
  *y = ty * m_options.tileHeight;
  *w = std::min( m_options.tileWidth, m_width - *x );
  *h = std::min( m_options.tileHeight, m_height - *y );
}

double TemporalTileReader::sceneDayOffset( int sceneIndex ) const
{
  return m_scenes.at( sceneIndex ).time.daysSince( m_referenceTime );
}

int TemporalTileReader::bandForRole( int sceneIndex, const QString &roleId, int overrideBand,
                                     bool *usedFallback )
{
  if ( sceneIndex < 0 || sceneIndex >= sceneCount() )
    return 0;
  const TemporalSceneRef &s = m_scenes.at( sceneIndex );
  int override = overrideBand;
  if ( override <= 0 )
  {
    const auto it = s.bandOverrides.find( roleId );
    if ( it != s.bandOverrides.end() )
      override = it->second;
  }
  return resolveBand( *m_datasets.at( sceneIndex ), roleId, override, usedFallback );
}

bool TemporalTileReader::normalizeAndMask( int sceneIndex, int band, int x, int y, int w, int h,
                                           float *values, bool skipMasking,
                                           bool skipScaleOffset )
{
  const GdalDatasetWrapper &ds = *m_datasets.at( sceneIndex );
  const size_t pixels = static_cast<size_t>( w ) * h;
  ensureScratch( pixels );

  // 1) Declared finite NoData + non-finite samples -> NaN (rs_change_streaming idiom).
  bool hasNodata = false;
  const double nd = ds.bandNoDataValue( band, &hasNodata );
  const float nodataF = static_cast<float>( nd );
  const bool sweepNodata = hasNodata && std::isfinite( nd );
  for ( size_t i = 0; i < pixels; ++i )
  {
    const float v = values[i];
    if ( !std::isfinite( v ) || ( sweepNodata && v == nodataF ) )
      values[i] = kNan;
  }

  // 2) Explicit, preflight-verified scale/offset normalization (never
  // guessed; belt-and-braces: only scenes that actually declare it).
  // Skippable for auxiliary reads (quality scores stay in native units).
  const bool sceneDeclaresScale =
      sceneIndex < m_radiometry.size() && m_radiometry.at( sceneIndex ).scaleDefined;
  if ( !skipScaleOffset && m_options.applyScaleOffset && m_report.scaleOffsetDeclared &&
       m_report.uniformScaleOffset && sceneDeclaresScale )
  {
    const double scale = m_report.uniformScale;
    const double offset = m_report.uniformOffset;
    if ( scale != 1.0 || offset != 0.0 )
    {
      for ( size_t i = 0; i < pixels; ++i )
        if ( std::isfinite( values[i] ) )
          values[i] = static_cast<float>( scale * static_cast<double>( values[i] ) + offset );
    }
  }

  // 3) QA / cloud masking (shared QaMask kernels).
  if ( skipMasking || !m_options.applyQaMasking )
    return true;
  if ( sceneIndex >= m_radiometry.size() )
    return true;
  const SceneRadiometry &rad = m_radiometry.at( sceneIndex );
  if ( rad.maskBand <= 0 || rad.maskKind.isEmpty() )
    return true;

  // Mask read through the FLOAT window API: GDAL converts any numeric mask
  // dtype (Byte/UInt16/Int32/Float...) losslessly for QA value ranges, so no
  // native-size buffers (and no 16-bit dtype gate) are needed. NaN comes from
  // edge padding and masks those samples (fail closed).
  ensureScratch( pixels, sizeof( float ) );
  if ( !ds.readBandWindow( rad.maskBand, x, y, w, h, m_maskFloat.data() ) )
    return false; // fail closed: an unreadable mask must not silently pass clouds

  if ( rad.maskKind == QLatin1String( "sentinel2_scl" ) )
  {
    // Float read -> class-id bytes (non-finite/out-of-range becomes 0 = SCL
    // no-data, which is masked) -> shared SCL kernel.
    for ( size_t i = 0; i < pixels; ++i )
    {
      const float v = m_maskFloat[i];
      m_sclU8[i] = ( std::isfinite( v ) && v >= 0.0f && v <= 15.0f )
                       ? static_cast<std::uint8_t>( static_cast<int>( v ) )
                       : 0;
    }
    bool classes[16] = {};
    for ( int c : temporal_mask_defaults::kSclMaskedClasses )
      classes[c] = true;
    QaMask::sclMask( m_sclU8.data(), m_maskBytes.data(), pixels, classes );
  }
  else if ( rad.maskKind == QLatin1String( "landsat_qa_pixel" ) )
  {
    // Float read -> QA word (non-finite becomes fill bit 0 -> masked) ->
    // shared QA_PIXEL kernel. QA values fit float's exact integer range.
    for ( size_t i = 0; i < pixels; ++i )
    {
      const float v = m_maskFloat[i];
      m_qaU16[i] = std::isfinite( v ) && v >= 0.0f && v < 65536.0f
                       ? static_cast<std::uint16_t>( static_cast<int>( v ) )
                       : 0;
    }
    QaMask::landsatQaMask( m_qaU16.data(), m_maskBytes.data(), pixels,
                           temporal_mask_defaults::kLandsatFlags );
  }
  else if ( rad.maskKind == QLatin1String( "explicit" ) )
  {
    // User-designated validity mask band: any non-zero (or non-finite) masks.
    for ( size_t i = 0; i < pixels; ++i )
      m_maskBytes[i] = ( !std::isfinite( m_maskFloat[i] ) || m_maskFloat[i] != 0.0f ) ? 1 : 0;
  }
  else
  {
    return true;
  }

  for ( size_t i = 0; i < pixels; ++i )
    if ( m_maskBytes[i] )
      values[i] = kNan;
  return true;
}

bool TemporalTileReader::readSceneBandTile( int sceneIndex, int band, int tileIndex,
                                            float *out, bool skipMasking,
                                            bool skipScaleOffset )
{
  if ( sceneIndex < 0 || sceneIndex >= sceneCount() )
    return false;
  const GdalDatasetWrapper &ds = *m_datasets.at( sceneIndex );
  if ( band < 1 || band > ds.bandCount() )
    return false;

  int x = 0, y = 0, w = 0, h = 0;
  tileRect( tileIndex, &x, &y, &w, &h );
  return readSceneBandWindow( sceneIndex, band, x, y, w, h, out, skipMasking, skipScaleOffset );
}

bool TemporalTileReader::readSceneBandWindow( int sceneIndex, int band, int xOff, int yOff,
                                              int w, int h, float *out, bool skipMasking,
                                              bool skipScaleOffset )
{
  if ( sceneIndex < 0 || sceneIndex >= sceneCount() )
    return false;
  const GdalDatasetWrapper &ds = *m_datasets.at( sceneIndex );
  if ( band < 1 || band > ds.bandCount() )
    return false;
  if ( w <= 0 || h <= 0 || !out )
    return false;
  if ( !ds.readBandWindow( band, xOff, yOff, w, h, out ) )
    return false;
  return normalizeAndMask( sceneIndex, band, xOff, yOff, w, h, out, skipMasking,
                           skipScaleOffset );
}

bool TemporalTileReader::readSceneBandPixel( int sceneIndex, int band, int x, int y, float *out,
                                             bool skipMasking, bool skipScaleOffset )
{
  if ( sceneIndex < 0 || sceneIndex >= sceneCount() )
    return false;
  const GdalDatasetWrapper &ds = *m_datasets.at( sceneIndex );
  if ( band < 1 || band > ds.bandCount() )
    return false;
  float v = kNan;
  if ( !ds.readPixel( band, x, y, &v ) )
    return false;

  bool hasNodata = false;
  const double nd = ds.bandNoDataValue( band, &hasNodata );
  const float nodataF = static_cast<float>( nd );
  if ( !std::isfinite( v ) || ( hasNodata && std::isfinite( nd ) && v == nodataF ) )
  {
    *out = kNan;
    return true;
  }
  // Same per-scene declared-scale gate as the window path (never apply a
  // uniform transform to a scene that does not declare it).
  const bool sceneDeclaresScale =
      sceneIndex < m_radiometry.size() && m_radiometry.at( sceneIndex ).scaleDefined;
  if ( !skipScaleOffset && m_options.applyScaleOffset && m_report.scaleOffsetDeclared &&
       m_report.uniformScaleOffset && sceneDeclaresScale )
    v = static_cast<float>( m_report.uniformScale * v + m_report.uniformOffset );

  // Per-pixel QA/cloud masking (same kernels, single-sample).
  if ( !skipMasking && m_options.applyQaMasking && sceneIndex < m_radiometry.size() )
  {
    const SceneRadiometry &rad = m_radiometry.at( sceneIndex );
    if ( rad.maskBand > 0 && !rad.maskKind.isEmpty() )
    {
      float maskSample = kNan;
      // Fail closed like the window path: an unreadable mask sample must
      // not silently pass clouds (#719).
      if ( !ds.readPixel( rad.maskBand, x, y, &maskSample ) )
      {
        *out = kNan;
        return true;
      }
      std::uint8_t flag = 0;
      if ( rad.maskKind == QLatin1String( "sentinel2_scl" ) )
      {
        bool classes[16] = {};
        for ( int c : temporal_mask_defaults::kSclMaskedClasses )
          classes[c] = true;
        std::uint8_t scl = 0;
        if ( std::isfinite( maskSample ) && maskSample >= 0.0f && maskSample <= 15.0f )
          scl = static_cast<std::uint8_t>( static_cast<int>( maskSample ) );
        QaMask::sclMask( &scl, &flag, 1, classes );
      }
      else if ( rad.maskKind == QLatin1String( "landsat_qa_pixel" ) )
      {
        std::uint16_t qa = 0; // non-finite -> 0 (fill bit set) -> masked
        if ( std::isfinite( maskSample ) && maskSample >= 0.0f && maskSample < 65536.0f )
          qa = static_cast<std::uint16_t>( static_cast<int>( maskSample ) );
        QaMask::landsatQaMask( &qa, &flag, 1, temporal_mask_defaults::kLandsatFlags );
      }
      else if ( rad.maskKind == QLatin1String( "explicit" ) )
      {
        flag = ( !std::isfinite( maskSample ) || maskSample != 0.0f ) ? 1 : 0;
      }
      if ( flag != 0 )
      {
        *out = kNan;
        return true;
      }
    }
  }

  *out = v;
  return true;
}

std::uint64_t TemporalTileReader::internalFloatSlots() const
{
  // Derived from the LIVE scratch buffers (they can outgrow the tile for
  // bbox-window ROI reads) so the number never understates residency.
  const std::uint64_t bytes = static_cast<std::uint64_t>( m_maskFloat.size() ) * 4 +
                              static_cast<std::uint64_t>( m_maskBytes.size() ) +
                              static_cast<std::uint64_t>( m_qaU16.size() ) * 2 +
                              static_cast<std::uint64_t>( m_sclU8.size() );
  return ( bytes + 3 ) / 4;
}

std::uint64_t TemporalTileReader::estimateWorkingSetBytes( int tileWidth, int tileHeight,
                                                           std::uint64_t buffersPerPixel,
                                                           std::uint64_t accumulatorFloatsPerPixel )
{
  const std::uint64_t tilePixels = static_cast<std::uint64_t>( tileWidth ) * tileHeight;
  // reader scratch: mask float (4B) + mask bytes + qa(2B) + scl(1B) = 8 B/pixel
  const std::uint64_t readerBytes = tilePixels * 8;
  const std::uint64_t callerSlots = tilePixels * buffersPerPixel + tilePixels * accumulatorFloatsPerPixel;
  return readerBytes + callerSlots * sizeof( float );
}

} // namespace sicnu::temporal
