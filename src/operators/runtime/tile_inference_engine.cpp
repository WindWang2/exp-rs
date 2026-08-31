// src/operators/runtime/tile_inference_engine.cpp
#include "operators/runtime/tile_inference_engine.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <map>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sicnu::operators::runtime {

// sicnu::operators::ErrorCode (free enum in rs_operator_error.h, visible via
// the parent namespace).

namespace {

constexpr int kMinTileSize = 16;
constexpr int kDefaultTileSize = 512;

struct CoreTile
{
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

/// Read a halo-extended window as BIP floats, padding fully out-of-raster
/// regions with NaN. readWindowBip clamps to the raster extent itself, but
/// requires non-negative offsets, so left/top overhang is shifted here.
bool readBipWindow( const GdalDatasetWrapper &ds, const std::vector<int> &bands,
                    int winX, int winY, int winW, int winH, float *buffer )
{
  // The window may extend past the raster left/top/inside edges (negative
  // winX/winY from halo, or a right/bottom overhang). Fill NaN, then place
  // the clamped read at its shifted offset so the engine sees the window's
  // true geometry (halo rows/cols are NaN where outside the raster).
  std::fill( buffer, buffer + static_cast<std::size_t>( winW ) * winH * bands.size(),
             std::numeric_limits<float>::quiet_NaN() );
  const int ix0 = std::max( 0, winX );
  const int iy0 = std::max( 0, winY );
  const int ix1 = std::min( ds.width(), winX + winW );
  const int iy1 = std::min( ds.height(), winY + winH );
  const int iw = ix1 - ix0;
  const int ih = iy1 - iy0;
  if ( iw <= 0 || ih <= 0 )
    return true; // fully outside — NaN padding stands
  // Read the clamped region into a temp and scatter it into the window at
  // the shifted offset — never via buffer itself which would require a risky
  // in-place shift + stale-region clear.
  const std::size_t bandCount = bands.size();
  std::vector<float> temp( static_cast<std::size_t>( iw ) * ih * bandCount );
  if ( !ds.readWindowBip( bands, ix0, iy0, iw, ih, temp.data() ) )
    return false;
  const int shiftCols = ix0 - winX;
  const int shiftRows = iy0 - winY;
  for ( int row = 0; row < ih; ++row )
  {
    const std::size_t dstBase =
      ( static_cast<std::size_t>( row + shiftRows ) * winW + shiftCols ) * bandCount;
    const std::size_t srcBase = static_cast<std::size_t>( row ) * iw * bandCount;
    std::copy( temp.begin() + srcBase,
               temp.begin() + srcBase + static_cast<std::size_t>( iw ) * bandCount,
               buffer + dstBase );
  }
  return true;
}

} // namespace

TileInferenceEngine::TileInferenceEngine( ModelInfo model, ModelRuntimePtr runtime )
    : m_model( std::move( model ) ), m_runtime( std::move( runtime ) )
{
}

int TileInferenceEngine::effectiveTileSize( const ModelInfo &model )
{
  int tile = model.tiling.tileSize;
  if ( tile <= 0 && model.input.width > 0 )
    tile = model.input.width; // fixed graph input ⇒ tiles at the input size
  if ( tile <= 0 )
    tile = kDefaultTileSize;
  return std::max( kMinTileSize, tile );
}

int TileInferenceEngine::effectiveHalo( const ModelInfo &model )
{
  return model.tiling.halo > 0 ? model.tiling.halo : model.tiling.overlap / 2;
}

TileInferenceStats TileInferenceEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "tile inference engine has no runtime session" );

  GdalDatasetWrapper ds;
  if ( !ds.open( QString::fromStdString( inputPath ) ) )
    throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + inputPath );
  const int rasterW = ds.width();
  const int rasterH = ds.height();
  const int rasterBands = ds.bandCount();
  if ( rasterW <= 0 || rasterH <= 0 || rasterBands <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + inputPath );

  // Manifest dtype contract (#632): input.dtype must match the raster's
  // actual GDAL type (the engine always reads float32; a mismatched dtype
  // silently misnormalizes).
  if ( !m_model.input.dtype.empty() )
  {
    static const std::map<std::string, int> kAccepted = {
      { "float32", GDT_Float32 }, { "float64", GDT_Float64 },
      { "float16", GDT_Float32 }, { "uint16", GDT_UInt16 },
      { "int16", GDT_Int16 },     { "uint8", GDT_Byte },
      { "int32", GDT_Int32 },     { "uint32", GDT_UInt32 },
    };
    const auto it = kAccepted.find( m_model.input.dtype );
    if ( it == kAccepted.end() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "model manifest declares unsupported input dtype '" + m_model.input.dtype + "'" );
    // NOTE: per-band comparison happens after band selection below (#705.3)
    // — mixed-type VRTs dodged the contract for the actually-fed bands
    // when only band 1 was checked.
    m_declaredDtype = it->second;
  }

  std::vector<int> bandList = bands;
  if ( bandList.empty() )
  {
    bandList.resize( rasterBands );
    for ( int i = 0; i < rasterBands; ++i )
      bandList[static_cast<std::size_t>( i )] = i + 1;
  }
  if ( m_declaredDtype >= 0 )
  {
    for ( int b : bandList )
    {
      if ( ds.bandDataType( b ) != m_declaredDtype )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model manifest requires input dtype '" + m_model.input.dtype
                                 + "' but raster band " + std::to_string( b ) + " has GDAL type "
                                 + std::to_string( ds.bandDataType( b ) )
                                 + " (convert the raster or update the manifest)" );
    }
  }
  else
  {
    for ( int b : bandList )
    {
      if ( b < 1 || b > rasterBands )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "band " + std::to_string( b ) + " out of range (1.." + std::to_string( rasterBands ) + ")" );
    }
  }
  const int bandCount = static_cast<int>( bandList.size() );

  // Manifest band_roles contract (#646): roles feed ranking, but at inference
  // the fed band count must match the declared roles (band i maps to role i,
  // and channel order is the file order - see the blob construction below).
  if ( !m_model.input.bandRoles.empty()
       && m_model.input.bandRoles.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "model manifest declares "
                             + std::to_string( m_model.input.bandRoles.size() )
                             + " band roles but " + std::to_string( bandCount )
                             + " bands are fed (pass an explicit band list or fix the manifest)" );

  // Contract check that needs real data: per-channel mean/std must match the
  // bands actually fed to the model.
  const ModelPreprocessContract &pre = m_model.preprocess;
  const bool meanStd = pre.normalize == "mean_std";
  if ( meanStd && !pre.mean.empty() && pre.mean.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "preprocess.mean declares " + std::to_string( pre.mean.size() )
                             + " channels but " + std::to_string( bandCount ) + " bands are fed" );
  if ( meanStd && !pre.stdv.empty() && pre.stdv.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "preprocess.std declares " + std::to_string( pre.stdv.size() )
                             + " channels but " + std::to_string( bandCount ) + " bands are fed" );

  const int tileSize = std::min( effectiveTileSize( m_model ), std::max( rasterW, rasterH ) );
  const int halo = std::max( 0, effectiveHalo( m_model ) );
  const int batchSize = std::max( 1, m_model.tiling.batchSize );
  const bool resizeToInput = pre.resize == "to_input" && m_model.input.width > 0 && m_model.input.height > 0;
  const int modelW = resizeToInput ? m_model.input.width : 0;
  const int modelH = resizeToInput ? m_model.input.height : 0;
  const int interp = pre.interpolation == "nearest" ? cv::INTER_NEAREST : cv::INTER_LINEAR;

  // Core tile grid over the raster extent.
  std::vector<CoreTile> core;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      core.push_back( CoreTile{ x, y, std::min( tileSize, rasterW - x ), std::min( tileSize, rasterH - y ) } );
  const int totalTiles = static_cast<int>( core.size() );

  TileInferenceStats stats;
  stats.tileSize = tileSize;
  stats.halo = halo;
  stats.batchSize = batchSize;
  stats.tilesPlanned = totalTiles;
  stats.outWidth = rasterW;
  stats.outHeight = rasterH;

  // Reusable read buffer: one halo-extended window at a time.
  const int maxWin = tileSize + 2 * halo;
  std::vector<float> windowBuffer( static_cast<std::size_t>( maxWin ) * maxWin * bandCount );
  // Per-band declared NoData sentinels (the BIP read yields raw values; the
  // shared NaN convention is applied here, matching opencv_utils semantics).
  std::vector<float> bandSentinel( static_cast<std::size_t>( bandCount ), 0.0f );
  std::vector<bool> bandHasSentinel( static_cast<std::size_t>( bandCount ), false );
  for ( int i = 0; i < bandCount; ++i )
  {
    bool has = false;
    const double nd = ds.bandNoDataValue( bandList[static_cast<std::size_t>( i )], &has );
    if ( has && std::isfinite( nd ) )
    {
      bandSentinel[static_cast<std::size_t>( i )] = static_cast<float>( nd );
      bandHasSentinel[static_cast<std::size_t>( i )] = true;
    }
  }

  // Per-batch preprocessed tiles (HWC float32) + their core nodata masks,
  // the fed spatial sizes and the core tiles themselves, awaiting one forward
  // pass. An explicit batchCores vector eliminates any arithmetic derivation
  // of the core tile index in the tail batch.
  std::vector<cv::Mat> batchMats;
  std::vector<cv::Mat> batchMasks;
  std::vector<std::pair<int, int>> batchFedSize;
  std::vector<CoreTile> batchCores;
  std::vector<CoreTile> nodataTiles; // skipped all-nodata cores (#705.4)

  std::unique_ptr<GdalStreamingOutput> writer;
  // Any failure after the writer exists must not leave a truncated GeoTIFF at
  // the caller's output path looking like a result (#647): the catch below
  // abandons the writer so its destructor removes the partial file.
  auto abandonOnFailure = [ &writer ]() {
    if ( writer )
      writer->abandon();
  };
  const std::array<double, 6> geoTransform = ds.geoTransform();
  const QString projection = ds.projection();

  context.reportProgressForced( 0.0, "Tiled inference: " + std::to_string( totalTiles ) + " tiles" );

  int done = 0;
  try
  {
  for ( int tileIndex = 0; tileIndex < totalTiles; ++tileIndex )
  {
    context.throwIfCancelled();

    const CoreTile &t = core[static_cast<std::size_t>( tileIndex ) ];
    const int winX = t.x - halo;
    const int winY = t.y - halo;
    const int winW = t.w + 2 * halo;
    const int winH = t.h + 2 * halo;
    if ( !readBipWindow( ds, bandList, winX, winY, winW, winH, windowBuffer.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed to read tile window at (" + std::to_string( t.x )
                             + ", " + std::to_string( t.y ) + ")" );

    // Declared sentinels → NaN so the mask/zero pipeline below sees them.
    {
      const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        const std::size_t b = i % static_cast<std::size_t>( bandCount );
        if ( bandHasSentinel[b] && windowBuffer[i] == bandSentinel[b] )
          windowBuffer[i] = std::numeric_limits<float>::quiet_NaN();
      }
    }

    // Wrap the window as HWC float and preprocess in place. Pixels where EVERY
    // band is non-finite are marked invalid: the model sees 0 (nodata_policy
    // "zero") and the output pixel is restored to NaN afterwards.
    cv::Mat hwc( winH, winW, CV_32FC( bandCount ), windowBuffer.data() );
    cv::Mat invalidMask( t.h, t.w, CV_8UC1, cv::Scalar( 0 ) );
    {
      const float *src = windowBuffer.data();
      const std::size_t windowStride = static_cast<std::size_t>( winW ) * bandCount;
      for ( int row = 0; row < t.h; ++row )
      {
        // Core region row inside the window starts at halo pixels in.
        const float *winRow = src + static_cast<std::size_t>( row + halo ) * windowStride
                              + static_cast<std::size_t>( halo ) * bandCount;
        uchar *maskRow = invalidMask.ptr<uchar>( row );
        for ( int col = 0; col < t.w; ++col )
        {
          const float *px = winRow + static_cast<std::size_t>( col ) * bandCount;
          bool allInvalid = true;
          for ( int c = 0; c < bandCount; ++c )
          {
            if ( std::isfinite( px[c] ) )
            {
              allInvalid = false;
              break;
            }
          }
          maskRow[col] = allInvalid ? 1 : 0;
        }
      }
      // nodata_policy "zero" (default; the only supported policy - others
      // are rejected at manifest parse, #646): non-finite samples become 0.
      const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        if ( !std::isfinite( windowBuffer[i] ) )
          windowBuffer[i] = 0.0f;
      }
      // Normalize in place (HWC): linear x*scale, mean_std (x-mean)/std*scale.
      const double *meanArr = meanStd && !pre.mean.empty() ? pre.mean.data() : nullptr;
      const double *stdArr = meanStd && !pre.stdv.empty() ? pre.stdv.data() : nullptr;
      const double scale = pre.scale;
      // Scale applies only to linear/mean_std normalization (#646): with
      // normalize "none" the pixels must reach the model unscaled, matching
      // the model_catalog.h contract ("applied last (linear & mean_std)").
      if ( meanStd || ( pre.normalize == "linear" && scale != 1.0 ) )
      {
        float *data = windowBuffer.data();
        for ( std::size_t i = 0; i < totalFloats; ++i )
        {
          const std::size_t c = i % static_cast<std::size_t>( bandCount );
          double v = data[i];
          if ( meanStd )
          {
            if ( meanArr )
              v -= meanArr[c];
            if ( stdArr && stdArr[c] > 0.0 )
              v /= stdArr[c];
          }
          v *= scale;
          data[i] = static_cast<float>( v );
        }
      }
    }

    // All-nodata core: skip the forward pass entirely (#705.4) — the output
    // is restored to NaN afterwards anyway, so the GPU/CPU pass is pure
    // waste. Only once the writer exists (the first real batch reveals the
    // output channel count); an entirely-nodata raster keeps the legacy
    // forward so the output geometry is still established.
    if ( writer && cv::countNonZero( invalidMask ) == invalidMask.total() )
    {
      nodataTiles.push_back( t );
      ++done;
      context.reportProgress( static_cast<double>( done ) / static_cast<double>( totalTiles ),
                              "Tiled inference: " + std::to_string( done ) + "/" + std::to_string( totalTiles ) );
      continue;
    }

    cv::Mat tileMat = hwc.clone(); // detached from the reused window buffer
    batchCores.push_back( t );
    if ( resizeToInput && ( winW != modelW || winH != modelH ) )
    {
      // cv::resize is limited to few channels; resample per band and merge.
      std::vector<cv::Mat> channels;
      cv::split( tileMat, channels );
      for ( auto &ch : channels )
      {
        cv::Mat resized;
        cv::resize( ch, resized, cv::Size( modelW, modelH ), 0, 0, interp );
        ch = resized;
      }
      cv::merge( channels, tileMat );
    }
    batchMats.push_back( std::move( tileMat ) );
    batchMasks.push_back( invalidMask.clone() ); // detached from the per-tile scratch mask
    batchFedSize.emplace_back( resizeToInput ? modelW : winW, resizeToInput ? modelH : winH );

    const bool batchFull = static_cast<int>( batchMats.size() ) >= batchSize || tileIndex == totalTiles - 1;
    if ( !batchFull )
      continue;

    // One forward pass per batch on the shared (cached) session.
    // Manual NCHW pack (#671): blobFromImage(s) asserts channels in
    // {1,3,4} — multispectral / SAR inference (2, >=5 channels) died with
    // a raw cv::Exception that also violates the engine's RSOperatorError
    // contract. The buffer already holds GDAL band order and manifest
    // band_roles expect channel i == file band i, so we keep that order
    // (swapRB=false equivalent) without delegating to the imread helper.
    // Also validate the band_roles arity up front so a manifest mismatch
    // surfaces as a typed error rather than a late tensor shape mismatch.
    if ( !m_model.input.bandRoles.empty()
         && static_cast<int>( m_model.input.bandRoles.size() ) != static_cast<int>( bandList.size() ) )
    {
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "model band_roles ("
                                 + std::to_string( m_model.input.bandRoles.size() )
                                 + ") does not match fed channel count (" + std::to_string( bandList.size() )
                                 + ")" );
    }
    cv::Mat blob;
    try {
      const int C = static_cast<int>( bandList.size() );
      const int B = static_cast<int>( batchMats.size() );
      const int H = batchMats.front().rows;
      const int W = batchMats.front().cols;
      // Single- and multi-channel tiles already share the same pack logic —
      // keeping one explicit path avoids a divergence between blobFromImage vs
      // blobFromImages shape conventions.
      int dims[4] = { B, C, H, W };
      blob = cv::Mat( 4, dims, CV_32F );
      blob.setTo( 0 );
      for ( int b = 0; b < B; ++b ) {
        std::vector<cv::Mat> channels;
        cv::split( batchMats[static_cast<size_t>( b )], channels );
        for ( int c = 0; c < C; ++c ) {
          const cv::Mat &ch = channels[static_cast<size_t>( c )];
          const int idx[3] = { b, c, 0 };
          float *dst = blob.ptr<float>( idx );
          for ( int y = 0; y < H; ++y ) {
            std::memcpy( dst + y * W, ch.ptr<float>( y ), static_cast<size_t>( W ) * sizeof( float ) );
          }
        }
      }
    } catch ( const RSOperatorError & ) {
      throw;
    } catch ( const std::exception &e ) {
      throw RSOperatorError( ErrorCode::OpenCvError, std::string( "failed to build inference blob: " ) + e.what() );
    } catch ( ... ) {
      throw RSOperatorError( ErrorCode::OpenCvError, "failed to build inference blob (unknown)" );
    }
    cv::Mat output;
    try
    {
      output = m_runtime->infer( blob );
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             std::string( "inference forward pass failed: " ) + e.what() );
    }
    if ( output.dims != 4 || output.size[0] != static_cast<int>( batchMats.size() ) )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "model output is not a 4-D NCHW batch matching the input tiles (got dims="
                               + std::to_string( output.dims ) + ", N=" + std::to_string( output.size[0] ) + ")" );
    const int outChannels = output.size[1];
    const int outH = output.size[2];
    const int outW = output.size[3];
    if ( outChannels <= 0 )
      throw RSOperatorError( ErrorCode::ComputationError, "model output has no channels" );

    // Depth check (#690): ArgMax / class-index heads emit int32/int64; the
    // per-pixel planes are later read as float — type-punning integer bits
    // into float32 silently corrupts. Fail or convert deterministically.
    if ( output.depth() != CV_32F ) {
      const int d = output.depth();
      const std::string dname = (d == CV_32S) ? "int32" : (d == CV_64F) ? "float64" : (d == CV_32S + 1) ? "int16" : std::to_string(d);
      if ( d == CV_32S || d == CV_64F || d == CV_16S || d == CV_8U ) {
        // Convert integer/float heads to CV_32F — preserves argmax class
        // indices (lossless for the int32/int64 range used by segmentation
        // class maps) and degrades gracefully for other float depths.
        cv::Mat converted;
        output.convertTo( converted, CV_32F );
        output = converted;
      } else {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model output dtype is " + dname
                                   + " — expected float32 (int32/int64 argmax heads were converted; other dtypes are unsupported: re-export the model with float outputs)" );
      }
    }

    if ( !writer )
    {
      writer = std::make_unique<GdalStreamingOutput>( QString::fromStdString( outputPath ),
                                                      rasterW, rasterH, outChannels, /*GDT_Float32*/ 6,
                                                      geoTransform, projection );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "failed to create output raster: " + outputPath );
      writer->setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
      stats.outBands = outChannels;
    }
    else if ( outChannels != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" + std::to_string( stats.outBands )
                               + " → " + std::to_string( outChannels ) + ")" );
    }

    const cv::Mat flat = output.reshape( 1, std::vector<int>{ static_cast<int>( batchMats.size() ) * outChannels,
                                                             outH * outW } );
    for ( std::size_t bi = 0; bi < batchMats.size(); ++bi )
    {
      const CoreTile &bt = batchCores[bi];
      const int fedW = batchFedSize[bi].first;
      const int fedH = batchFedSize[bi].second;
      for ( int c = 0; c < outChannels; ++c )
      {
        cv::Mat plane = flat.row( static_cast<int>( bi ) * outChannels + c )
                          .reshape( 1, outH )
                          .clone(); // (outH, outW) CV_32F
        // Map the output plane back onto the core tile. Grid-preserving
        // models (out == fed spatial size): CROP the halo margin away — the
        // core pixels are [halo, halo+core) so overlapping windows never
        // shift or duplicate output. Models that change the spatial dims
        // (strided heads): resample back to the core size.
        if ( outW == fedW && outH == fedH )
        {
          if ( resizeToInput )
          {
            // The window was resampled to the fixed model input before the
            // forward pass; scale the core rect accordingly.
            const double sxF = static_cast<double>( outW ) / std::max( 1, bt.w + 2 * halo );
            const double syF = static_cast<double>( outH ) / std::max( 1, bt.h + 2 * halo );
            int cx = static_cast<int>( std::lround( halo * sxF ) );
            int cy = static_cast<int>( std::lround( halo * syF ) );
            int cw = std::max( 1, static_cast<int>( std::lround( bt.w * sxF ) ) );
            int ch = std::max( 1, static_cast<int>( std::lround( bt.h * syF ) ) );
            cx = std::clamp( cx, 0, std::max( 0, outW - 1 ) );
            cy = std::clamp( cy, 0, std::max( 0, outH - 1 ) );
            cw = std::min( cw, outW - cx );
            ch = std::min( ch, outH - cy );
            plane = plane( cv::Range( cy, cy + ch ), cv::Range( cx, cx + cw ) ).clone();
            if ( plane.cols != bt.w || plane.rows != bt.h )
              cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, interp );
          }
          else if ( halo > 0 && outW == fedW && outH == fedH )
          {
            // Grid-preserving with halo: crop the halo border away.
            plane = plane( cv::Range( halo, halo + bt.h ), cv::Range( halo, halo + bt.w ) ).clone();
          }
        }
        else if ( outW != bt.w || outH != bt.h )
        {
          cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, interp );
        }
        if ( m_model.postprocess.maskThreshold >= 0.0 )
        {
          const float thr = static_cast<float>( m_model.postprocess.maskThreshold );
          cv::Mat mask = plane >= thr; // NaN ≥ thr is false → 0, restored below
          mask.convertTo( plane, CV_32F, 1.0 / 255.0 );
        }
        // Restore nodata on core pixels whose every input band was invalid.
        const cv::Mat &tileMask = batchMasks[bi];
        for ( int row = 0; row < bt.h; ++row )
        {
          const uchar *maskRow = tileMask.ptr<uchar>( row );
          float *outRow = plane.ptr<float>( row );
          for ( int col = 0; col < bt.w; ++col )
          {
            if ( maskRow[col] )
              outRow[col] = std::numeric_limits<float>::quiet_NaN();
          }
        }
        const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                               tileIndex, totalTiles };
        if ( !writer->writeTile( c + 1, writeTile, plane.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                 + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
      }
      ++done;
    }
    batchMats.clear();
    batchMasks.clear();
    batchFedSize.clear();
    batchCores.clear();

    context.reportProgress( static_cast<double>( done ) / static_cast<double>( totalTiles ),
                            "Tiled inference: " + std::to_string( done ) + "/" + std::to_string( totalTiles ) );
  }

    // Flush skipped all-nodata tiles as NaN now that the writer exists
    // (also runs for the trailing no-batch case).
    if ( writer && !nodataTiles.empty() )
    {
      std::vector<float> nanPlane;
      for ( const CoreTile &bt : nodataTiles )
      {
        nanPlane.assign( static_cast<std::size_t>( bt.w ) * bt.h,
                         std::numeric_limits<float>::quiet_NaN() );
        const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                               0, 1 };
        for ( int c = 0; c < stats.outBands; ++c )
          if ( !writer->writeTile( c + 1, writeTile, nanPlane.data() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "failed to write nodata tile at (" + std::to_string( bt.x ) + ", "
                                     + std::to_string( bt.y ) + ")" );
      }
      nodataTiles.clear();
    }
  }
  catch ( ... )
  {
    abandonOnFailure();
    throw;
  }

  QString writeError;
  if ( !writer || !writer->closeWithError( &writeError ) )
  {
    if ( writer )
      writer->removeOutput();
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to finalize output raster: " + writeError.toStdString() );
  }
  context.reportProgressForced( 1.0, "Tiled inference complete" );
  stats.tilesProcessed = done;
  return stats;
}

} // namespace sicnu::operators::runtime
