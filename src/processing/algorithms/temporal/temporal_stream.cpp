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
  if ( m_nativeBytes.size() < samples * maskElemSize )
    m_nativeBytes.resize( samples * maskElemSize );
  if ( m_maskBytes.size() < samples )
    m_maskBytes.resize( samples );
  if ( m_qaU16.size() < samples )
    m_qaU16.resize( samples );
  if ( m_sclU8.size() < samples )
    m_sclU8.resize( samples );
  // bytes -> 4-byte float slots, rounded up
  const std::uint64_t bytes = static_cast<std::uint64_t>( samples ) * ( maskElemSize + 1 ) +
                              static_cast<std::uint64_t>( samples ) * 3; // qa + scl scratch
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
                                           float *values, bool skipMasking )
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
  const bool sceneDeclaresScale =
      sceneIndex < m_radiometry.size() && m_radiometry.at( sceneIndex ).scaleDefined;
  if ( m_options.applyScaleOffset && m_report.scaleOffsetDeclared &&
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

  const int dtype = ds.bandDataType( rad.maskBand );
  const int elemSize = ( dtype == GDT_Byte ) ? 1 : 2; // preflight rejected wider dtypes
  ensureScratch( pixels, static_cast<size_t>( elemSize ) );
  if ( !ds.readBandWindowNative( rad.maskBand, x, y, w, h, m_nativeBytes.data() ) )
    return false; // fail closed: an unreadable mask must not silently pass clouds

  if ( rad.maskKind == QLatin1String( "sentinel2_scl" ) )
  {
    bool classes[16] = {};
    for ( int c : temporal_mask_defaults::kSclMaskedClasses )
      classes[c] = true;
    if ( elemSize == 1 )
    {
      QaMask::sclMask( m_nativeBytes.data(), m_maskBytes.data(), pixels, classes );
    }
    else
    {
      // Wide SCL-like band: reduce to its low byte before class matching.
      for ( size_t i = 0; i < pixels; ++i )
      {
        std::uint16_t wide = 0;
        std::memcpy( &wide, m_nativeBytes.data() + i * 2, 2 );
        m_sclU8[i] = static_cast<std::uint8_t>( wide & 0xFF );
      }
      QaMask::sclMask( m_sclU8.data(), m_maskBytes.data(), pixels, classes );
    }
  }
  else if ( rad.maskKind == QLatin1String( "landsat_qa_pixel" ) )
  {
    if ( elemSize == 1 )
    {
      for ( size_t i = 0; i < pixels; ++i )
        m_qaU16[i] = m_nativeBytes.data()[i];
    }
    else
    {
      std::memcpy( m_qaU16.data(), m_nativeBytes.data(), pixels * 2 );
    }
    QaMask::landsatQaMask( m_qaU16.data(), m_maskBytes.data(), pixels,
                           temporal_mask_defaults::kLandsatFlags );
  }
  else if ( rad.maskKind == QLatin1String( "explicit" ) )
  {
    // User-designated 0/1 validity mask band: any non-zero sample masks.
    for ( size_t i = 0; i < pixels; ++i )
    {
      std::uint16_t sample = 0;
      if ( elemSize == 1 )
        sample = m_nativeBytes.data()[i];
      else
        std::memcpy( &sample, m_nativeBytes.data() + i * 2, 2 );
      m_maskBytes[i] = sample != 0 ? 1 : 0;
    }
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
                                            float *out, bool skipMasking )
{
  if ( sceneIndex < 0 || sceneIndex >= sceneCount() )
    return false;
  const GdalDatasetWrapper &ds = *m_datasets.at( sceneIndex );
  if ( band < 1 || band > ds.bandCount() )
    return false;

  int x = 0, y = 0, w = 0, h = 0;
  tileRect( tileIndex, &x, &y, &w, &h );
  return readSceneBandWindow( sceneIndex, band, x, y, w, h, out, skipMasking );
}

bool TemporalTileReader::readSceneBandWindow( int sceneIndex, int band, int xOff, int yOff,
                                              int w, int h, float *out, bool skipMasking )
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
  return normalizeAndMask( sceneIndex, band, xOff, yOff, w, h, out, skipMasking );
}

bool TemporalTileReader::readSceneBandPixel( int sceneIndex, int band, int x, int y, float *out )
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
  if ( m_options.applyScaleOffset && m_report.scaleOffsetDeclared && m_report.uniformScaleOffset )
    v = static_cast<float>( m_report.uniformScale * v + m_report.uniformOffset );

  // Per-pixel QA/cloud masking (same kernels, single-sample).
  if ( m_options.applyQaMasking && sceneIndex < m_radiometry.size() )
  {
    const SceneRadiometry &rad = m_radiometry.at( sceneIndex );
    if ( rad.maskBand > 0 && !rad.maskKind.isEmpty() )
    {
      std::uint16_t maskSample = 0;
      bool masked = false;
      if ( ds.readBandWindowNative( rad.maskBand, x, y, 1, 1, &maskSample ) )
      {
        std::uint8_t flag = 0;
        if ( rad.maskKind == QLatin1String( "sentinel2_scl" ) )
        {
          bool classes[16] = {};
          for ( int c : temporal_mask_defaults::kSclMaskedClasses )
            classes[c] = true;
          const std::uint8_t scl = static_cast<std::uint8_t>( maskSample & 0xFF );
          QaMask::sclMask( &scl, &flag, 1, classes );
        }
        else if ( rad.maskKind == QLatin1String( "landsat_qa_pixel" ) )
        {
          QaMask::landsatQaMask( &maskSample, &flag, 1, temporal_mask_defaults::kLandsatFlags );
        }
        else if ( rad.maskKind == QLatin1String( "explicit" ) )
        {
          flag = maskSample != 0 ? 1 : 0;
        }
        masked = flag != 0;
      }
      if ( masked )
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
  const std::uint64_t bytes = static_cast<std::uint64_t>( m_nativeBytes.size() ) +
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
  // reader scratch: native(2B) + mask(1B) + qa(2B) + scl(1B) per pixel = 6 B
  const std::uint64_t readerBytes = tilePixels * 6;
  const std::uint64_t callerSlots = tilePixels * buffersPerPixel + tilePixels * accumulatorFloatsPerPixel;
  return readerBytes + callerSlots * sizeof( float );
}

} // namespace sicnu::temporal
