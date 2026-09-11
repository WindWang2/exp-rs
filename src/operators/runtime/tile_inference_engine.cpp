// src/operators/runtime/tile_inference_engine.cpp
#include "operators/runtime/tile_inference_engine.h"

#include "operators/framework/artifact_digest.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include "rs_classification_utils.h"

#include <gdal.h>
#include <ogr_spatialref.h>

#include <opencv2/imgproc.hpp>

#include <json/json.h>

#include <array>
#include <cstring>
#include <map>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

// --- Platform 8.0 provenance helpers ----------------------------------------

/// Compact CRS display string for payloads/sidecars: "EPSG:32633" when the
/// SRS carries an authority code, else a truncated WKT; "" for undeclared.
std::string crsDisplayName( const QString &wkt )
{
  if ( wkt.trimmed().isEmpty() )
    return {};
  OGRSpatialReference srs;
  if ( srs.SetFromUserInput( wkt.toUtf8().constData() ) == OGRERR_NONE )
  {
    const char *authority = srs.GetAuthorityName( nullptr );
    const char *code = srs.GetAuthorityCode( nullptr );
    if ( authority && code )
      return std::string( authority ) + ":" + code;
  }
  const std::string text = wkt.toStdString();
  if ( text.size() <= 64 )
    return text;
  // Truncate on a safe boundary: never split a UTF-8 multibyte sequence.
  std::size_t cut = 61;
  while ( cut > 0 && ( text[cut] & 0xC0 ) == 0x80 )
    --cut;
  return text.substr( 0, cut ) + "...";
}

/// Semantic CRS comparison through GDAL (geodetic authority for the
/// co-registration verdict — string equality of WKT would false-negative on
/// formatting). SetFromUserInput accepts WKT AND authority codes
/// ("EPSG:4326"), so callers may hand over either spelling.
/// Returns: 0 = same, 1 = different/unverifiable, -1 = at least one side
/// undeclared.
int compareCrs( const QString &primaryCrs, const QString &otherCrs )
{
  const bool primaryEmpty = primaryCrs.trimmed().isEmpty();
  const bool otherEmpty = otherCrs.trimmed().isEmpty();
  if ( primaryEmpty && otherEmpty )
    return 0; // nothing declared on either side — nothing to disagree about
  if ( primaryEmpty || otherEmpty )
    return -1;
  OGRSpatialReference primary;
  OGRSpatialReference other;
  if ( primary.SetFromUserInput( primaryCrs.toUtf8().constData() ) != OGRERR_NONE
       || other.SetFromUserInput( otherCrs.toUtf8().constData() ) != OGRERR_NONE )
    return 1; // unparsable declarations cannot be verified equal
  return primary.IsSame( &other ) ? 0 : 1;
}

/// Publishes the provenance sidecar next to a successfully published raster.
/// Same-directory staged write + rename. The CALLER removes any previous
/// sidecar BEFORE the product rename, so the on-disk states possible across
/// a crash are: product+matching sidecar (full success), product without
/// sidecar (crash before the sidecar rename — detectable absence, never a
/// stale mismatched one), or the previous product untouched. There is no
/// consumer-side detection; the next successful run rewrites both.
bool publishProvenanceSidecar( const QString &finalPath, const std::string &outputPath,
                               const Json::Value &provenance, std::string *error )
{
  const QString sidecarPath = finalPath + QStringLiteral( ".prov.json" );
  const QString stagePath = sidecarPath + QStringLiteral( ".stage~" );
  QFile stage( stagePath );
  if ( !stage.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
  {
    if ( error )
      *error = "failed to stage the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  const Json::Value formatted = provenance;
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  const std::string text = Json::writeString( builder, formatted );
  stage.write( text.data(), static_cast<qint64>( text.size() ) );
  stage.close();
  if ( stage.error() != QFileDevice::NoError )
  {
    stage.remove();
    if ( error )
      *error = "failed to write the provenance sidecar: " + stagePath.toStdString();
    return false;
  }
  QFile::remove( sidecarPath ); // Windows rename does not overwrite (stage leftovers)
  if ( !QFile::rename( stagePath, sidecarPath ) )
  {
    stage.remove();
    if ( error )
      *error = "failed to publish the provenance sidecar: " + sidecarPath.toStdString();
    return false;
  }
  ( void )outputPath;
  return true;
}

/// Builds the shared provenance document body for one engine run (everything
/// the caller needs to reproduce the run's semantics). Fields the run cannot
/// know stay absent — truthful provenance, never placeholders.
Json::Value buildProvenanceDocument( const ModelInfo &model, const ModelRuntimePtr &runtime,
                                     const TileInferenceStats &stats,
                                     const std::string &outputModeNote )
{
  Json::Value prov( Json::objectValue );
  prov["schema"] = "exp-rs-prov/1";

  Json::Value modelJson( Json::objectValue );
  modelJson["name"] = model.name;
  if ( !model.id.empty() )
    modelJson["id"] = model.id;
  if ( !model.modelVersion.empty() )
    modelJson["version"] = model.modelVersion;
  modelJson["identity_tag"] = model.identityTag();
  if ( !model.contentDigest.empty() )
    modelJson["content_digest"] = model.contentDigest;
  // Platform 9.0 (M7/M8): the whole-package digest when the model ships one.
  if ( !model.packageDigest.empty() )
    modelJson["package_digest"] = model.packageDigest;
  modelJson["framework"] = model.framework;
  if ( !model.sourceManifest.empty() )
    modelJson["source_manifest"] = model.sourceManifest;
  if ( !model.license.empty() )
    modelJson["license"] = model.license;
  prov["model"] = modelJson;

  Json::Value execution( Json::objectValue );
  if ( runtime )
  {
    execution["backend"] = runtime->backendName();
    execution["device"] = runtime->deviceName();
    // Platform 9.0 (M8) execution identity: the execution provider that was
    // engaged and the backend library version — reproducibility can name the
    // exact software that ran, honestly ("" stays absent).
    const ProviderRuntimeDetails details = runtime->providerDetails();
    if ( !details.executionProvider.empty() )
      execution["execution_provider"] = details.executionProvider;
    if ( !details.runtimeVersion.empty() )
      execution["runtime_version"] = details.runtimeVersion;
  }
  execution["tile_size"] = stats.tileSize;
  execution["halo"] = stats.halo;
  execution["batch_size"] = stats.batchSize;
  execution["tiles_planned"] = stats.tilesPlanned;
  execution["tiles_processed"] = stats.tilesProcessed;
  execution["tiles_skipped_nodata"] = stats.tilesSkippedNoData;
  if ( stats.batchReductions > 0 )
    execution["batch_reductions"] = stats.batchReductions;
  if ( !outputModeNote.empty() )
    execution["output_mode"] = outputModeNote;
  prov["execution"] = execution;

  Json::Value inputs( Json::arrayValue );
  for ( const GridProvenance &grid : stats.inputGrids )
  {
    Json::Value input( Json::objectValue );
    input["name"] = grid.name;
    input["path"] = grid.path;
    if ( !grid.preparedFrom.empty() )
    {
      Json::Value prepared( Json::arrayValue );
      for ( const std::string &origin : grid.preparedFrom )
        prepared.append( origin );
      input["prepared_from"] = prepared;
    }
    if ( !grid.crs.empty() )
      input["crs"] = grid.crs;
    input["crs_verified"] = grid.crsVerified;
    input["width"] = grid.width;
    input["height"] = grid.height;
    if ( grid.frames > 1 )
      input["frames"] = grid.frames;
    // Platform 9.0 (M3): effective preprocess + identity fingerprint — the
    // reproduction record states what preprocessed this feed and what the
    // fed raster was fingerprinted as.
    if ( !grid.preprocessNote.empty() )
      input["preprocess"] = grid.preprocessNote;
    if ( grid.fingerprint.isObject() )
      input["fingerprint"] = grid.fingerprint;
    inputs.append( input );
  }
  prov["inputs"] = inputs;

  Json::Value output( Json::objectValue );
  output["bands"] = stats.outBands;
  output["width"] = stats.outWidth;
  output["height"] = stats.outHeight;
  Json::Value heads( Json::arrayValue );
  for ( int channels : stats.headChannels )
    heads.append( channels );
  if ( !stats.headChannels.empty() )
    output["head_channels"] = heads;
  // Platform 9.0 (M6): per-product-class pixel metadata for Labels/Mask.
  if ( !stats.classPixelCounts.empty() )
  {
    Json::Value counts( Json::arrayValue );
    for ( long long pixels : stats.classPixelCounts )
      counts.append( static_cast<Json::Int64>( pixels ) );
    output["class_pixel_counts"] = counts;
    if ( !model.output.classes.empty() )
    {
      Json::Value names( Json::arrayValue );
      for ( const std::string &cls : model.output.classes )
        names.append( cls );
      output["classes"] = names;
    }
  }
  prov["output"] = output;

  prov["created_utc"] =
    QDateTime::currentDateTimeUtc().toString( Qt::ISODateWithMs ).toStdString();
  return prov;
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

std::string TileInferenceEngine::inputDTypeMismatch( const ModelInfo &model,
                                                     const std::vector<int> &bands,
                                                     const std::function<int( int )> &bandDataType )
{
  if ( model.input.dtype.empty() || bands.empty() )
    return {};
  static const std::map<std::string, int> kAccepted = {
    { "float32", GDT_Float32 }, { "float64", GDT_Float64 },
    { "float16", GDT_Float32 }, { "uint16", GDT_UInt16 },
    { "int16", GDT_Int16 },     { "uint8", GDT_Byte },
    { "int32", GDT_Int32 },     { "uint32", GDT_UInt32 },
  };
  const auto accepted = kAccepted.find( model.input.dtype );
  if ( accepted == kAccepted.end() )
    return "model manifest declares unsupported input dtype '" + model.input.dtype + "'";
  for ( int band : bands )
  {
    const int rasterType = bandDataType( band );
    if ( rasterType != accepted->second )
      return "model manifest requires input dtype '" + model.input.dtype
               + "' but the raster band " + std::to_string( band ) + " has GDAL type "
               + std::to_string( rasterType )
               + " (convert the raster or update the manifest)";
  }
  return {};
}

std::string TileInferenceEngine::missingOutputTensor( const ModelInfo &model,
                                                      const std::vector<std::string> &graphOutputNames )
{
  if ( model.output.tensorNames.empty() )
    return {};
  if ( graphOutputNames.empty() )
    return {}; // runtime cannot enumerate outputs — contract stays advisory
  for ( const auto &declared : model.output.tensorNames )
  {
    if ( std::find( graphOutputNames.begin(), graphOutputNames.end(), declared )
         == graphOutputNames.end() )
    {
      std::string available;
      for ( const auto &name : graphOutputNames )
        available += ( available.empty() ? "" : ", " ) + name;
      return "model manifest declares output tensor '" + declared
               + "' but the loaded graph provides only: " + available;
    }
  }
  return {};
}

std::string TileInferenceEngine::outputTypeMismatch( int outputCvType, const std::string &tensorName )
{
  if ( outputCvType == CV_32F )
    return {};
  // Mnemonic for the common depths keeps the message readable; quantized
  // (8U/16U) heads are rejected until a dequantizing write path exists (#690).
  const char *depthName = nullptr;
  switch ( outputCvType )
  {
    case CV_8U: depthName = "CV_8U"; break;
    case CV_8S: depthName = "CV_8S"; break;
    case CV_16U: depthName = "CV_16U"; break;
    case CV_16S: depthName = "CV_16S"; break;
    case CV_32S: depthName = "CV_32S"; break;
    case CV_64F: depthName = "CV_64F"; break;
    case CV_16F: depthName = "CV_16F"; break;
    default: break; // the numeric type is authoritative for exotic combos
  }
  std::string actual = std::to_string( outputCvType );
  if ( depthName )
    actual += std::string( " (" ) + depthName + ")";
  return "model output tensor '" + ( tensorName.empty() ? std::string( "<default>" ) : tensorName )
           + "' has OpenCV type " + actual
           + " but the raster writer emits float32: expected CV_32F (type 5)";
}

std::string TileInferenceEngine::classesChannelMismatch( const ModelInfo &model, int outputChannels,
                                                         const std::string &tensorName )
{
  const std::size_t declared = model.output.classes.size();
  if ( declared == 0 || outputChannels == static_cast<int>( declared ) )
    return {};
  return "model output tensor '" + ( tensorName.empty() ? std::string( "<default>" ) : tensorName )
           + "' has " + std::to_string( outputChannels ) + " channel(s) but the manifest declares "
           + std::to_string( declared )
           + " classes (the raster head writes one channel per class)";
}

bool TileInferenceEngine::batchIsAllNoData( const std::vector<int> &validPixelCounts )
{
  if ( validPixelCounts.empty() )
    return false;
  for ( int valid : validPixelCounts )
  {
    if ( valid > 0 )
      return false;
  }
  return true;
}

Json::Value TileInferenceEngine::feedFingerprint( const std::string &path,
                                                  const std::vector<int> &bands,
                                                  std::int64_t contentMaxBytes )
{
  GdalDatasetWrapper ds;
  if ( !ds.open( QString::fromStdString( path ) ) )
    throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + path );

  Json::Value fp( Json::objectValue );
  fp["path"] = path;
  fp["width"] = ds.width();
  fp["height"] = ds.height();
  fp["band_count"] = ds.bandCount();
  Json::Value dtypes( Json::arrayValue );
  for ( int b = 1; b <= ds.bandCount(); ++b )
    dtypes.append( GDALGetDataTypeName(
      static_cast<GDALDataType>( ds.bandDataType( b ) ) ) );
  fp["band_dtypes"] = dtypes;
  const std::array<double, 6> gt = ds.geoTransform();
  Json::Value gtJson( Json::arrayValue );
  for ( int i = 0; i < 6; ++i )
    gtJson.append( gt[static_cast<std::size_t>( i )] );
  fp["geotransform"] = gtJson;
  const QString projection = ds.projection();
  if ( !projection.trimmed().isEmpty() )
    fp["crs"] = crsDisplayName( projection );
  Json::Value selected( Json::arrayValue );
  for ( int band : bands )
    selected.append( band );
  fp["selected_bands"] = selected;

  // Content identity: a real digest when the file fits the bound; an honest
  // size+mtime marker when it does not (never a digest-shaped lie).
  const QFileInfo info( QString::fromStdString( path ) );
  const qint64 bytes = info.size();
  Json::Value content( Json::objectValue );
  content["bytes"] = static_cast<Json::Int64>( bytes );
  if ( bytes >= 0 && bytes <= contentMaxBytes )
  {
    content["sha256"] = sicnu::operators::artifactSha256Hex( path );
  }
  else
  {
    content["reason"] = "file-too-large";
    content["mtime_utc"] =
      info.lastModified().toUTC().toString( Qt::ISODateWithMs ).toStdString();
  }
  fp["content"] = content;
  return fp;
}

// --- Platform 9.0 (M5): feather-weighted tile blending -----------------------

/// Cosine-feather weight for one output pixel of a tile window: 1.0 inside
/// the core, ramping from 1 (core edge) to 0 (window edge) across the halo
/// with a cosine profile. @p distance is how far the pixel lies beyond the
/// core edge in px; @p halo is the ramp width (<=0 → everything is core).
float featherWeight( int distance, int halo )
{
  if ( distance <= 0 )
    return 1.0f;
  if ( distance >= halo )
    return 0.0f;
  const double r = static_cast<double>( distance ) / static_cast<double>( halo );
  return static_cast<float>( 0.5 * ( 1.0 - std::cos( M_PI * r ) ) );
}

/**
 * Feather-weighted tile accumulator (Platform 9.0 M5). The historical stitch
 * crops every tile's halo and writes hard core edges — visible seams where
 * neighboring tiles disagree. With blending, the tile's FULL window
 * prediction (core + halo, i.e. exactly what the model saw) contributes a
 * weighted average: weight 1 in the core, cosine ramp across the halo.
 * Final pixel = Σ(w·v)/Σw over every tile that saw it (NaN predictions are
 * skipped — weight 0 — and a pixel no tile predicted stays NoData).
 *
 * Memory is bounded by a sliding row window of (tile + 2·halo) rows: rows are
 * final once every tile whose halo could reach them has been added, and tiles
 * arrive in row-major order, so rows leave the window monotonically. The
 * accumulator never holds the whole raster.
 */
class FeatherAccumulator
{
  public:
    FeatherAccumulator( int rasterW, int rasterH, int slotCount, int tileSize, int halo,
                        RasterOutputMode mode, float maskThreshold,
                        const std::vector<int> &classMapping, float writeNoData )
        : m_rasterW( rasterW ), m_rasterH( rasterH ), m_slots( std::max( 1, slotCount ) ),
          m_span( std::clamp( tileSize + 2 * std::max( 0, halo ), 1, rasterH ) ),
          m_mode( mode ), m_maskThreshold( maskThreshold ), m_classMapping( classMapping ),
          m_writeNoData( writeNoData )
    {
      resetWindow( 0 );
    }

    int windowTop() const { return m_top; }

    /// Adds one full-window plane (fed-size, core+halo) for one slot. The
    /// window must already cover the tile's affected rows — the engine calls
    /// flushThrough( tile.y - halo ) BEFORE adding, per tile, in row-major
    /// order (rows below tile.y - halo can receive no further contributions).
    void add( int slot, const cv::Mat &windowPlane, const CoreTile &core, int halo,
              int windowW, int windowH )
    {
      CV_Assert( slot >= 0 && slot < m_slots );
      // The window prediction may overhang the raster; clip to it.
      const int x0 = std::max( 0, core.x - halo );
      const int y0 = std::max( 0, core.y - halo );
      const int x1 = std::min( m_rasterW, core.x + core.w + halo );
      const int y1 = std::min( m_rasterH, core.y + core.h + halo );
      if ( x0 >= x1 || y0 >= y1 )
        return;
      cv::Mat sum = m_sum[static_cast<std::size_t>( slot )];
      for ( int y = y0; y < y1; ++y )
      {
        const int wy = y - ( core.y - halo ); // window-plane row
        if ( wy < 0 || wy >= windowH )
          continue;
        const float *srcRow = windowPlane.ptr<float>( wy );
        float *sumRow = sum.ptr<float>( y - m_top );
        float *weightRow = m_weight.ptr<float>( y - m_top );
        for ( int x = x0; x < x1; ++x )
        {
          const int wx = x - ( core.x - halo );
          if ( wx < 0 || wx >= windowW )
            continue;
          const float v = srcRow[wx];
          if ( !std::isfinite( v ) )
            continue; // nodata prediction: weight 0, never poisons the average
          // Weight: distance from the CORE rect (per axis), cosine ramp.
          const int dx = std::max( { core.x - x, x - ( core.x + core.w - 1 ), 0 } );
          const int dy = std::max( { core.y - y, y - ( core.y + core.h - 1 ), 0 } );
          const float w = featherWeight( dx, halo ) * featherWeight( dy, halo );
          sumRow[x] += v * w;
          weightRow[x] += w;
        }
      }
    }

    /// Finalizes rows [m_top, rowExclusive) through the writer.
    void flushThrough( int rowExclusive, GdalStreamingOutput &writer )
    {
      const int last = std::min( rowExclusive, m_rasterH );
      if ( last <= m_top )
        return;
      finalizeRows( m_top, last, writer );
      // Clear the finalized rows' accumulators (they are reusable window rows).
      const int cleared = std::min( last - m_top, m_span );
      for ( int s = 0; s < m_slots; ++s )
        m_sum[static_cast<std::size_t>( s )]
         ( cv::Range( 0, cleared ), cv::Range::all() ) = 0.0f;
      m_weight( cv::Range( 0, cleared ), cv::Range::all() ) = 0.0f;
      m_top = last;
    }

    /// Finalizes everything remaining (end of run).
    void finish( GdalStreamingOutput &writer ) { flushThrough( m_rasterH, writer ); }

  private:
    void resetWindow( int top )
    {
      m_top = top;
      m_sum.assign( static_cast<std::size_t>( m_slots ),
                    cv::Mat::zeros( m_span, m_rasterW, CV_32FC1 ) );
      m_weight = cv::Mat::zeros( m_span, m_rasterW, CV_32FC1 );
    }

    /// Writes rows [from, to): Probability mode writes every slot as its own
    /// band; derived modes collapse the class slots into ONE band (argmax /
    /// threshold), the class blend happening BEFORE the collapse.
    void finalizeRows( int from, int to, GdalStreamingOutput &writer )
    {
      const int rows = to - from;
      const GdalBlockStream::Tile writeTile{ 0, from, m_rasterW, rows, 0, m_rasterW, rows, 0, 1 };
      if ( m_mode == RasterOutputMode::Probability )
      {
        cv::Mat out( rows, m_rasterW, CV_32FC1 );
        for ( int s = 0; s < m_slots; ++s )
        {
          for ( int r = 0; r < rows; ++r )
          {
            const float *sumRow = m_sum[static_cast<std::size_t>( s )].ptr<float>( r );
            const float *weightRow = m_weight.ptr<float>( r );
            float *outRow = out.ptr<float>( r );
            for ( int x = 0; x < m_rasterW; ++x )
              outRow[x] = weightRow[x] > 0.0f ? sumRow[x] / weightRow[x] : m_writeNoData;
          }
          if ( !writer.writeTile( s + 1, writeTile, out.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "failed to write blended output rows" );
        }
        return;
      }
      // Derived modes: the class slots are [0, m_classCount); slot
      // m_classCount (when present) is the blended uncertainty band.
      const int classes = m_classCount > 0 ? m_classCount : m_slots;
      const bool hasUncertainty = m_slots > classes;
      cv::Mat derived( rows, m_rasterW, CV_32FC1 );
      for ( int r = 0; r < rows; ++r )
      {
        const float *weightRow = m_weight.ptr<float>( r );
        float *outRow = derived.ptr<float>( r );
        for ( int x = 0; x < m_rasterW; ++x )
        {
          if ( weightRow[x] <= 0.0f )
          {
            outRow[x] = m_writeNoData;
            continue;
          }
          int best = 0;
          float bestv = -std::numeric_limits<float>::infinity();
          bool invalid = false;
          for ( int c = 0; c < classes; ++c )
          {
            const float v = m_sum[static_cast<std::size_t>( c )].ptr<float>( r )[x]
                            / weightRow[x];
            if ( !std::isfinite( v ) )
            {
              invalid = true;
              break;
            }
            if ( v > bestv )
            {
              bestv = v;
              best = c;
            }
          }
          if ( invalid )
          {
            outRow[x] = m_writeNoData;
            continue;
          }
          switch ( m_mode )
          {
            case RasterOutputMode::Labels:
            {
              const int productClass = ( static_cast<std::size_t>( best ) < m_classMapping.size() )
                                         ? m_classMapping[static_cast<std::size_t>( best )]
                                         : best;
              outRow[x] = static_cast<float>( productClass );
              if ( m_classTally && productClass >= 0
                   && static_cast<std::size_t>( productClass ) < m_classTally->size() )
                ( *m_classTally )[static_cast<std::size_t>( productClass )]++;
              break;
            }
            case RasterOutputMode::Mask:
            {
              const float maskValue = ( classes == 1 )
                                        ? ( bestv >= m_maskThreshold ? 1.0f : 0.0f )
                                        : ( best != 0 ? 1.0f : 0.0f );
              outRow[x] = maskValue;
              if ( m_classTally && static_cast<std::size_t>( maskValue ) < m_classTally->size() )
                ( *m_classTally )[static_cast<std::size_t>( maskValue )]++;
              break;
            }
            case RasterOutputMode::Confidence:
              outRow[x] = bestv;
              break;
            default:
              outRow[x] = m_writeNoData;
              break;
          }
        }
      }
      GdalBlockStream::Tile bandTile = writeTile;
      if ( !writer.writeTile( 1, bandTile, derived.ptr<float>() ) )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "failed to write blended output rows" );
      if ( hasUncertainty )
      {
        cv::Mat unc( rows, m_rasterW, CV_32FC1 );
        for ( int r = 0; r < rows; ++r )
        {
          const float *sumRow = m_sum[static_cast<std::size_t>( classes )].ptr<float>( r );
          const float *weightRow = m_weight.ptr<float>( r );
          float *outRow = unc.ptr<float>( r );
          for ( int x = 0; x < m_rasterW; ++x )
            outRow[x] = weightRow[x] > 0.0f ? sumRow[x] / weightRow[x] : m_writeNoData;
        }
        if ( !writer.writeTile( 2, bandTile, unc.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable,
                                 "failed to write blended uncertainty rows" );
      }
    }

    int m_rasterW;
    int m_rasterH;
    int m_slots;
    int m_span;      // window height in rows (tile + 2·halo, clamped to raster)
    int m_top = 0;   // first raster row held by the window
    std::vector<cv::Mat> m_sum;
    cv::Mat m_weight;
    RasterOutputMode m_mode;
    float m_maskThreshold = 0.5f;
    std::vector<int> m_classMapping;
    float m_writeNoData = std::numeric_limits<float>::quiet_NaN();
    int m_classCount = 0; // derived modes: how many of the slots are head-0 classes
    std::vector<long long> *m_classTally = nullptr; // Platform 9.0 (M6), optional

  public:
    /// Declared by the engine after construction: how many of the slots are
    /// CLASS planes of head 0 (derived modes), enabling the blended collapse.
    void setClassCount( int classes ) { m_classCount = classes; }
    /// Platform 9.0 (M6): shared per-product-class tally (engine-owned,
    /// lives for the whole run).
    void setClassTally( std::vector<long long> *tally ) { m_classTally = tally; }
};

TileInferenceStats TileInferenceEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context )
{
  return run( inputPath, bands, outputPath, context, TileInferenceRunOptions{} );
}

TileInferenceStats TileInferenceEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context,
                                             const TileInferenceRunOptions &options )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "tile inference engine has no runtime session" );

  // Platform 7.0 preprocess additions are executed by the multimodal engine;
  // the single-input engine refuses them loudly instead of running identity.
  if ( m_model.preprocess.pad > 0 || !std::isnan( m_model.preprocess.clampMin )
       || !std::isnan( m_model.preprocess.clampMax ) )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "preprocess.pad / clamp_min / clamp_max are executed by the "
                             "multi-input engine (runMultiInput); this single-input model must "
                             "drop them from the manifest to run here" );

  // Manifest output.tensor_names contract (#705): every declared name must
  // exist in the loaded graph; the first declared name selects the head the
  // engine consumes. Skipped when the runtime cannot enumerate its outputs.
  // Platform 3.0 multi-head: when the runtime can enumerate its outputs, ALL
  // declared tensor names are consumed and their channels stacked in manifest
  // order. Without enumeration the contract stays advisory and only the first
  // declared name (or the default head) is used — the historical behavior.
  std::vector<std::string> headNames;
  if ( !m_model.output.tensorNames.empty() )
  {
    if ( const std::string missing = missingOutputTensor( m_model, m_runtime->outputTensorNames() );
         !missing.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData, missing );
    if ( m_runtime->outputTensorNames().empty() )
      headNames = { m_model.output.tensorNames.front() };
    else
      headNames = m_model.output.tensorNames;
  }
  else
  {
    headNames = { std::string() };
  }
  const std::string uncertainty = uncertaintyMethod( m_model );

  // Platform 4.0 derived output modes. Conflicts fail loudly BEFORE any
  // compute: derived modes collapse head 0's class planes, so multi-head
  // models and uncertainty bands (which need the full class stack on disk)
  // refuse them.
  const RasterOutputMode mode = options.outputMode != RasterOutputMode::Probability
                                  ? options.outputMode
                                  : rasterOutputMode( m_model );
  int writeBands = 0;      // resolved when the writer is created
  int writeType = 6;       // GDT_Float32; Labels/Mask write Byte/UInt16
  float writeNoData = std::numeric_limits<float>::quiet_NaN();
  if ( mode != RasterOutputMode::Probability )
  {
    if ( !uncertainty.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "output.uncertainty requires the full probability stack — "
                             "remove output.format or output.uncertainty" );
    if ( m_model.output.tensorNames.size() > 1 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "derived output.format applies to single-head models; this "
                             "manifest declares " + std::to_string( m_model.output.tensorNames.size() )
                               + " tensor heads" );
    if ( mode == RasterOutputMode::Labels && m_model.postprocess.maskThreshold >= 0.0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "postprocess.mask_threshold is meaningless with output.format=labels "
                             "(argmax never thresholds) — remove one of the two" );
    switch ( mode )
    {
      case RasterOutputMode::Labels:
      {
        const int classCount = static_cast<int>( m_model.output.classes.size() );
        writeBands = 1;
        writeType = classCount <= 255 ? /*GDT_Byte*/ 1 : /*GDT_UInt16*/ 2;
        writeNoData = classCount <= 255 ? 255.0f : 65535.0f;
        break;
      }
      case RasterOutputMode::Mask:
        writeBands = 1;
        writeType = /*GDT_Byte*/ 1;
        writeNoData = 255.0f;
        break;
      case RasterOutputMode::Confidence:
        writeBands = 1;
        writeType = /*GDT_Float32*/ 6;
        writeNoData = std::numeric_limits<float>::quiet_NaN();
        break;
      case RasterOutputMode::Probability:
        break; // unreachable: handled above
    }
  }

  GdalDatasetWrapper ds;
  if ( !ds.open( QString::fromStdString( inputPath ) ) )
    throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + inputPath );
  const int rasterW = ds.width();
  const int rasterH = ds.height();
  const int rasterBands = ds.bandCount();
  if ( rasterW <= 0 || rasterH <= 0 || rasterBands <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + inputPath );

  std::vector<int> bandList = bands;
  if ( bandList.empty() )
  {
    bandList.resize( rasterBands );
    for ( int i = 0; i < rasterBands; ++i )
      bandList[static_cast<std::size_t>( i )] = i + 1;
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

  // Manifest dtype contract (#632/#705): input.dtype must match the raster's
  // actual GDAL type for EVERY band fed to the model (the engine always reads
  // float32; a mismatched dtype silently misnormalizes). Checking band 1
  // alone let mixed-type rasters pass validation and fail only mid-read.
  if ( const std::string dtypeError =
         inputDTypeMismatch( m_model, bandList,
                             [ &ds ]( int band ) { return ds.bandDataType( band ); } );
       !dtypeError.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData, dtypeError );

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
  int batchSize = effectiveBatchSize( m_model, ModelRuntimeRegistry::instance().hardware(),
                                      tileSize, bandCount );
  // batchCap is a CAP, never an upgrade: it can only lower the effective
  // batch (memory-pinned runs), keeping the manifest/budget verdict as the
  // upper bound that estimateExecution also admitted on.
  if ( options.batchSizeOverride > 0 )
    batchSize = std::min( batchSize, options.batchSizeOverride );
  batchSize = std::max( 1, batchSize );
  const bool resizeToInput = pre.resize == "to_input" && m_model.input.width > 0 && m_model.input.height > 0;
  const int modelW = resizeToInput ? m_model.input.width : 0;
  const int modelH = resizeToInput ? m_model.input.height : 0;
  const int interp = pre.interpolation == "nearest" ? cv::INTER_NEAREST : cv::INTER_LINEAR;

  // Platform 9.0 (M5): tile output blending. options.blend (Unset) defers to
  // the manifest's tiling.blend (default none = the historical hard-edge
  // stitch). Feather blending requires a halo — the whole point is averaging
  // the overlap zone — and the grid-preserving head geometry (per head below);
  // anything else is a typed refusal, never a silently ignored knob.
  TileBlend blend = options.blend;
  if ( blend == TileBlend::Unset )
    blend = m_model.tiling.blend == "feather" ? TileBlend::Feather : TileBlend::None;
  if ( blend == TileBlend::Feather && halo <= 0 )
    throw RSOperatorError(
      ErrorCode::InvalidParameter,
      "tiling.blend=feather requires a halo (tiling.halo or tiling.overlap) — "
        "overlapping windows are what gets averaged; refusing instead of "
        "blending nothing" );

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

  // Platform 8.0 grid provenance: the single input's verified grid identity
  // (self-consistent by construction; recorded for payload + sidecar).
  {
    GridProvenance grid;
    grid.name = m_model.input.name.empty() ? "input" : m_model.input.name;
    grid.path = inputPath;
    grid.crs = crsDisplayName( ds.projection() );
    grid.crsVerified = !grid.crs.empty();
    grid.width = rasterW;
    grid.height = rasterH;
    stats.inputGrids.push_back( std::move( grid ) );
  }

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
  std::vector<int> batchValidPixels;
  // Core tiles of batches whose every pixel is nodata: their forward pass is
  // skipped and NoData is written directly once the writer exists (#705).
  std::vector<CoreTile> deferredNoData;

  std::unique_ptr<GdalStreamingOutput> writer;
  // Platform 9.0 (M5): the feather-blending accumulator (created with the
  // writer; owns all output writing while active).
  std::unique_ptr<FeatherAccumulator> accumulator;
  // Staging path for the atomic publish (assigned when the writer is created;
  // empty until then — the publish step runs only on full success).
  QString stagePath;
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

  int done = 0;    // tiles fully written (forwarded or NoData-flushed)
  int skipped = 0; // tiles deferred as all-nodata, not yet written

  // Writes NoData directly for tiles whose forward pass was skipped (#705):
  // every core pixel of these tiles is invalid, so every output channel is
  // the writer's NoData value — the same NaN the mask restore writes for
  // partially invalid tiles.
  auto flushDeferredNoData = [ & ]( int currentTileIndex )
  {
    // Platform 9.0 (M5): under feather blending the accumulator owns every
    // written pixel; an all-nodata tile contributes nothing and its pixels
    // end as NoData wherever no neighbor covered them (finish() normalizes).
    if ( !writer || deferredNoData.empty() || blend == TileBlend::Feather )
      return;
    for ( const CoreTile &dt : deferredNoData )
    {
      // NoData fill uses the WRITER's sentinel (NaN for float stacks, 255/65535
      // for derived label/mask rasters — a NaN bit-cast into Byte is garbage).
      cv::Mat nodataPlane( dt.h, dt.w, CV_32FC1, cv::Scalar( writeNoData ) );
      for ( int c = 0; c < stats.outBands; ++c )
      {
        const GdalBlockStream::Tile writeTile{ dt.x, dt.y, dt.w, dt.h, 0, dt.w, dt.h,
                                               currentTileIndex, totalTiles };
        if ( !writer->writeTile( c + 1, writeTile, nodataPlane.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable,
                                 "failed to write output tile at ("
                                   + std::to_string( dt.x ) + ", " + std::to_string( dt.y ) + ")" );
      }
      ++done;
      --skipped;
    }
    deferredNoData.clear();
  };

  // One forward pass + streaming write for the tiles accumulated in the
  // batch buffers. Runs for regular batches and for the all-nodata probe.
  auto flushBatch = [ & ]( int currentTileIndex )
  {
    // One forward pass per batch on the shared (cached) session.
    // Manual NCHW pack (#671): blobFromImage(s) asserts channels in
    // {1,3,4} — multispectral / SAR inference (2, >=5 channels) died with
    // a raw cv::Exception that also violates the engine's RSOperatorError
    // contract. The buffer already holds GDAL band order and manifest
    // band_roles expect channel i == file band i, so we keep that order
    // (swapRB=false equivalent) without delegating to the imread helper.
    // The band_roles arity contract is validated up front in run().
    cv::Mat blob;
    try
    {
      const int C = bandCount;
      const int B = static_cast<int>( batchMats.size() );
      const int H = batchMats.front().rows;
      const int W = batchMats.front().cols;
      // blobFromImages asserted same-size batches (a mixed batch threw);
      // edge tiles make that reachable whenever raster dims are not a
      // multiple of the tile size and resizeToInput is off — keep the check
      // LOUD instead of silently packing garbage.
      for ( const cv::Mat &m : batchMats )
      {
        if ( m.rows != H || m.cols != W )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "mixed tile sizes in one inference batch (" + std::to_string( m.cols ) + "x"
                                   + std::to_string( m.rows ) + " vs " + std::to_string( W ) + "x"
                                   + std::to_string( H ) + ") - enable resize:to_input or align the raster" );
      }
      int dims[4] = { B, C, H, W };
      blob = cv::Mat( 4, dims, CV_32F );
      blob.setTo( 0 );
      for ( int b = 0; b < B; ++b )
      {
        std::vector<cv::Mat> channels;
        cv::split( batchMats[static_cast<std::size_t>( b )], channels );
        for ( int c = 0; c < C; ++c )
        {
          const cv::Mat &ch = channels[static_cast<std::size_t>( c )];
          // ptr(b, c, 0) is the 3-arg overload (data + b*step0 + c*step1):
          // the const int* overload reads idx[dims] (a 4th, garbage element)
          // on a 4-D Mat — an OOB stack read whose damage depends on the
          // stack garbage.
          float *dst = blob.ptr<float>( b, c, 0 );
          for ( int y = 0; y < H; ++y )
          {
            std::memcpy( dst + static_cast<std::size_t>( y ) * W, ch.ptr<float>( y ),
                         static_cast<std::size_t>( W ) * sizeof( float ) );
          }
        }
      }
    }
    catch ( const RSOperatorError & )
    {
      throw;
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::OpenCvError,
                             std::string( "failed to build inference blob (bands fed: " )
                               + std::to_string( bandCount ) + "): " + e.what() );
    }
    catch ( ... )
    {
      throw RSOperatorError( ErrorCode::OpenCvError, "failed to build inference blob (unknown)" );
    }
    // TTA helper: average the head logits over horizontal/vertical flips so
    // the result stays on the original orientation (Platform 3.0 goal §10;
    // opt-in via run options — compute doubles, honest default off).
    auto forwardHead = [ & ]( const std::string &headName ) -> cv::Mat {
      auto forwardOnce = [ & ]( const cv::Mat &b ) -> cv::Mat {
        try
        {
          return headName.empty() ? m_runtime->infer( b ) : m_runtime->infer( b, headName );
        }
        catch ( const RSOperatorError & )
        {
          throw;
        }
        catch ( const std::exception &e )
        {
          throw RSOperatorError( ErrorCode::ComputationError,
                                 std::string( "inference forward pass failed: " ) + e.what() );
        }
      };
      cv::Mat output = forwardOnce( blob );
      if ( options.tta == TtaMode::None )
        return output;

      // cv::flipND needs OpenCV 4.5+; CI images may lack it. For NCHW 4-D
      // tensors, flip H (axis 2) / W (axis 3) via per-(n,c) 2-D cv::flip.
      auto flipNCHWAxis = []( const cv::Mat &src, int axis ) {
        CV_Assert( src.dims == 4 && ( axis == 2 || axis == 3 ) );
        const int sizes[4] = { src.size[0], src.size[1], src.size[2], src.size[3] };
        cv::Mat dst( 4, sizes, src.type() );
        const int N = sizes[0], C = sizes[1], H = sizes[2], W = sizes[3];
        for ( int n = 0; n < N; ++n )
        {
          for ( int c = 0; c < C; ++c )
          {
            // ptr(n,c) yields the contiguous HxW plane for continuous NCHW.
            cv::Mat srcPlane( H, W, src.type(), const_cast<uchar *>( src.ptr( n, c ) ) );
            cv::Mat dstPlane( H, W, dst.type(), dst.ptr( n, c ) );
            cv::flip( srcPlane, dstPlane, axis == 3 ? 1 : 0 );
          }
        }
        return dst;
      };
      // Every forward returns a freshly detached Mat (the runtime clones its
      // output), so the accumulator is safe from aliasing.
      auto flipH = [ & ]( const cv::Mat &b ) { return flipNCHWAxis( b, 3 ); };
      auto flipV = [ & ]( const cv::Mat &b ) { return flipNCHWAxis( b, 2 ); };
      auto forwardUnflippedH = [ & ]( ) {
        return flipNCHWAxis( forwardOnce( flipH( blob ) ), 3 );
      };
      auto forwardUnflippedHV = [ & ]( ) {
        return flipNCHWAxis( flipNCHWAxis( forwardOnce( flipH( flipV( blob ) ) ), 2 ), 3 );
      };
      cv::Mat acc = output.clone();
      acc += forwardUnflippedH();
      if ( options.tta == TtaMode::HVFlip )
      {
        auto forwardUnflippedV = [ & ]( ) {
          return flipNCHWAxis( forwardOnce( flipV( blob ) ), 2 );
        };
        acc += forwardUnflippedV();
        acc += forwardUnflippedHV();
        acc *= 0.25;
      }
      else
      {
        acc *= 0.5;
      }
      return acc;
    };

    // Forward every declared head (multi-head = one forward per head; the
    // shared session serializes them). Heads are validated with the same
    // contracts as the historical single-head path.
    std::vector<cv::Mat> headOutputs( headNames.size() );
    for ( std::size_t h = 0; h < headNames.size(); ++h )
    {
      const std::string &headName = headNames[h];
      cv::Mat output = forwardHead( headName );
      if ( output.dims != 4 || output.size[0] != static_cast<int>( batchMats.size() ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model output is not a 4-D NCHW batch matching the input tiles (got dims="
                                 + std::to_string( output.dims ) + ", N=" + std::to_string( output.size[0] ) + ")" );
      if ( const std::string typeError = outputTypeMismatch( output.type(), headName );
           !typeError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, typeError );
      if ( output.size[1] <= 0 )
        throw RSOperatorError( ErrorCode::ComputationError, "model output has no channels" );
      if ( const std::string classesError = classesChannelMismatch( m_model, output.size[1], headName );
           !classesError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, classesError );
      headOutputs[h] = output;
    }

    // Band layout: every head's channels in declaration order, plus one
    // uncertainty band after the first head with >= 2 channels when the
    // manifest declares output.uncertainty (classification/segmentation
    // semantics).
    const bool addUncertainty = !uncertainty.empty();
    int totalBands = 0;
    std::vector<int> headChannelList( headOutputs.size() );
    int uncertaintyHeadIndex = -1;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      headChannelList[h] = headOutputs[h].size[1];
      totalBands += headChannelList[h];
      if ( addUncertainty && uncertaintyHeadIndex < 0 && headChannelList[h] >= 2 )
        uncertaintyHeadIndex = static_cast<int>( h );
    }
    const int uncertaintyBandOffset = uncertaintyHeadIndex >= 0 ? totalBands : -1;
    if ( uncertaintyHeadIndex >= 0 )
      ++totalBands;

    if ( !writer )
    {
      // Atomic publication (Platform 4.0): the streaming writer stages into a
      // same-directory temp file; on success it is renamed onto the caller's
      // path, so the output path only ever holds a COMPLETE raster — a crash
      // or failure leaves no truncated file that could look like a result.
      const QFileInfo outFi( QString::fromStdString( outputPath ) );
      stagePath = QString::fromStdString( outputPath ) + QStringLiteral( ".tmp~" );
      QDir().mkpath( outFi.absolutePath() );
      QFile::remove( stagePath );
      const int writerBands = mode == RasterOutputMode::Probability ? totalBands : writeBands;
      writer = std::make_unique<GdalStreamingOutput>( stagePath,
                                                      rasterW, rasterH, writerBands, writeType,
                                                      geoTransform, projection );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "failed to create output raster: " + outputPath );
      writer->setNoDataValue( static_cast<double>( writeNoData ) );
      stats.outBands = writerBands;
      stats.headChannels = headChannelList;
      if ( uncertaintyHeadIndex >= 0 )
        stats.headChannels[static_cast<std::size_t>( uncertaintyHeadIndex )] += 1;
      // Platform 9.0 (M6): per-class product metadata for Labels/Mask.
      if ( mode == RasterOutputMode::Labels || mode == RasterOutputMode::Mask )
      {
        // Labels: PRODUCT classes after the remap; Mask: the {0,1} domain.
        int productClasses = 2;
        if ( mode == RasterOutputMode::Labels )
          productClasses = m_model.postprocess.classMapping.empty()
            ? static_cast<int>( m_model.output.classes.size() )
            : 1 + *std::max_element( m_model.postprocess.classMapping.begin(),
                                     m_model.postprocess.classMapping.end() );
        stats.classPixelCounts.assign(
          static_cast<std::size_t>( std::max( 1, productClasses ) ), 0 );
      }
      // Platform 9.0 (M5): with feather blending the accumulator owns ALL
      // output writing — one slot per Probability band, or the head-0 class
      // slots (+ optional blended uncertainty slot) for derived modes.
      if ( blend == TileBlend::Feather )
      {
        if ( mode == RasterOutputMode::Probability )
          accumulator = std::make_unique<FeatherAccumulator>(
            rasterW, rasterH, writerBands, tileSize, halo, mode,
            static_cast<float>( m_model.postprocess.maskThreshold ),
            m_model.postprocess.classMapping, writeNoData );
        else
        {
          const int head0Classes = headChannelList.empty() ? 0 : headChannelList.front();
          const int slotCount = head0Classes + ( uncertaintyHeadIndex >= 0 ? 1 : 0 );
          accumulator = std::make_unique<FeatherAccumulator>(
            rasterW, rasterH, std::max( 1, slotCount ), tileSize, halo, mode,
            static_cast<float>( m_model.postprocess.maskThreshold ),
            m_model.postprocess.classMapping, writeNoData );
          accumulator->setClassCount( head0Classes );
          accumulator->setClassTally( &stats.classPixelCounts );
        }
      }
      // Record the head layout so downstream consumers can split the stack.
      {
        QString layout;
        for ( std::size_t h = 0; h < headNames.size(); ++h )
        {
          if ( h )
            layout += QLatin1Char( ',' );
          const QString name = headNames[h].empty() ? QStringLiteral( "default" )
                                                    : QString::fromStdString( headNames[h] );
          layout += QString( "%1:%2" ).arg( name ).arg( headChannelList[h] );
        }
        if ( uncertaintyHeadIndex >= 0 )
          layout += QString( ",uncertainty:%1" ).arg( QString::fromStdString( uncertainty ) );
        if ( mode == RasterOutputMode::Labels )
        {
          layout = QStringLiteral( "labels:1" );
          // Deterministic palette (analysis-layer convention, ADR 0061) so
          // label rasters render meaningfully without a lookup sidecar.
          QString palette;
          QString names;
          // Platform 8.0 WP-E: with a declared product-class remap the
          // palette keys are the PRODUCT class ids (what the raster holds),
          // keyed by their model class order.
          const std::vector<int> &classRemap = m_model.postprocess.classMapping;
          for ( int classId = 0; classId < static_cast<int>( m_model.output.classes.size() ); ++classId )
          {
            const QColor color = rsSynthesizedClassColor( classId );
            if ( classId )
            {
              palette += QLatin1Char( ';' );
              names += QLatin1Char( ';' );
            }
            const int productClass =
              ( static_cast<std::size_t>( classId ) < classRemap.size() )
                ? classRemap[static_cast<std::size_t>( classId )] : classId;
            palette += QString( "%1:%2,%3,%4" ).arg( productClass ).arg( color.red() )
                         .arg( color.green() ).arg( color.blue() );
            names += QString::fromStdString( m_model.output.classes[static_cast<std::size_t>( classId )] );
          }
          writer->setMetadataItem( QStringLiteral( "SICNU_CLASS_PALETTE" ), palette );
          writer->setMetadataItem( QStringLiteral( "SICNU_CLASS_NAMES" ), names );
        }
        writer->setMetadataItem( QStringLiteral( "SICNU_OUTPUT_HEADS" ), layout );
      }
    }
    else if ( mode == RasterOutputMode::Probability && totalBands != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" + std::to_string( stats.outBands )
                               + " → " + std::to_string( totalBands) + ")" );
    }

    // The writer exists now, so NoData tiles deferred by earlier all-nodata
    // batches can go straight to disk.
    flushDeferredNoData( currentTileIndex );

    // Per-tile class planes of head 0, collected for the derived output
    // modes (labels/mask/confidence) and written as ONE band per tile.
    std::vector<std::vector<cv::Mat>> derivedPlanes(
      batchMats.size() );

    int bandOffset = 0;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      const cv::Mat &output = headOutputs[h];
      const std::string &headName = headNames[h];
      const int outChannels = output.size[1];
      const int outH = output.size[2];
      const int outW = output.size[3];
      const bool isUncertaintyHead = ( uncertaintyHeadIndex == static_cast<int>( h ) );
      const cv::Mat flat = output.reshape( 1, std::vector<int>{ static_cast<int>( batchMats.size() ) * outChannels,
                                                               outH * outW } );
      for ( std::size_t bi = 0; bi < batchMats.size(); ++bi )
      {
        const CoreTile &bt = batchCores[bi];
        const int fedW = batchFedSize[bi].first;
        const int fedH = batchFedSize[bi].second;
        // Platform 9.0 (M5): rows strictly above this tile's window top can
        // receive no further contributions (row-major tiles) — finalize them.
        if ( accumulator )
          accumulator->flushThrough( std::max( 0, bt.y - halo ), *writer );
        // Whether this tile's head output maps 1:1 back onto the FED window
        // (the geometry feather blending needs: the accumulator averages the
        // whole window prediction, so resizing/cropping paths must not run).
        const bool blendThisHead = accumulator && !resizeToInput
                                     && outW == fedW && outH == fedH;
        // Stitched (core-size) planes of this tile for the uncertainty pass.
        std::vector<cv::Mat> headPlanes;
        if ( isUncertaintyHead && !uncertainty.empty() )
          headPlanes.reserve( static_cast<std::size_t>( outChannels ) );
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
          // Platform 9.0 (M5): under feather blending the FULL window
          // prediction (core + halo — exactly what the model saw) is what the
          // accumulator averages, so the crop/resize mapping is skipped and
          // the plane stays fed-size.
          if ( !blendThisHead )
          {
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
          }
          // Uncertainty statistics need the PRE-threshold class planes:
          // entropy/margin over binarized {0,1} planes would be meaningless.
          cv::Mat thresholded;
          if ( isUncertaintyHead )
            headPlanes.push_back( plane );
          if ( mode == RasterOutputMode::Probability && m_model.postprocess.maskThreshold >= 0.0 )
          {
            const float thr = static_cast<float>( m_model.postprocess.maskThreshold );
            cv::Mat mask = plane >= thr; // NaN ≥ thr is false → 0, restored below
            mask.convertTo( thresholded, CV_32F, 1.0 / 255.0 );
            plane = thresholded;
          }
          // Restore nodata on core pixels whose every input band was invalid.
          // Under blending the plane is the FED window: the core sits halo px in.
          const cv::Mat &tileMask = batchMasks[bi];
          for ( int row = 0; row < bt.h; ++row )
          {
            const uchar *maskRow = tileMask.ptr<uchar>( row );
            float *outRow = plane.ptr<float>( row + ( blendThisHead ? halo : 0 ) );
            for ( int col = 0; col < bt.w; ++col )
            {
              if ( maskRow[col] )
                outRow[col + ( blendThisHead ? halo : 0 )] =
                  std::numeric_limits<float>::quiet_NaN();
            }
          }
          if ( mode == RasterOutputMode::Probability )
          {
            if ( blendThisHead )
            {
              accumulator->add( bandOffset + c, plane, bt, halo, fedW, fedH );
            }
            else
            {
              const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                     currentTileIndex, totalTiles };
              if ( !writer->writeTile( bandOffset + c + 1, writeTile, plane.ptr<float>() ) )
                throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                       + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
            }
          }
          else if ( h == 0 )
          {
            // Derived modes keep the class planes in memory; ONE derived band
            // is written after the channel loop below. Under blending these
            // are the WINDOW planes — the accumulator collapses AFTER blending
            // (blend probabilities, then argmax — never blend labels).
            derivedPlanes[bi].push_back( std::move( plane ) );
          }
        }
        if ( mode != RasterOutputMode::Probability && h == 0 )
        {
          const std::vector<cv::Mat> &planes = derivedPlanes[bi];
          const int channels = static_cast<int>( planes.size() );
          // Platform 8.0 WP-E: the Labels remap must cover every model class
          // of this head — a partial mapping would silently pass classes
          // through un-remapped (the #646 failure class).
          if ( mode == RasterOutputMode::Labels && !m_model.postprocess.classMapping.empty()
               && m_model.postprocess.classMapping.size()
                    != static_cast<std::size_t>( channels ) )
            throw RSOperatorError(
              ErrorCode::InvalidInputData,
              "postprocess.class_mapping declares "
                + std::to_string( m_model.postprocess.classMapping.size() )
                + " entries but head '" + headNames[h] + "' produces "
                + std::to_string( channels ) + " class planes" );
          // Platform 9.0 (M5): blending hands the class planes to the
          // accumulator; the argmax/threshold collapse happens at row
          // finalization on the BLENDED probabilities.
          if ( blendThisHead )
          {
            for ( int c = 0; c < channels; ++c )
              accumulator->add( c, planes[static_cast<std::size_t>( c )], bt, halo, fedW, fedH );
            derivedPlanes[bi].clear();
          }
          else
          {
          cv::Mat derived( bt.h, bt.w, CV_32FC1, cv::Scalar( writeNoData ) );
          const float maskThr = m_model.postprocess.maskThreshold >= 0.0
                                  ? static_cast<float>( m_model.postprocess.maskThreshold )
                                  : 0.5f;
          for ( int row = 0; row < bt.h; ++row )
          {
            float *outRow = derived.ptr<float>( row );
            for ( int col = 0; col < bt.w; ++col )
            {
              bool invalid = false;
              int best = 0;
              float bestv = -std::numeric_limits<float>::infinity();
              for ( int c = 0; c < channels; ++c )
              {
                const float v = planes[static_cast<std::size_t>( c )].ptr<float>( row )[col];
                if ( !std::isfinite( v ) )
                {
                  invalid = true;
                  break;
                }
                if ( v > bestv )
                {
                  bestv = v;
                  best = c;
                }
              }
              if ( invalid )
                continue; // stays NoData
              switch ( mode )
              {
                case RasterOutputMode::Labels:
                {
                  // Platform 8.0 WP-E: declared product-class remap (model
                  // class → product class). Guarded below so an arity
                  // mismatch refuses loudly instead of indexing OOB.
                  const int productClass =
                    m_model.postprocess.classMapping.empty()
                      ? best
                      : m_model.postprocess.classMapping[static_cast<std::size_t>( best )];
                  outRow[col] = static_cast<float>( productClass );
                  // Platform 9.0 (M6): per-product-class pixel tally.
                  if ( productClass >= 0
                       && static_cast<std::size_t>( productClass )
                            < stats.classPixelCounts.size() )
                    stats.classPixelCounts[static_cast<std::size_t>( productClass )]++;
                  break;
                }
                case RasterOutputMode::Confidence:
                  outRow[col] = bestv;
                  break;
                case RasterOutputMode::Mask:
                  outRow[col] = channels == 1 ? ( bestv >= maskThr ? 1.0f : 0.0f )
                                              : ( best != 0 ? 1.0f : 0.0f );
                  break;
                case RasterOutputMode::Probability:
                  break; // unreachable
              }
            }
          }
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( 1, writeTile, derived.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                     + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
          derivedPlanes[bi].clear();
          }
        }
        if ( isUncertaintyHead && uncertaintyBandOffset >= 0 )
        {
          // Platform 9.0 (M5): uncertainty is computed per pixel over this
          // tile's planes — window-size planes under blending, so the band
          // blends exactly like every other output band.
          cv::Mat unc = headUncertainty( headPlanes, uncertainty );
          if ( blendThisHead )
          {
            accumulator->add( uncertaintyBandOffset, unc, bt, halo, fedW, fedH );
          }
          else
          {
            const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                   currentTileIndex, totalTiles };
            if ( !writer->writeTile( uncertaintyBandOffset + 1, writeTile, unc.ptr<float>() ) )
              throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write uncertainty tile" );
          }
        }
        // Tiles are counted once (on the first head); heads share one tile.
        if ( h == 0 )
          ++done;
      }
      bandOffset += outChannels;
    }
    batchMats.clear();
    batchMasks.clear();
    batchFedSize.clear();
    batchCores.clear();
    batchValidPixels.clear();
  };

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
    int validPixels = 0; // core pixels with at least one finite band (#705)
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
          if ( !allInvalid )
            ++validPixels;
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
    batchValidPixels.push_back( validPixels );

    const bool batchFull = static_cast<int>( batchMats.size() ) >= batchSize || tileIndex == totalTiles - 1;
    if ( !batchFull )
      continue;

    // #705: a batch whose tiles hold ZERO valid pixels would run full forward
    // passes only for every output pixel to be overwritten by the NoData
    // restore — skip the forward and queue direct NoData writes instead
    // (flushed as soon as the streaming writer exists).
    if ( batchIsAllNoData( batchValidPixels ) )
    {
      skipped += static_cast<int>( batchCores.size() );
      stats.tilesSkippedNoData += static_cast<int>( batchCores.size() );
      deferredNoData.insert( deferredNoData.end(), batchCores.begin(), batchCores.end() );
      batchMats.clear();
      batchMasks.clear();
      batchFedSize.clear();
      batchCores.clear();
      batchValidPixels.clear();
      context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                              "Tiled inference: " + std::to_string( done + skipped ) + "/"
                                + std::to_string( totalTiles ) );
      continue;
    }

    // OOM ladder (Platform 4.0, goal §3): an over-budget batch is retried
    // tile-by-tile — per-tile semantics never change, so the scientific
    // contract holds (only the batching shrinks). At batch=1 an OOM is final
    // and carries the diagnostic. The engine NEVER responds to OOM by
    // shrinking tiles, changing resolution, or altering model semantics.
    try
    {
      flushBatch( tileIndex );
    }
    catch ( const RSOperatorError &e )
    {
      if ( classifyInferenceError( e.what() ) != InferenceFailureKind::OutOfMemory )
        throw;
      if ( batchMats.size() <= 1 )
        throw RSOperatorError(
          ErrorCode::ComputationError,
          std::string( "inference ran out of memory even at batch=1 (tile " )
            + std::to_string( tileSize )
            + " px): free memory or use a smaller model - the engine never alters spatial "
              "resolution or model semantics to fit memory. Original error: "
            + e.what() );
      ++stats.batchReductions;
      // Serial retry: hold the pending batch, flush one tile per forward.
      const std::vector<cv::Mat> pending = std::move( batchMats );
      const std::vector<cv::Mat> pendingMasks = std::move( batchMasks );
      const std::vector<std::pair<int, int>> pendingFed = std::move( batchFedSize );
      const std::vector<CoreTile> pendingCores = std::move( batchCores );
      const std::vector<int> pendingValid = std::move( batchValidPixels );
      batchMats.clear();
      batchMasks.clear();
      batchFedSize.clear();
      batchCores.clear();
      batchValidPixels.clear();
      for ( std::size_t i = 0; i < pending.size(); ++i )
      {
        batchMats.assign( 1, pending[i] );
        batchMasks.assign( 1, pendingMasks[i] );
        batchFedSize.assign( 1, pendingFed[i] );
        batchCores.assign( 1, pendingCores[i] );
        batchValidPixels.assign( 1, pendingValid[i] );
        try
        {
          flushBatch( tileIndex );
        }
        catch ( const RSOperatorError &inner )
        {
          if ( classifyInferenceError( inner.what() ) == InferenceFailureKind::OutOfMemory )
            throw RSOperatorError(
              ErrorCode::ComputationError,
              "inference ran out of memory even at batch=1 (tile " + std::to_string( tileSize )
                + " px): free memory or use a smaller model — the engine never alters spatial "
                  "resolution or model semantics to fit memory. Original error: "
                + inner.what() );
          throw;
        }
      }
    }
    context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                            "Tiled inference: " + std::to_string( done + skipped ) + "/"
                              + std::to_string( totalTiles ) );
  }

  // Entire raster is NoData: no regular batch ever ran, so the writer (and
  // with it the model's true output channel count) does not exist yet. One
  // zero-tile probe forward establishes the output shape — its result is
  // NaN-restored like any other invalid tile. The probe tile is popped from
  // the deferred list and written by the normal flushBatch path.
  if ( !deferredNoData.empty() && !writer )
  {
    const CoreTile probeTile = deferredNoData.front();
    deferredNoData.erase( deferredNoData.begin() );
    const int probeFedW = resizeToInput ? modelW : probeTile.w + 2 * halo;
    const int probeFedH = resizeToInput ? modelH : probeTile.h + 2 * halo;
    batchMats.push_back( cv::Mat( probeFedH, probeFedW, CV_32FC( bandCount ), cv::Scalar( 0.0 ) ) );
    batchMasks.push_back( cv::Mat( probeTile.h, probeTile.w, CV_8UC1, cv::Scalar( 1 ) ) );
    batchFedSize.emplace_back( probeFedW, probeFedH );
    batchCores.push_back( probeTile );
    batchValidPixels.push_back( 0 );
    flushBatch( totalTiles - 1 );
  }
  if ( !deferredNoData.empty() )
    flushDeferredNoData( totalTiles - 1 );

  }
  catch ( ... )
  {
    abandonOnFailure();
    throw;
  }

  QString writeError;
  if ( accumulator )
    accumulator->finish( *writer ); // Platform 9.0 (M5): blended rows finalize here
  if ( !writer || !writer->closeWithError( &writeError ) )
  {
    if ( writer )
      writer->removeOutput();
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to finalize output raster: " + writeError.toStdString() );
  }
  // Atomic publish: only a fully written, closed raster is renamed onto the
  // caller's path (same directory — same volume). Windows rename does not
  // overwrite, so the previous result moves to a .prev~ backup first; a
  // failure at ANY step restores it — the caller's output path ends up with
  // either the NEW raster or the OLD one, never nothing and never a torn one.
  const QString finalPath = QString::fromStdString( outputPath );
  const QString backupPath = finalPath + QStringLiteral( ".prev~" );
  QFile::remove( backupPath );
  const bool hadExisting = QFile::exists( finalPath );
  if ( hadExisting && !QFile::rename( finalPath, backupPath ) )
  {
    QFile::remove( stagePath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to back up the previous output before publishing: " + outputPath );
  }
  if ( !QFile::rename( stagePath, finalPath ) )
  {
    QFile::remove( stagePath );
    if ( hadExisting )
      QFile::rename( backupPath, finalPath ); // best-effort restore of the old result
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to publish output raster to: " + outputPath );
  }
  // Platform 8.0: a published raster inference product always carries its
  // provenance sidecar. Ordering: the OLD sidecar is removed BEFORE the new
  // product lands (a crash can then only leave a MISSING sidecar, never a
  // stale/mismatched one), and the old product's backup is kept until the
  // new sidecar is in — a sidecar failure restores the previous product
  // instead of destroying it (same invariant as the raster publish).
  QFile::remove( finalPath + QStringLiteral( ".prov.json" ) );
  stats.tilesProcessed = done; // counters finalized BEFORE the document build
  {
    const Json::Value prov = buildProvenanceDocument( m_model, m_runtime, stats, {} );
    std::string provError;
    if ( !publishProvenanceSidecar( finalPath, outputPath, prov, &provError ) )
    {
      QFile::remove( finalPath );
      if ( hadExisting )
        QFile::rename( backupPath, finalPath ); // restore the previous good product
      throw RSOperatorError( ErrorCode::FileNotWritable, provError );
    }
  }
  QFile::remove( backupPath );
  context.reportProgressForced( 1.0, "Tiled inference complete" );
  stats.tilesProcessed = done;
  return stats;
}

// --- Platform 7.0: multimodal / temporal tiled inference --------------------

std::string TileInferenceEngine::gridMismatch( const std::string &primaryPath, int primaryW,
                                               int primaryH, const double *primaryGeoTransform,
                                               const std::string &otherPath, int otherW, int otherH,
                                               const double *otherGeoTransform )
{
  if ( primaryW != otherW || primaryH != otherH )
    return "input grids are not co-registered: '" + primaryPath + "' is "
             + std::to_string( primaryW ) + "x" + std::to_string( primaryH ) + " but '" + otherPath
             + "' is " + std::to_string( otherW ) + "x" + std::to_string( otherH )
             + " — align the rasters first (the runtime never warps implicitly)";
  if ( primaryGeoTransform && otherGeoTransform )
  {
    for ( int i = 0; i < 6; ++i )
    {
      if ( std::abs( primaryGeoTransform[i] - otherGeoTransform[i] ) > 1e-6 )
        return "input grids are not co-registered: '" + primaryPath + "' and '" + otherPath
                 + "' carry different geotransforms — align the rasters first (warp/resample "
                   "through the geospatial raster_convert seam), the runtime refuses misaligned "
                   "feeds instead of guessing";
    }
  }
  return {};
}

std::string TileInferenceEngine::crsMismatch( const std::string &primaryPath,
                                              const std::string &primaryCrs,
                                              const std::string &otherPath,
                                              const std::string &otherCrs,
                                              bool strictAlignment )
{
  const int comparison = compareCrs( QString::fromStdString( primaryCrs ),
                                     QString::fromStdString( otherCrs ) );
  if ( comparison == 0 )
    return {};
  if ( comparison < 0 )
  {
    // At least one side declares no CRS. Non-strict feeds keep the
    // historical behavior (geometry-only check); alignment=reference demands
    // VERIFIED co-registration, so an unverifiable CRS is a refusal.
    if ( !strictAlignment )
      return {};
    return "input grids are not verifiably co-registered: '" + primaryPath + "' and '"
             + otherPath + "' — one of the rasters declares no CRS, and the input "
             "contract demands alignment=reference (verify the CRS or relax the "
             "alignment declaration; the runtime never guesses a CRS)";
  }
  return "input grids are not co-registered: '" + primaryPath + "' ("
           + ( primaryCrs.empty() ? "<no CRS>" : crsDisplayName( QString::fromStdString( primaryCrs ) ) )
           + ") and '" + otherPath + "' ("
           + ( otherCrs.empty() ? "<no CRS>" : crsDisplayName( QString::fromStdString( otherCrs ) ) )
           + ") carry different coordinate reference systems — identical geotransform "
             "numbers under different CRS do NOT describe the same ground. Align the "
             "rasters through the geospatial raster_convert warp seam; the runtime "
             "refuses misaligned feeds instead of warping implicitly";
}

TileInferenceStats TileInferenceEngine::runMultiInput( const std::vector<NamedRasterFeed> &feeds,
                                                       const std::string &outputPath,
                                                       RSOperatorContext &context,
                                                       const TileInferenceRunOptions &options )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "tile inference engine has no runtime session" );
  if ( feeds.empty() )
    throw RSOperatorError( ErrorCode::InvalidParameter, "multi-input inference needs at least one feed" );
  // Platform 9.0 (M5): feather blending is implemented for the single-input
  // engine; the multi-input path refuses loudly instead of silently ignoring
  // the knob (the #646 failure class).
  {
    const TileBlend resolvedBlend =
      options.blend == TileBlend::Unset
        ? ( m_model.tiling.blend == "feather" ? TileBlend::Feather : TileBlend::None )
        : options.blend;
    if ( resolvedBlend == TileBlend::Feather )
      throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tiling.blend=feather is not implemented for the multi-input engine yet — "
          "run the feeds through the single-input path or drop the blend contract" );
  }

  // Head resolution: identical contract to run() (manifest tensor_names vs
  // the graph's own outputs; advisory when the runtime cannot enumerate).
  std::vector<std::string> headNames;
  if ( !m_model.output.tensorNames.empty() )
  {
    if ( const std::string missing = missingOutputTensor( m_model, m_runtime->outputTensorNames() );
         !missing.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData, missing );
    headNames = m_runtime->outputTensorNames().empty() ? std::vector<std::string>{ m_model.output.tensorNames.front() }
                                                       : m_model.output.tensorNames;
  }
  else
    headNames = { std::string() };
  const std::string uncertainty = uncertaintyMethod( m_model );

  // The multimodal path writes the full probability stack: derived formats
  // collapse head 0's class planes and are a single-head single-input
  // convenience — refusing here is the typed, honest answer.
  if ( rasterOutputMode( m_model ) != RasterOutputMode::Probability
       || options.outputMode != RasterOutputMode::Probability )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "multi-input models write the probability stack only — derived "
                             "output.format (labels/mask/confidence) is a single-head convenience; "
                             "derive from the stack afterwards or run the single-input engine" );
  if ( m_model.preprocess.resize == "to_input" )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "preprocess.resize=to_input is not supported for multi-input models — "
                             "declare aligned grids per input (alignment=reference) instead" );

  // Map feeds onto the manifest input contracts: by NAME (the binding
  // document) or positionally when the feed omits its name. Duplicate names
  // refuse — ambiguity is never resolved silently.
  if ( feeds.size() != m_model.inputs.size() )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "model declares " + std::to_string( m_model.inputs.size() )
                             + " inputs but " + std::to_string( feeds.size() )
                             + " feeds were given" );
  std::vector<std::size_t> feedContract( feeds.size() );
  for ( std::size_t f = 0; f < feeds.size(); ++f )
  {
    if ( feeds[f].name.empty() )
    {
      feedContract[f] = f; // positional
      continue;
    }
    bool matched = false;
    for ( std::size_t c = 0; c < m_model.inputs.size(); ++c )
    {
      if ( m_model.inputs[c].name == feeds[f].name )
      {
        if ( std::find( feedContract.begin(), feedContract.begin() + f, c ) != feedContract.begin() + f )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "two feeds share the input name '" + feeds[f].name + "'" );
        feedContract[f] = c;
        matched = true;
        break;
      }
    }
    if ( !matched )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "feed '" + feeds[f].name
                               + "' matches no declared model input (declared: "
                               + ( m_model.inputs.empty() || m_model.inputs[0].name.empty()
                                     ? std::string( "<unnamed>" )
                                     : m_model.inputs[0].name )
                               + "...)" );
  }

  // Platform 9.0 (M3): each feed resolves its EFFECTIVE preprocessing — the
  // per-input override when the manifest declares one, else the global
  // contract. The shared `pre`/`meanStd`/`hasClamp` locals below remain the
  // SINGLE-INPUT path; runMultiInput never reads them for a feed.
  const ModelPreprocessContract &pre = m_model.preprocess;
  const bool meanStd = pre.normalize == "mean_std";
  const bool hasClamp = !std::isnan( pre.clampMin ) || !std::isnan( pre.clampMax );

  // Open every feed's frames; validate the temporal contract and co-registration.
  struct FeedReader
  {
    std::vector<std::unique_ptr<GdalDatasetWrapper>> frames; // per timestep
    /// Platform 8.0 WP-D: per-timestep quality masks (optional, parallel to
    /// frames; null entries only when the feed declares no masks at all).
    std::vector<std::unique_ptr<GdalDatasetWrapper>> qualityMasks;
    std::vector<int> bands;
    int channels = 0;        // bands × temporal length (the fed channel count)
    const ModelInputContract *contract = nullptr;
    std::vector<float> sentinel;
    std::vector<bool> hasSentinel;
    std::vector<float> window; // reused per-tile buffer
    std::vector<float> qualityWindow; // reused per-tile mask buffer (WP-D)
    // Platform 9.0 (M3): the feed's resolved preprocessing contract.
    ModelPreprocessContract preprocess;
  };
  std::vector<FeedReader> readers( feeds.size() );

  GdalDatasetWrapper primary;
  if ( !primary.open( QString::fromStdString( feeds[0].paths.at( 0 ) ) ) )
    throw RSOperatorError( ErrorCode::GdalError,
                           "failed to open primary input raster: " + feeds[0].paths[0] );
  const int rasterW = primary.width();
  const int rasterH = primary.height();
  if ( rasterW <= 0 || rasterH <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + feeds[0].paths[0] );
  const std::array<double, 6> primaryGt = primary.geoTransform();
  // Raw projection strings are the COMPARISON input (display names can be
  // lossy — authority-less WKT is truncated for messages and would never
  // re-parse); crsDisplayName is for messages/sidecar only.
  const QString primaryProjection = primary.projection();
  const std::string primaryCrsRaw = primaryProjection.toStdString();
  const std::string primaryCrs = crsDisplayName( primaryProjection );
  // Declared before the feed loop: grid provenance accumulates per feed.
  TileInferenceStats stats;

  for ( std::size_t f = 0; f < feeds.size(); ++f )
  {
    FeedReader &reader = readers[f];
    reader.contract = &m_model.inputs[feedContract[f]];
    const ModelInputContract &contract = *reader.contract;

    // Platform 9.0 (M3): effective preprocessing for THIS feed — the per-input
    // override when declared, else the global contract (identical values for
    // override-free manifests; historical behavior preserved bit-for-bit).
    reader.preprocess = contract.preprocessDeclared ? contract.preprocess : m_model.preprocess;
    const ModelPreprocessContract &feedPre = reader.preprocess;

    // Platform 8.0 alignment provenance: preparedFrom must parallel the fed
    // paths (or be absent) — a partial list would record wrong origins.
    if ( !feeds[f].preparedFrom.empty()
         && feeds[f].preparedFrom.size() != feeds[f].paths.size() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "feed '" + feeds[f].name + "': prepared_from provenance has "
                               + std::to_string( feeds[f].preparedFrom.size() ) + " entries but "
                               + std::to_string( feeds[f].paths.size() ) + " frames are fed" );
    // Platform 8.0 CRS authority: strict only when the contract demands
    // verified co-registration (alignment=reference).
    const bool strictAlignment = ( contract.alignment == "reference" );

    // Platform 8.0 dynamic T (sequence collapse): the FEED defines T — every
    // provided frame is fed, no missing-frame policy exists by definition.
    const bool dynamicT = contract.temporalCollapse == "sequence" && contract.temporalDynamic;
    const std::size_t declaredFrames =
      dynamicT ? feeds[f].paths.size()
               : ( contract.temporalLength > 0 ? static_cast<std::size_t>( contract.temporalLength )
                                               : 1u );
    const std::string missingPolicy =
      contract.missingTimestep.empty() ? "refuse" : contract.missingTimestep;
    if ( feeds[f].paths.empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "feed '" + feeds[f].name + "' provides no raster paths" );
    // Resource bound (WP-D): a feed materializes ALL its frames in memory —
    // an unbounded time axis is an unbounded allocation. 1024 frames is far
    // above any real EO series and keeps blob memory bounded per tile.
    constexpr std::size_t kMaxTemporalFrames = 1024;
    if ( feeds[f].paths.size() > kMaxTemporalFrames )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "feed '" + feeds[f].name + "' provides "
                               + std::to_string( feeds[f].paths.size() ) + " frames; the "
                               "temporal lane is bounded at " + std::to_string( kMaxTemporalFrames )
                               + " (split the series or coarsen it)" );
    if ( dynamicT )
    {
      if ( feeds[f].paths.size() < 2 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "feed '" + feeds[f].name
                                 + "' declares temporal_dynamic but provides only "
                                 + std::to_string( feeds[f].paths.size() )
                                 + " frame (a time axis needs T >= 2)" );
    }
    else
    {
      if ( feeds[f].paths.size() > declaredFrames )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "feed '" + feeds[f].name + "' provides "
                                 + std::to_string( feeds[f].paths.size() ) + " frames but the input "
                                 "contract declares temporal_length "
                                 + std::to_string( contract.temporalLength ) );
      if ( feeds[f].paths.size() < declaredFrames && missingPolicy != "zero" )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "feed '" + feeds[f].name + "' provides "
                                 + std::to_string( feeds[f].paths.size() ) + " of "
                                 + std::to_string( declaredFrames )
                                 + " temporal frames and missing_timestep=refuse (declare "
                                   "missing_timestep=zero to fill missing frames explicitly)" );
    }
    // Zero-filled frames beyond the provided paths materialize as absent
    // datasets — the read path substitutes zeros for them.

    // Platform 8.0 WP-D: declared acquisition times must parse as ISO 8601
    // and be STRICTLY INCREASING in feed order. A misordered series is a
    // typed refusal — the engine never silently sorts (the model would see a
    // different time axis than the agent intended).
    if ( !feeds[f].timestamps.empty() )
    {
      if ( feeds[f].timestamps.size() != feeds[f].paths.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "feed '" + feeds[f].name + "': " + std::to_string( feeds[f].timestamps.size() )
                                 + " timestamps but " + std::to_string( feeds[f].paths.size() )
                                 + " frames (timestamps must parallel the frames)" );
      QDateTime previous;
      for ( std::size_t t = 0; t < feeds[f].timestamps.size(); ++t )
      {
        const QDateTime parsed =
          QDateTime::fromString( QString::fromStdString( feeds[f].timestamps[t] ), Qt::ISODate );
        if ( !parsed.isValid() )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "feed '" + feeds[f].name + "': timestamp '"
                                   + feeds[f].timestamps[t]
                                   + "' is not a valid ISO 8601 instant" );
        if ( t > 0 && !( previous < parsed ) )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "feed '" + feeds[f].name + "': timestamps are not strictly "
                                   "increasing ('" + feeds[f].timestamps[t - 1] + "' then '"
                                   + feeds[f].timestamps[t]
                                   + "') — order the frames by acquisition time; the engine "
                                     "never reorders a temporal series silently" );
        previous = parsed;
      }
    }
    // Quality masks must parallel the frames 1:1 (Platform 8.0 WP-D).
    if ( !feeds[f].qualityMasks.empty()
         && feeds[f].qualityMasks.size() != feeds[f].paths.size() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "feed '" + feeds[f].name + "': " + std::to_string( feeds[f].qualityMasks.size() )
                               + " quality masks but " + std::to_string( feeds[f].paths.size() )
                               + " frames (quality masks must parallel the frames)" );

    // Band selection against the FIRST provided frame.
    const std::string &firstPath = feeds[f].paths.front();
    std::vector<int> bandList = feeds[f].bands;
    std::string feedCrs;     // display CRS of the first frame ("" = undeclared)
    std::string feedCrsRaw;  // RAW projection string (the comparison input)
    {
      GdalDatasetWrapper probe;
      if ( !probe.open( QString::fromStdString( firstPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + firstPath );
      if ( probe.width() != rasterW || probe.height() != rasterH )
      {
        const std::array<double, 6> gt = probe.geoTransform();
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               gridMismatch( feeds[0].paths[0], rasterW, rasterH, primaryGt.data(),
                                             firstPath, probe.width(), probe.height(), gt.data() ) );
      }
      const std::array<double, 6> gt = probe.geoTransform();
      feedCrs = crsDisplayName( probe.projection() );
      feedCrsRaw = probe.projection().toStdString();
      if ( f > 0 )
      {
        if ( const std::string grid = gridMismatch( feeds[0].paths[0], rasterW, rasterH,
                                                    primaryGt.data(), firstPath, rasterW, rasterH,
                                                    gt.data() );
             !grid.empty() )
          throw RSOperatorError( ErrorCode::InvalidInputData, grid );
        // Platform 8.0: same geotransform NUMBERS under different CRS are
        // different grids — the CRS verdict is part of co-registration.
        if ( const std::string crs = crsMismatch( feeds[0].paths[0], primaryCrsRaw, firstPath,
                                                  feedCrsRaw, strictAlignment );
             !crs.empty() )
          throw RSOperatorError( ErrorCode::InvalidInputData, crs );
      }
      const int rasterBands = probe.bandCount();
      if ( rasterBands <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + firstPath );
      if ( bandList.empty() )
      {
        bandList.resize( rasterBands );
        for ( int i = 0; i < rasterBands; ++i )
          bandList[static_cast<std::size_t>( i )] = i + 1;
      }
      else
      {
        for ( int b : bandList )
          if ( b < 1 || b > rasterBands )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "feed '" + feeds[f].name + "': band "
                                     + std::to_string( b ) + " out of range (1.."
                                     + std::to_string( rasterBands ) + ")" );
      }
      // Dtype contract per feed: each input contract's declared dtype must
      // match the actual GDAL type of every band fed from that feed (same
      // semantics as the single-input engine's #632/#705 check).
      if ( !contract.dtype.empty() )
      {
        static const std::map<std::string, int> kAccepted = {
          { "float32", GDT_Float32 }, { "float64", GDT_Float64 },
          { "float16", GDT_Float32 }, { "uint16", GDT_UInt16 },
          { "int16", GDT_Int16 },     { "uint8", GDT_Byte },
          { "int32", GDT_Int32 },     { "uint32", GDT_UInt32 },
        };
        const auto accepted = kAccepted.find( contract.dtype );
        if ( accepted == kAccepted.end() )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "feed '" + feeds[f].name + "': manifest declares unsupported "
                                   "input dtype '" + contract.dtype + "'" );
        for ( int band : bandList )
        {
          if ( probe.bandDataType( band ) != accepted->second )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "feed '" + feeds[f].name + "': manifest requires input dtype '"
                                     + contract.dtype + "' but band " + std::to_string( band )
                                     + " has GDAL type "
                                     + std::to_string( probe.bandDataType( band ) )
                                     + " (convert the raster or update the manifest)" );
        }
      }
      if ( !contract.bandRoles.empty()
           && contract.bandRoles.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "feed '" + feeds[f].name + "': manifest declares "
                                 + std::to_string( contract.bandRoles.size() ) + " band roles but "
                                 + std::to_string( bandList.size() ) + " bands are fed" );
      // Platform 9.0 (M3): mean/std arity is checked against THIS feed's
      // EFFECTIVE preprocess and ITS fed channel count — a per-input override
      // with its own channel count validates alone, never against another
      // feed's band count.
      const bool feedMeanStd = feedPre.normalize == "mean_std";
      if ( feedMeanStd && !feedPre.mean.empty() && feedPre.mean.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "feed '" + feeds[f].name + "': preprocess.mean declares "
                                 + std::to_string( feedPre.mean.size() )
                                 + " channels but " + std::to_string( bandList.size() )
                                 + " bands are fed" );
      if ( feedMeanStd && !feedPre.stdv.empty() && feedPre.stdv.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "feed '" + feeds[f].name + "': preprocess.std declares "
                                 + std::to_string( feedPre.stdv.size() )
                                 + " channels but " + std::to_string( bandList.size() )
                                 + " bands are fed" );
    }
    reader.bands = bandList;
    reader.channels = static_cast<int>( bandList.size() * declaredFrames );

    // Open all provided frames.
    reader.frames.resize( declaredFrames );
    for ( std::size_t t = 0; t < feeds[f].paths.size(); ++t )
    {
      auto frame = std::make_unique<GdalDatasetWrapper>();
      if ( !frame->open( QString::fromStdString( feeds[f].paths[t] ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed to open input raster: " + feeds[f].paths[t] );
      // EVERY frame must sit on the primary grid — a mismatched later frame
      // would silently feed shifted/NaN windows instead of failing loudly.
      if ( frame->width() != rasterW || frame->height() != rasterH
           || [ & ]() {
                const std::array<double, 6> gt = frame->geoTransform();
                for ( int i = 0; i < 6; ++i )
                  if ( std::abs( gt[i] - primaryGt[i] ) > 1e-6 )
                    return true;
                return false;
              }() )
      {
        const std::array<double, 6> gt = frame->geoTransform();
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               gridMismatch( feeds[0].paths[0], rasterW, rasterH,
                                             primaryGt.data(), feeds[f].paths[t],
                                             frame->width(), frame->height(), gt.data() ) );
      }
      // Every frame is also CRS-checked: a temporal series acquired in one
      // CRS must never leak a reprojected member whose numbers happen to fit.
      if ( const std::string crs = crsMismatch( feeds[0].paths[0], primaryCrsRaw,
                                                feeds[f].paths[t],
                                                frame->projection().toStdString(),
                                                strictAlignment );
           !crs.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, crs );
      reader.frames[t] = std::move( frame );
    }
    // Missing frames stay null → zero-filled below (missing_timestep=zero).

    // Platform 8.0 WP-D: open quality masks and verify each against the
    // primary grid (a mask that does not sit on the fed grid would silently
    // invalidate the wrong pixels — worse than no mask).
    reader.qualityMasks.resize( feeds[f].qualityMasks.size() );
    for ( std::size_t t = 0; t < feeds[f].qualityMasks.size(); ++t )
    {
      auto mask = std::make_unique<GdalDatasetWrapper>();
      if ( !mask->open( QString::fromStdString( feeds[f].qualityMasks[t] ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed to open quality mask: " + feeds[f].qualityMasks[t] );
      if ( mask->width() != rasterW || mask->height() != rasterH )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "quality mask '" + feeds[f].qualityMasks[t] + "' is "
                                 + std::to_string( mask->width() ) + "x"
                                 + std::to_string( mask->height() ) + " but the primary grid is "
                                 + std::to_string( rasterW ) + "x" + std::to_string( rasterH )
                                 + " — align the mask through the geospatial seam first" );
      if ( const std::string crs =
             crsMismatch( feeds[0].paths[0], primaryCrsRaw, feeds[f].qualityMasks[t],
                          mask->projection().toStdString(), strictAlignment );
           !crs.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, crs );
      if ( mask->bandCount() < 1 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "quality mask '" + feeds[f].qualityMasks[t]
                                 + "' carries no bands" );
      reader.qualityMasks[t] = std::move( mask );
    }

    // Per-band NoData sentinels from the first frame.
    reader.sentinel.assign( bandList.size(), 0.0f );
    reader.hasSentinel.assign( bandList.size(), false );
    if ( reader.frames[0] )
    {
      for ( std::size_t i = 0; i < bandList.size(); ++i )
      {
        bool has = false;
        const double nd = reader.frames[0]->bandNoDataValue( bandList[i], &has );
        if ( has && std::isfinite( nd ) )
        {
          reader.sentinel[i] = static_cast<float>( nd );
          reader.hasSentinel[i] = true;
        }
      }
    }

    // Platform 8.0 grid provenance for payload + sidecar: recorded only
    // after every check above passed, so a presence here IS the verdict.
    GridProvenance grid;
    grid.name = contract.name.empty() ? "input" + std::to_string( f + 1 ) : contract.name;
    grid.path = firstPath;
    grid.preparedFrom = feeds[f].preparedFrom; // per-frame origins, verbatim
    grid.crs = ( f == 0 ) ? primaryCrs : feedCrs;
    grid.crsVerified = ( f == 0 ) ? !grid.crs.empty()
                                  : ( !grid.crs.empty() && !primaryCrs.empty() );
    grid.width = rasterW;
    grid.height = rasterH;
    grid.frames = static_cast<int>( feeds[f].paths.size() );
    // Platform 9.0 (M3): the effective preprocess summary — WHAT normalized
    // this feed, and against how many channels.
    grid.preprocessNote =
      feedPre.normalize.empty() ? "none" : feedPre.normalize;
    // Platform 9.0 (M3): identity fingerprint (structure + bounded content).
    if ( options.computeFeedFingerprints )
      grid.fingerprint = feedFingerprint( firstPath, bandList,
                                          options.fingerprintContentMaxBytes );
    stats.inputGrids.push_back( std::move( grid ) );
  }

  // Tile geometry: primary grid authority; fed channels for the batch budget
  // come from the PRIMARY feed (the others scale linearly and the admission
  // seam already accounted the session).
  const int tileSize = std::min( effectiveTileSize( m_model ), std::max( rasterW, rasterH ) );
  const int halo = std::max( 0, effectiveHalo( m_model ) );
  const int pad = std::max( 0, pre.pad );
  // Budget on the LARGEST feed: with per-feed temporal lengths the "others
  // scale linearly" assumption does not hold (a dynamic-T feed can dwarf the
  // primary), so admit on the worst case.
  int maxFeedChannels = 0;
  for ( const FeedReader &reader : readers )
    maxFeedChannels = std::max( maxFeedChannels, reader.channels );
  int batchSize = effectiveBatchSize( m_model, ModelRuntimeRegistry::instance().hardware(),
                                      tileSize, maxFeedChannels );
  if ( options.batchSizeOverride > 0 )
    batchSize = std::min( batchSize, options.batchSizeOverride );
  batchSize = std::max( 1, batchSize );
  const int maxWin = tileSize + 2 * halo + 2 * pad;

  std::vector<CoreTile> core;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      core.push_back( CoreTile{ x, y, std::min( tileSize, rasterW - x ), std::min( tileSize, rasterH - y ) } );
  const int totalTiles = static_cast<int>( core.size() );
  const double minCoverage = std::clamp( m_model.tiling.minValidCoverage, 0.0, 1.0 );

  stats.tileSize = tileSize;
  stats.halo = halo;
  stats.batchSize = batchSize;
  stats.tilesPlanned = totalTiles;
  stats.outWidth = rasterW;
  stats.outHeight = rasterH;

  // Reused per-feed window buffers (fed size incl. halo; pad applied after
  // preprocessing so the zero pad cannot be normalized over).
  for ( auto &reader : readers )
    reader.window.assign( static_cast<std::size_t>( maxWin ) * maxWin * reader.bands.size(), 0.0f );

  // Pending batch across ALL feeds.
  std::vector<std::vector<cv::Mat>> batchByFeed( feeds.size() );
  std::vector<cv::Mat> batchMasks;
  std::vector<std::pair<int, int>> batchFedSize;
  std::vector<CoreTile> batchCores;
  std::vector<int> batchValidPixels;
  std::vector<CoreTile> deferredNoData;

  std::unique_ptr<GdalStreamingOutput> writer;
  QString stagePath;
  int done = 0;
  int skipped = 0;

  // Preprocess one RAW window in place: sentinel→NaN, non-finite→0,
  // normalize, clamp. Channel-aware exactly like the single-input engine;
  // Platform 9.0 (M3): the contract is the FEED's effective preprocess, so a
  // per-input override (e.g. SAR vs optical normalization) applies per feed.
  auto preprocessWindow = [&]( FeedReader &reader, std::vector<float> &buffer, int winW,
                               int winH ) {
    const ModelPreprocessContract &pre = reader.preprocess;
    const bool meanStd = pre.normalize == "mean_std";
    const bool hasClamp = !std::isnan( pre.clampMin ) || !std::isnan( pre.clampMax );
    const std::size_t bandCount = reader.bands.size();
    const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
    for ( std::size_t i = 0; i < totalFloats; ++i )
    {
      const std::size_t b = i % bandCount;
      if ( reader.hasSentinel[b] && buffer[i] == reader.sentinel[b] )
        buffer[i] = std::numeric_limits<float>::quiet_NaN();
      if ( !std::isfinite( buffer[i] ) )
        buffer[i] = 0.0f; // nodata_policy "zero"
    }
    if ( meanStd || ( pre.normalize == "linear" && pre.scale != 1.0 ) )
    {
      const double *meanArr = meanStd && !pre.mean.empty() ? pre.mean.data() : nullptr;
      const double *stdArr = meanStd && !pre.stdv.empty() ? pre.stdv.data() : nullptr;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        const std::size_t c = i % bandCount;
        double v = buffer[i];
        if ( meanStd )
        {
          if ( meanArr )
            v -= meanArr[c];
          if ( stdArr && stdArr[c] > 0.0 )
            v /= stdArr[c];
        }
        v *= pre.scale;
        if ( hasClamp )
        {
          if ( !std::isnan( pre.clampMin ) && v < pre.clampMin )
            v = pre.clampMin;
          if ( !std::isnan( pre.clampMax ) && v > pre.clampMax )
            v = pre.clampMax;
        }
        buffer[i] = static_cast<float>( v );
      }
    }
    else if ( hasClamp )
    {
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        double v = buffer[i];
        if ( !std::isnan( pre.clampMin ) && v < pre.clampMin )
          v = pre.clampMin;
        if ( !std::isnan( pre.clampMax ) && v > pre.clampMax )
          v = pre.clampMax;
        buffer[i] = static_cast<float>( v );
      }
    }
  };

  // One forward pass + stitch + write for the pending batch. The OOM ladder
  // and the atomic publish share the single-input semantics exactly.
  auto flushBatch = [&]( int currentTileIndex ) {
    context.throwIfCancelled();

    // Build one blob per feed. Channel-fold feeds (temporalCollapse
    // "channels") carry (B, T·C, H, W); sequence feeds carry an explicit
    // time axis (B, T, C, H, W) — rank 5, layout NCTHW (Platform 8.0 WP-D).
    const int B = static_cast<int>( batchCores.size() );
    const int fedW = batchFedSize[0].first;
    const int fedH = batchFedSize[0].second;
    std::vector<NamedTensor> inputs( feeds.size() );
    for ( std::size_t f = 0; f < feeds.size(); ++f )
    {
      FeedReader &reader = readers[f];
      const bool sequenceCollapse = reader.contract->temporalCollapse == "sequence";
      const int C = sequenceCollapse ? static_cast<int>( reader.bands.size() ) : reader.channels;
      std::vector<cv::Mat> &mats = batchByFeed[f];
      if ( sequenceCollapse )
      {
        const int T = static_cast<int>( reader.frames.size() );
        const int dims[5] = { B, T, C, fedH, fedW };
        cv::Mat blob( 5, dims, CV_32F );
        blob.setTo( 0 );
        for ( int b = 0; b < B; ++b )
        {
          const cv::Mat &tileMat = mats[static_cast<std::size_t>( b )]; // HWC, T·C channels
          std::vector<cv::Mat> channels;
          cv::split( tileMat, channels );
          for ( int t = 0; t < T; ++t )
            for ( int c = 0; c < C; ++c )
            {
              const cv::Mat &ch = channels[static_cast<std::size_t>( t * C + c )];
              // cv::Mat::ptr has no 4-index overload for 5-D mats — the blob
              // is continuous, so address the (b,t,c) plane arithmetically.
              float *dst =
                reinterpret_cast<float *>( blob.data )
                + ( ( static_cast<std::size_t>( b ) * T + t ) * C + c )
                    * static_cast<std::size_t>( fedH ) * fedW;
              for ( int y = 0; y < fedH; ++y )
                std::memcpy( dst + static_cast<std::size_t>( y ) * fedW, ch.ptr<float>( y ),
                             static_cast<std::size_t>( fedW ) * sizeof( float ) );
            }
        }
        inputs[f] = NamedTensor{ reader.contract->name, TensorBlob::fromMat( blob ) };
        continue;
      }
      const int dims[4] = { B, C, fedH, fedW };
      cv::Mat blob( 4, dims, CV_32F );
      blob.setTo( 0 );
      for ( int b = 0; b < B; ++b )
      {
        const cv::Mat &tileMat = mats[static_cast<std::size_t>( b )]; // HWC, C channels
        std::vector<cv::Mat> channels;
        cv::split( tileMat, channels );
        for ( int c = 0; c < C; ++c )
        {
          const cv::Mat &ch = channels[static_cast<std::size_t>( c )];
          float *dst = blob.ptr<float>( b, c, 0 );
          for ( int y = 0; y < fedH; ++y )
            std::memcpy( dst + static_cast<std::size_t>( y ) * fedW, ch.ptr<float>( y ),
                         static_cast<std::size_t>( fedW ) * sizeof( float ) );
        }
      }
      inputs[f] = NamedTensor{ reader.contract->name, TensorBlob::fromMat( blob ) };
    }

    // One named forward for the whole batch (named bind; all heads).
    std::vector<NamedTensor> outputs;
    try
    {
      outputs = m_runtime->inferNamed( inputs, headNames.front().empty() ? std::vector<std::string>{} : headNames );
    }
    catch ( const RSOperatorError & )
    {
      throw;
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             std::string( "multi-input forward pass failed: " ) + e.what() );
    }

    // Head outputs: named results first, then graph order for unnamed ones.
    std::vector<cv::Mat> headOutputs;
    std::vector<std::string> resolvedHeadNames;
    for ( auto &nt : outputs )
    {
      if ( nt.second.rank() != 4 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model output '" + nt.first + "' is rank "
                                 + std::to_string( nt.second.rank() )
                                 + " — the raster writer stitches rank-4 heads" );
      cv::Mat mat;
      try
      {
        mat = nt.second.toMat();
      }
      catch ( const std::exception &e )
      {
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               std::string( "model output '" ) + nt.first
                                 + "' cannot cross the raster stitcher: " + e.what() );
      }
      if ( const std::string typeError = outputTypeMismatch( mat.type(), nt.first ); !typeError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, typeError );
      if ( mat.size[1] <= 0 )
        throw RSOperatorError( ErrorCode::ComputationError, "model output has no channels" );
      if ( const std::string classesError = classesChannelMismatch( m_model, mat.size[1], nt.first );
           !classesError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, classesError );
      headOutputs.push_back( std::move( mat ) );
      resolvedHeadNames.push_back( nt.first );
    }
    if ( headOutputs.empty() )
      throw RSOperatorError( ErrorCode::ComputationError, "model produced no usable output" );
    if ( resolvedHeadNames.size() < headNames.size() && !headNames.front().empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "model produced " + std::to_string( resolvedHeadNames.size() )
                               + " heads but the manifest declares "
                               + std::to_string( headNames.size() ) );

    // Band layout: identical to run() — all heads in order + one uncertainty
    // band after the first head with >= 2 channels.
    const bool addUncertainty = !uncertainty.empty();
    int totalBands = 0;
    std::vector<int> headChannelList( headOutputs.size() );
    int uncertaintyHeadIndex = -1;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      headChannelList[h] = headOutputs[h].size[1];
      totalBands += headChannelList[h];
      if ( addUncertainty && uncertaintyHeadIndex < 0 && headChannelList[h] >= 2 )
        uncertaintyHeadIndex = static_cast<int>( h );
    }
    const int uncertaintyBandOffset = uncertaintyHeadIndex >= 0 ? totalBands : -1;
    if ( uncertaintyHeadIndex >= 0 )
      ++totalBands;

    if ( !writer )
    {
      const QFileInfo outFi( QString::fromStdString( outputPath ) );
      stagePath = QString::fromStdString( outputPath ) + QStringLiteral( ".tmp~" );
      QDir().mkpath( outFi.absolutePath() );
      QFile::remove( stagePath );
      writer = std::make_unique<GdalStreamingOutput>( stagePath, rasterW, rasterH, totalBands,
                                                      /*GDT_Float32*/ 6,
                                                      primary.geoTransform(), primary.projection() );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "failed to create output raster: " + outputPath );
      writer->setNoDataValue( std::numeric_limits<float>::quiet_NaN() );
      stats.outBands = totalBands;
      stats.headChannels = headChannelList;
      if ( uncertaintyHeadIndex >= 0 )
        stats.headChannels[static_cast<std::size_t>( uncertaintyHeadIndex )] += 1;
      QString layout;
      for ( std::size_t h = 0; h < resolvedHeadNames.size(); ++h )
      {
        if ( h )
          layout += QLatin1Char( ',' );
        const QString name = resolvedHeadNames[h].empty() ? QStringLiteral( "default" )
                                                          : QString::fromStdString( resolvedHeadNames[h] );
        layout += QString( "%1:%2" ).arg( name ).arg( headChannelList[h] );
      }
      if ( uncertaintyHeadIndex >= 0 )
        layout += QString( ",uncertainty:%1" ).arg( QString::fromStdString( uncertainty ) );
      writer->setMetadataItem( QStringLiteral( "SICNU_OUTPUT_HEADS" ), layout );
    }
    else if ( totalBands != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" +
                               std::to_string( stats.outBands ) + " → " + std::to_string( totalBands )
                               + ")" );
    }

    // Stitch: per head, per tile, per channel — the same crop/resize/restore
    // math as the single-input engine (grid-preserving ⇒ crop halo+pad).
    int bandOffset = 0;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      const cv::Mat &output = headOutputs[h];
      const int outChannels = output.size[1];
      const int outH = output.size[2];
      const int outW = output.size[3];
      const bool isUncertaintyHead = ( uncertaintyHeadIndex == static_cast<int>( h ) );
      const cv::Mat flat =
        output.reshape( 1, std::vector<int>{ B * outChannels, outH * outW } );
      for ( int bi = 0; bi < B; ++bi )
      {
        const CoreTile &bt = batchCores[static_cast<std::size_t>( bi )];
        const int tileFedW = batchFedSize[static_cast<std::size_t>( bi )].first;
        const int tileFedH = batchFedSize[static_cast<std::size_t>( bi )].second;
        std::vector<cv::Mat> headPlanes;
        if ( isUncertaintyHead )
          headPlanes.reserve( static_cast<std::size_t>( outChannels ) );
        for ( int c = 0; c < outChannels; ++c )
        {
          cv::Mat plane = flat.row( bi * outChannels + c ).reshape( 1, outH ).clone();
          if ( outW == tileFedW && outH == tileFedH )
          {
            // Grid-preserving: crop halo+pad border away.
            const int margin = halo + pad;
            if ( margin > 0 )
              plane = plane( cv::Range( margin, margin + bt.h ), cv::Range( margin, margin + bt.w ) ).clone();
          }
          else if ( outW != bt.w || outH != bt.h )
          {
            cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, cv::INTER_LINEAR );
          }
          // The writer consumes a CONTIGUOUS bt.w × bt.h buffer: with uniform
          // fed windows the core sits at the window origin, so an uncropped
          // plane is still fed-sized — materialize the exact core rect.
          if ( plane.cols != bt.w || plane.rows != bt.h )
            plane = plane( cv::Range( 0, bt.h ), cv::Range( 0, bt.w ) ).clone();
          if ( isUncertaintyHead )
            headPlanes.push_back( plane );
          if ( m_model.postprocess.maskThreshold >= 0.0 )
          {
            const float thr = static_cast<float>( m_model.postprocess.maskThreshold );
            cv::Mat mask = plane >= thr;
            cv::Mat thresholded;
            mask.convertTo( thresholded, CV_32F, 1.0 / 255.0 );
            plane = thresholded;
          }
          // Restore NoData where every feed's every band was invalid.
          const cv::Mat &tileMask = batchMasks[static_cast<std::size_t>( bi )];
          for ( int row = 0; row < bt.h; ++row )
          {
            const uchar *maskRow = tileMask.ptr<uchar>( row );
            float *outRow = plane.ptr<float>( row );
            for ( int col = 0; col < bt.w; ++col )
              if ( maskRow[col] )
                outRow[col] = std::numeric_limits<float>::quiet_NaN();
          }
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( bandOffset + c + 1, writeTile, plane.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "failed to write output tile at (" + std::to_string( bt.x )
                                     + ", " + std::to_string( bt.y ) + ")" );
        }
        if ( isUncertaintyHead && uncertaintyBandOffset >= 0 )
        {
          cv::Mat unc = headUncertainty( headPlanes, uncertainty );
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( uncertaintyBandOffset + 1, writeTile, unc.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write uncertainty tile" );
        }
      }
      bandOffset += outChannels;
    }
    done += B;
    for ( auto &mats : batchByFeed )
      mats.clear();
    batchMasks.clear();
    batchFedSize.clear();
    batchCores.clear();
    batchValidPixels.clear();
    context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                            "Tiled multi-input inference: " + std::to_string( done + skipped ) + "/"
                              + std::to_string( totalTiles ) );
  };

  auto flushDeferredNoDataMulti = [&]( int currentTileIndex ) {
    if ( !writer || deferredNoData.empty() )
      return;
    while ( !deferredNoData.empty() )
    {
      const CoreTile &bt = deferredNoData.front();
      cv::Mat nanTile( bt.h, bt.w, CV_32F, std::numeric_limits<float>::quiet_NaN() );
      for ( int band = 1; band <= stats.outBands; ++band )
      {
        const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                               currentTileIndex, totalTiles };
        if ( !writer->writeTile( band, writeTile, nanTile.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write NoData tile" );
      }
      // Same accounting convention as the single-input engine: a deferred
      // tile moves from "skipped" back to "done" once its NoData rows are on
      // disk, so tilesProcessed covers the whole raster.
      --skipped;
      ++done;
      deferredNoData.erase( deferredNoData.begin() );
    }
  };

  try
  {
    for ( int tileIndex = 0; tileIndex < totalTiles; ++tileIndex )
    {
      context.throwIfCancelled();
      const CoreTile &t = core[static_cast<std::size_t>( tileIndex )];
      const int winX = t.x - halo;
      const int winY = t.y - halo;
      // Uniform fed window: EVERY tile reads the full tileSize window with
      // the core at [halo, halo+core). Edge tiles extend past the raster and
      // are NaN/zero-filled by the read — fed sizes stay constant so a
      // batch can never mix geometries (the single-input engine refuses
      // mixed batches; here the geometry makes mixed batches unreachable).
      const int winW = tileSize + 2 * halo;
      const int winH = tileSize + 2 * halo;

      // Per-feed window reads for this tile. A core pixel is VALID when any
      // feed sees a finite value in any channel — NoData is only what nothing
      // sees. The mask is rebuilt from the prepared tile mats so the
      // halo/pad layout has a single source of truth.
      cv::Mat invalidMask( t.h, t.w, CV_8UC1, cv::Scalar( 1 ) );
      int validPixels = 0;
      std::vector<cv::Mat> tileMats( feeds.size() );
      for ( std::size_t f = 0; f < feeds.size(); ++f )
      {
        FeedReader &reader = readers[f];
        const int bandCount = static_cast<int>( reader.bands.size() );
        const std::size_t declaredFrames = reader.frames.size();
        // Stacked HWC buffer: (frames × bands) channels, zeros for missing
        // frames (missing_timestep=zero) and preprocessed data otherwise.
        std::vector<float> stacked( static_cast<std::size_t>( winH ) * winW * reader.channels, 0.0f );
        for ( std::size_t frame = 0; frame < declaredFrames; ++frame )
        {
          GdalDatasetWrapper *ds = reader.frames[frame].get();
          if ( !ds )
            continue; // missing frame stays zero-filled
          if ( !readBipWindow( *ds, reader.bands, winX, winY, winW, winH, reader.window.data() ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "failed to read tile window at (" + std::to_string( t.x ) + ", "
                                     + std::to_string( t.y ) + ") of feed '" + reader.contract->name
                                     + "'" );
          // Platform 8.0 WP-D quality mask: a 0 marks the pixel invalid for
          // THIS frame. Invalid pixels become NaN — exactly the NoData
          // convention — so they are excluded from the valid-coverage gate
          // AND zeroed for the forward by the shared preprocess path. The
          // pass covers the whole window (halo included) so context pixels
          // follow the same rule as core pixels.
          if ( frame < reader.qualityMasks.size() && reader.qualityMasks[frame] )
          {
            if ( reader.qualityWindow.size()
                 != static_cast<std::size_t>( winH ) * winW )
              reader.qualityWindow.assign( static_cast<std::size_t>( winH ) * winW, 1.0f );
            const std::vector<int> qualityBand{ 1 };
            if ( !readBipWindow( *reader.qualityMasks[frame], qualityBand, winX, winY, winW,
                                 winH, reader.qualityWindow.data() ) )
              throw RSOperatorError( ErrorCode::GdalError,
                                     "failed to read quality mask window at ("
                                       + std::to_string( t.x ) + ", " + std::to_string( t.y )
                                       + ") of feed '" + reader.contract->name + "'" );
            const std::size_t bandCountQ = reader.bands.size();
            float *windowData = reader.window.data();
            for ( int i = 0; i < winH * winW; ++i )
            {
              if ( reader.qualityWindow[static_cast<std::size_t>( i )] == 0.0f )
              {
                float *px = windowData + static_cast<std::size_t>( i ) * bandCountQ;
                for ( std::size_t c = 0; c < bandCountQ; ++c )
                  px[c] = std::numeric_limits<float>::quiet_NaN();
              }
            }
          }
          // Validity from the RAW window BEFORE preprocessing zero-fills it:
          // a core pixel is valid when any band of any frame of any feed sees
          // a finite non-sentinel value (NoData is only what nothing sees).
          {
            const std::size_t windowStride = static_cast<std::size_t>( winW ) * bandCount;
            for ( int row = 0; row < t.h; ++row )
            {
              const float *winRow = reader.window.data()
                                      + static_cast<std::size_t>( row + halo ) * windowStride
                                      + static_cast<std::size_t>( halo ) * bandCount;
              uchar *maskRow = invalidMask.ptr<uchar>( row );
              for ( int col = 0; col < t.w; ++col )
              {
                if ( !maskRow[col] )
                  continue;
                const float *px = winRow + static_cast<std::size_t>( col ) * bandCount;
                for ( int c = 0; c < bandCount; ++c )
                {
                  const float v = px[c];
                  if ( std::isfinite( v ) && !( reader.hasSentinel[static_cast<std::size_t>( c )]
                                                && v == reader.sentinel[static_cast<std::size_t>( c )] ) )
                  {
                    maskRow[col] = 0;
                    break;
                  }
                }
              }
            }
          }
          preprocessWindow( reader, reader.window, winW, winH );
          const std::size_t frameBase = frame * static_cast<std::size_t>( bandCount );
          for ( int row = 0; row < winH; ++row )
          {
            for ( int col = 0; col < winW; ++col )
            {
              for ( int c = 0; c < bandCount; ++c )
              {
                stacked[( static_cast<std::size_t>( row ) * winW + col ) * reader.channels
                        + frameBase + static_cast<std::size_t>( c )] =
                  reader.window[( static_cast<std::size_t>( row ) * winW + col ) * bandCount
                                 + static_cast<std::size_t>( c )];
              }
            }
          }
        }
        // Symmetric zero pad AFTER preprocessing (so the pad cannot be
        // normalized over): the fed window grows by pad on every side.
        cv::Mat stackedHwc( winH, winW, CV_32FC( reader.channels ), stacked.data() );
        cv::Mat fedMat = stackedHwc.clone();
        if ( pad > 0 )
        {
          cv::Mat padded( winH + 2 * pad, winW + 2 * pad, CV_32FC( reader.channels ),
                          cv::Scalar( 0.0 ) );
          stackedHwc.copyTo( padded( cv::Rect( pad, pad, winW, winH ) ) );
          fedMat = padded;
        }
        tileMats[f] = std::move( fedMat );
      }
      // Aggregate valid count for the coverage gate / all-nodata skip.
      for ( int row = 0; row < t.h; ++row )
      {
        const uchar *maskRow = invalidMask.ptr<uchar>( row );
        for ( int col = 0; col < t.w; ++col )
          if ( !maskRow[col] )
            ++validPixels;
      }

      // Per-tile coverage gate + all-nodata skip (Platform 7.0 / #705):
      // decided per TILE (a batch-level verdict would write valid output
      // tiles as NoData). Skipped tiles become NoData rows — never a
      // resolution change, never a model change.
      const double coverage =
        static_cast<double>( validPixels ) / static_cast<double>( std::max( 1, t.w * t.h ) );
      if ( validPixels == 0 || ( minCoverage > 0.0 && coverage < minCoverage ) )
      {
        deferredNoData.push_back( t );
        ++skipped;
        ++stats.tilesSkippedNoData;
        context.reportProgress(
          static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
          "Tiled multi-input inference: " + std::to_string( done + skipped ) + "/"
            + std::to_string( totalTiles ) );
        continue;
      }

      for ( std::size_t f = 0; f < feeds.size(); ++f )
        batchByFeed[f].push_back( tileMats[f] );
      batchCores.push_back( t );
      batchMasks.push_back( invalidMask.clone() );
      batchFedSize.emplace_back( winW + 2 * pad, winH + 2 * pad );
      batchValidPixels.push_back( validPixels );

      const bool batchFull =
        static_cast<int>( batchCores.size() ) >= batchSize || tileIndex == totalTiles - 1;
      if ( !batchFull )
        continue;

      // OOM ladder: identical semantics — halve to serial, never shrink
      // tiles, never change the model.
      try
      {
        flushBatch( tileIndex );
      }
      catch ( const RSOperatorError &e )
      {
        if ( classifyInferenceError( e.what() ) != InferenceFailureKind::OutOfMemory )
          throw;
        if ( batchCores.size() <= 1 )
          throw RSOperatorError(
            ErrorCode::ComputationError,
            std::string( "inference ran out of memory even at batch=1 (tile " )
              + std::to_string( tileSize )
              + " px): free memory or use a smaller model - the engine never alters spatial "
                "resolution or model semantics to fit memory. Original error: "
              + e.what() );
        ++stats.batchReductions;
        const std::vector<std::vector<cv::Mat>> pending = std::move( batchByFeed );
        const std::vector<cv::Mat> pendingMasks = std::move( batchMasks );
        const std::vector<std::pair<int, int>> pendingFed = std::move( batchFedSize );
        const std::vector<CoreTile> pendingCores = std::move( batchCores );
        const std::vector<int> pendingValid = std::move( batchValidPixels );
        batchByFeed.assign( feeds.size(), {} );
        batchMasks.clear();
        batchFedSize.clear();
        batchCores.clear();
        batchValidPixels.clear();
        for ( std::size_t i = 0; i < pendingCores.size(); ++i )
        {
          for ( std::size_t f = 0; f < feeds.size(); ++f )
            batchByFeed[f].assign( 1, pending[f][i] );
          batchMasks.assign( 1, pendingMasks[i] );
          batchFedSize.assign( 1, pendingFed[i] );
          batchCores.assign( 1, pendingCores[i] );
          batchValidPixels.assign( 1, pendingValid[i] );
          try
          {
            flushBatch( tileIndex );
          }
          catch ( const RSOperatorError &inner )
          {
            if ( classifyInferenceError( inner.what() ) == InferenceFailureKind::OutOfMemory )
              throw RSOperatorError(
                ErrorCode::ComputationError,
                "inference ran out of memory even at batch=1 (tile " + std::to_string( tileSize )
                  + " px): free memory or use a smaller model — the engine never alters spatial "
                    "resolution or model semantics to fit memory. Original error: "
                  + inner.what() );
            throw;
          }
        }
      }
      context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                              "Tiled multi-input inference: " + std::to_string( done + skipped )
                                + "/" + std::to_string( totalTiles ) );
    }

    if ( !deferredNoData.empty() && !writer )
    {
      // Whole-raster NoData: one zero probe establishes the output shape.
      const CoreTile probeTile = deferredNoData.front();
      deferredNoData.erase( deferredNoData.begin() );
      const int probeFedW = probeTile.w + 2 * halo + 2 * pad;
      const int probeFedH = probeTile.h + 2 * halo + 2 * pad;
      for ( std::size_t f = 0; f < feeds.size(); ++f )
        batchByFeed[f].push_back(
          cv::Mat( probeFedH, probeFedW, CV_32FC( readers[f].channels ), cv::Scalar( 0.0 ) ) );
      batchMasks.push_back( cv::Mat( probeTile.h, probeTile.w, CV_8UC1, cv::Scalar( 1 ) ) );
      batchFedSize.emplace_back( probeFedW, probeFedH );
      batchCores.push_back( probeTile );
      batchValidPixels.push_back( 0 );
      flushBatch( totalTiles - 1 );
    }
    flushDeferredNoDataMulti( totalTiles - 1 );
  }
  catch ( ... )
  {
    if ( writer )
      writer->removeOutput();
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
  const QString finalPath = QString::fromStdString( outputPath );
  const QString backupPath = finalPath + QStringLiteral( ".prev~" );
  QFile::remove( backupPath );
  const bool hadExisting = QFile::exists( finalPath );
  if ( hadExisting && !QFile::rename( finalPath, backupPath ) )
  {
    QFile::remove( stagePath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to back up the previous output before publishing: " + outputPath );
  }
  if ( !QFile::rename( stagePath, finalPath ) )
  {
    QFile::remove( stagePath );
    if ( hadExisting )
      QFile::rename( backupPath, finalPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to publish output raster to: " + outputPath );
  }
  // Platform 8.0: same provenance contract and ordering as the single-input
  // publish (old sidecar removed before the product lands; backup kept until
  // the new sidecar is in).
  QFile::remove( finalPath + QStringLiteral( ".prov.json" ) );
  stats.tilesProcessed = done; // counters finalized BEFORE the document build
  {
    const Json::Value prov = buildProvenanceDocument( m_model, m_runtime, stats,
                                                      "multi_input_probability_stack" );
    std::string provError;
    if ( !publishProvenanceSidecar( finalPath, outputPath, prov, &provError ) )
    {
      QFile::remove( finalPath );
      if ( hadExisting )
        QFile::rename( backupPath, finalPath ); // restore the previous good product
      throw RSOperatorError( ErrorCode::FileNotWritable, provError );
    }
  }
  QFile::remove( backupPath );
  context.reportProgressForced( 1.0, "Tiled multi-input inference complete" );
  stats.tilesProcessed = done;
  return stats;
}


int TileInferenceEngine::effectiveBatchSize( const ModelInfo &model,
                                             const ModelHardwareCapabilities &hw,
                                             int tilePx, int fedChannels )
{
  // Manifest request first; the budget can only lower it.
  int batch = std::max( 1, model.tiling.batchSize );
  const int tile = std::max( kMinTileSize, tilePx );
  // Per-sample working set: input blob + output planes + halo overhead.
  const unsigned long long perSampleBytes =
    4ULL * static_cast<unsigned long long>( tile ) * tile
      * static_cast<unsigned long long>( std::max( 1, fedChannels ) ) * 4ULL;
  // GPU VRAM budget (when reported via SICNU_MODEL_VRAM_MB) is the only
  // enforced ceiling here; CPU-side RAM admission is owned by TaskCenter's
  // resource budget fed by estimateExecution — this function deliberately
  // does not guess a RAM share.
  unsigned long long budgetBytes = 0;
  if ( hw.vramBudgetMb > 0 )
    budgetBytes = static_cast<unsigned long long>( hw.vramBudgetMb ) * 1024ULL * 1024ULL;
  if ( budgetBytes > 0 && perSampleBytes > 0 )
  {
    const int budgetBatch = static_cast<int>( std::max<unsigned long long>(
      1, budgetBytes / std::max<unsigned long long>( perSampleBytes, 1 ) ) );
    batch = std::min( batch, budgetBatch );
  }
  return std::max( 1, batch );
}

std::string TileInferenceEngine::uncertaintyMethod( const ModelInfo &model )
{
  const std::string &u = model.output.uncertainty;
  if ( u.empty() || u == "none" )
    return {};
  if ( u == "entropy" || u == "margin" )
    return u;
  return {}; // unknown tokens are rejected at manifest parse
}

RasterOutputMode TileInferenceEngine::rasterOutputMode( const ModelInfo &model )
{
  const std::string &f = model.output.format;
  if ( f.empty() || f == "probability" )
    return RasterOutputMode::Probability;
  if ( f == "labels" )
    return RasterOutputMode::Labels;
  if ( f == "mask" )
    return RasterOutputMode::Mask;
  if ( f == "confidence" )
    return RasterOutputMode::Confidence;
  // Unknown formats are rejected at manifest parse; this is defense in depth.
  throw RSOperatorError( ErrorCode::InvalidInputData,
                         "unsupported output.format '" + f
                           + "' (supported: probability, labels, mask, confidence)" );
}

cv::Mat TileInferenceEngine::headUncertainty( const std::vector<cv::Mat> &classPlanes,
                                              const std::string &method )
{
  const int channels = static_cast<int>( classPlanes.size() );
  if ( channels < 2 || classPlanes.front().empty() )
    return cv::Mat();
  const int rows = classPlanes.front().rows;
  const int cols = classPlanes.front().cols;
  cv::Mat result( rows, cols, CV_32F );

  if ( method == "margin" )
  {
    // Probabilities via softmax first, then top1 − top2 gap.
    for ( int r = 0; r < rows; ++r )
    {
      float *outRow = result.ptr<float>( r );
      for ( int c = 0; c < cols; ++c )
      {
        float maxLogit = -std::numeric_limits<float>::infinity();
        for ( const cv::Mat &plane : classPlanes )
          maxLogit = std::max( maxLogit, plane.at<float>( r, c ) );
        double sum = 0.0;
        double top1 = 0.0;
        double top2 = 0.0;
        for ( const cv::Mat &plane : classPlanes )
        {
          const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit );
          sum += p;
        }
        for ( const cv::Mat &plane : classPlanes )
        {
          const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit ) / sum;
          if ( p > top1 )
          {
            top2 = top1;
            top1 = p;
          }
          else if ( p > top2 )
          {
            top2 = p;
          }
        }
        outRow[c] = static_cast<float>( top1 - top2 );
      }
    }
    return result;
  }

  // Default: softmax entropy in [0, ln C].
  const double logC = std::log( static_cast<double>( channels ) );
  for ( int r = 0; r < rows; ++r )
  {
    float *outRow = result.ptr<float>( r );
    for ( int c = 0; c < cols; ++c )
    {
      float maxLogit = -std::numeric_limits<float>::infinity();
      for ( const cv::Mat &plane : classPlanes )
        maxLogit = std::max( maxLogit, plane.at<float>( r, c ) );
      double sum = 0.0;
      for ( const cv::Mat &plane : classPlanes )
        sum += std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit );
      double entropy = 0.0;
      for ( const cv::Mat &plane : classPlanes )
      {
        const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit ) / sum;
        if ( p > 0.0 )
          entropy -= p * std::log( p );
      }
      outRow[c] = static_cast<float>( entropy / logC ); // normalized [0, 1]
    }
  }
  return result;
}

} // namespace sicnu::operators::runtime
