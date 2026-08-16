// rs_training_data_extraction.cpp — see header for design notes.
#include "rs_training_data_extraction.h"

#include "rs_classification_utils.h"
#include "rs_pixel_rasterizer.h"

#include <gdal_priv.h>
#include <ogr_api.h>

#include <algorithm>
#include <map>
#include <random>
#include <utility>
#include <vector>

namespace
{

bool reportProgress( const RsTrainingDataExtraction::Progress &progress,
                     double fraction, const QString &message )
{
  return !progress || progress( fraction, message );
}

/**
 * Dedup geometries into a pixel → class map (last class wins), using each
 * entry's pixel-index cache when present and the windowed rasterizer
 * otherwise. Returns false when cancelled via the progress sink.
 */
bool collectPixels( const QVector<RsTrainingGeometry> &geometries,
                    const double gt[6], int W, int H,
                    QHash<quint64, int> &pixelClass,
                    const RsTrainingDataExtraction::Progress &progress )
{
  const quint64 nPix = static_cast<quint64>( W ) * static_cast<quint64>( H );
  for ( int i = 0; i < geometries.size(); ++i )
  {
    if ( !reportProgress( progress,
                          0.4 * static_cast<double>( i ) / geometries.size(),
                          QStringLiteral( "Rasterizing training geometries" ) ) )
      return false;

    const RsTrainingGeometry &tg = geometries[i];
    if ( tg.classId <= 0 )
      continue;
    QVector<quint64> idx = tg.pixelIndices;
    if ( idx.isEmpty() )
    {
      const QSet<quint64> px = RsPixelRasterizer::rasterize( tg.geometry, gt, W, H );
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    for ( quint64 p : idx )
    {
      if ( p < nPix )
        pixelClass.insert( p, tg.classId );
    }
  }
  return true;
}

/**
 * Shared tail: sample list (+ optional per-class cap), scanline-grouped band
 * reads, NoData/ignore filtering and the minimum-samples policy.
 */
void buildMatrices( GDALDataset *ds,
                    const QVector<int> &bands,
                    const QHash<quint64, int> &pixelClass,
                    const RsTrainingDataExtraction::Options &options,
                    const RsTrainingDataExtraction::Progress &progress,
                    RsTrainingDataResult &out )
{
  const int W = ds->GetRasterXSize();
  const int B = bands.size();

  // Flatten to (classId, pixelIdx). With a per-class cap, bucket pixels by
  // class (ascending pixel order for determinism) and subsample each bucket
  // with the shared mt19937(42) sequence; otherwise keep the hash iteration
  // order (matches the historical classification-window behavior).
  QVector<QPair<int, quint64>> samples;
  if ( options.maxSamplesPerClass > 0 )
  {
    std::map<int, std::vector<quint64>> byClass;
    for ( auto it = pixelClass.constBegin(); it != pixelClass.constEnd(); ++it )
      byClass[it.value()].push_back( it.key() );

    // ADR 0061 — shared deterministic subsampling policy (mt19937(seed) +
    // shuffle, keep the first maxSamplesPerClass of each sorted bucket).
    std::mt19937 rng( options.seed );
    for ( auto &kv : byClass )
    {
      std::vector<quint64> &px = kv.second;
      std::sort( px.begin(), px.end() );
      rsShuffleAndKeep( rng, px, static_cast<size_t>( options.maxSamplesPerClass ) );
      for ( quint64 p : px )
        samples.push_back( qMakePair( kv.first, p ) );
    }
  }
  else
  {
    samples.reserve( pixelClass.size() );
    for ( auto it = pixelClass.constBegin(); it != pixelClass.constEnd(); ++it )
      samples.push_back( qMakePair( it.value(), it.key() ) );
  }

  if ( samples.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage = QStringLiteral( "No valid training pixels extracted" );
    return;
  }

  // Group sample columns by row so each unique row is read once per band
  // (scanline) instead of 1×1 RasterIO.
  QHash<int, QVector<QPair<int, int>>> byRow;
  byRow.reserve( samples.size() );
  for ( int s = 0; s < samples.size(); ++s )
  {
    const quint64 idx = samples[s].second;
    const int r = static_cast<int>( idx / static_cast<quint64>( W ) );
    const int c = static_cast<int>( idx % static_cast<quint64>( W ) );
    byRow[r].append( qMakePair( s, c ) );
  }

  out.X.create( samples.size(), B, CV_32F );
  out.y.create( samples.size(), 1, CV_32S );
  for ( int s = 0; s < samples.size(); ++s )
    out.y.at<int>( s, 0 ) = samples[s].first;

  std::vector<float> rowBuf( static_cast<size_t>( W ) );
  for ( int bi = 0; bi < B; ++bi )
  {
    GDALRasterBand *band = ds->GetRasterBand( bands[bi] );
    for ( auto it = byRow.constBegin(); it != byRow.constEnd(); ++it )
    {
      const int r = it.key();
      const CPLErr err = band->RasterIO(
        GF_Read, 0, r, W, 1, rowBuf.data(),
        W, 1, GDT_Float32, 0, 0 );
      if ( err != CE_None )
      {
        out.error = RsTrainingDataResult::Error::RasterReadFailed;
        out.errorMessage = QStringLiteral( "Failed to read raster band %1" ).arg( bands[bi] );
        out.X.release();
        out.y.release();
        return;
      }
      for ( const QPair<int, int> &sc : it.value() )
        out.X.at<float>( sc.first, bi ) = rowBuf[static_cast<size_t>( sc.second )];
    }
    if ( !reportProgress( progress,
                          0.4 + 0.5 * static_cast<double>( bi + 1 ) / B,
                          QStringLiteral( "Reading training samples" ) ) )
    {
      out.error = RsTrainingDataResult::Error::Cancelled;
      out.errorMessage = QStringLiteral( "Cancelled" );
      out.X.release();
      out.y.release();
      return;
    }
  }

  // Drop samples that fall on NoData / user ignore values (edge/background).
  // ADR 0061 — per-band NoData discovery is owned by rsCollectBandNodata
  // (shared with the pipeline tile path).
  const RsPixelIgnoreOptions &ignore = options.ignore;
  std::vector<bool> bandHasNodata;
  std::vector<float> bandNodata;
  rsCollectBandNodata( ds, bands, ignore, bandHasNodata, bandNodata );

  std::vector<float> feat( static_cast<size_t>( B ) );
  QVector<int> keepRows;
  keepRows.reserve( samples.size() );
  for ( int s = 0; s < samples.size(); ++s )
  {
    for ( int bi = 0; bi < B; ++bi )
      feat[static_cast<size_t>( bi )] = out.X.at<float>( s, bi );
    if ( !ignore.isIgnorePixel( feat.data(), B, bandHasNodata, bandNodata ) )
      keepRows.push_back( s );
  }

  if ( keepRows.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage = QStringLiteral( "No valid training pixels extracted" );
    out.X.release();
    out.y.release();
    return;
  }

  if ( keepRows.size() < options.minSamples )
  {
    out.error = RsTrainingDataResult::Error::InsufficientSamples;
    out.errorMessage = QStringLiteral( "Insufficient training samples (%1 kept, %2 required)" )
                         .arg( keepRows.size() ).arg( options.minSamples );
    out.X.release();
    out.y.release();
    return;
  }

  if ( keepRows.size() != samples.size() )
  {
    cv::Mat X2( keepRows.size(), B, CV_32F );
    cv::Mat y2( keepRows.size(), 1, CV_32S );
    for ( int i = 0; i < keepRows.size(); ++i )
    {
      out.X.row( keepRows[i] ).copyTo( X2.row( i ) );
      y2.at<int>( i, 0 ) = out.y.at<int>( keepRows[i], 0 );
    }
    out.X = X2;
    out.y = y2;
  }

  for ( int s = 0; s < out.y.rows; ++s )
    out.classCounts[out.y.at<int>( s, 0 )]++;

  reportProgress( progress, 1.0, QStringLiteral( "Training samples ready" ) );
  out.ok = true;
}

/// Open `rasterPath` read-only and validate 1-based band indices.
GDALDataset *openRaster( const QString &rasterPath,
                         const QVector<int> &bands,
                         RsTrainingDataResult &out )
{
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
  {
    out.error = RsTrainingDataResult::Error::RasterOpenFailed;
    out.errorMessage = QStringLiteral( "Failed to open raster: %1" ).arg( rasterPath );
    return nullptr;
  }
  const int nBands = ds->GetRasterCount();
  for ( int b : bands )
  {
    if ( b < 1 || b > nBands )
    {
      out.error = RsTrainingDataResult::Error::InvalidBand;
      out.errorMessage = QStringLiteral( "Band %1 out of range (1-%2)" ).arg( b ).arg( nBands );
      GDALClose( ds );
      return nullptr;
    }
  }
  return ds;
}

} // namespace

RsTrainingDataExtraction::Options::Options()
  : maxSamplesPerClass( 0 ), minSamples( 0 ), seed( 42u )
{
}

RsTrainingDataResult RsTrainingDataExtraction::extract(
  const QString &rasterPath,
  const QVector<int> &bands,
  const QVector<RsTrainingGeometry> &geometries,
  const Options &options,
  const Progress &progress )
{
  RsTrainingDataResult out;
  if ( rasterPath.isEmpty() || bands.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::InvalidBand;
    out.errorMessage = QStringLiteral( "Empty raster path or band list" );
    return out;
  }

  GDALDataset *ds = openRaster( rasterPath, bands, out );
  if ( !ds )
    return out;

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  double gt[6] = { 0, 1, 0, 0, 0, 1 };
  ds->GetGeoTransform( gt );

  QHash<quint64, int> pixelClass;
  if ( !collectPixels( geometries, gt, W, H, pixelClass, progress ) )
  {
    out.error = RsTrainingDataResult::Error::Cancelled;
    out.errorMessage = QStringLiteral( "Cancelled" );
    GDALClose( ds );
    return out;
  }

  buildMatrices( ds, bands, pixelClass, options, progress, out );
  GDALClose( ds );
  return out;
}

int RsTrainingDataExtraction::classFieldIndex( OGRFeatureDefnH defn,
                                               const QString &classField )
{
  int fieldIdx = OGR_FD_GetFieldIndex( defn, classField.toUtf8().constData() );
  if ( fieldIdx < 0 )
    fieldIdx = OGR_FD_GetFieldIndex( defn, "class" );
  if ( fieldIdx < 0 )
    fieldIdx = OGR_FD_GetFieldIndex( defn, "id" );
  return fieldIdx;
}

RsTrainingDataResult RsTrainingDataExtraction::extractFromVector(
  const QString &rasterPath,
  const QVector<int> &bands,
  const QString &vectorPath,
  const QString &classField,
  const Options &options,
  const Progress &progress )
{
  RsTrainingDataResult out;
  if ( rasterPath.isEmpty() || bands.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::InvalidBand;
    out.errorMessage = QStringLiteral( "Empty raster path or band list" );
    return out;
  }

  GDALAllRegister();
  OGRRegisterAll();

  GDALDatasetH vecDs = GDALOpenEx( vectorPath.toUtf8().constData(),
                                   GDAL_OF_VECTOR, nullptr, nullptr, nullptr );
  if ( !vecDs )
  {
    out.error = RsTrainingDataResult::Error::VectorOpenFailed;
    out.errorMessage = QStringLiteral( "Failed to open training vector: %1" ).arg( vectorPath );
    return out;
  }

  OGRLayerH layer = GDALDatasetGetLayer( vecDs, 0 );
  if ( !layer )
  {
    out.error = RsTrainingDataResult::Error::VectorNoLayers;
    out.errorMessage = QStringLiteral( "Training dataset has no layers" );
    GDALClose( vecDs );
    return out;
  }

  OGRFeatureDefnH defn = OGR_L_GetLayerDefn( layer );
  const int fieldIdx = classFieldIndex( defn, classField );
  if ( fieldIdx < 0 )
  {
    out.error = RsTrainingDataResult::Error::ClassFieldNotFound;
    out.errorMessage = QStringLiteral( "Class field not found: %1 (also tried 'class', 'id')" )
                         .arg( classField );
    GDALClose( vecDs );
    return out;
  }

  QVector<RsTrainingGeometry> geometries;
  OGR_L_ResetReading( layer );
  OGRFeatureH feat = nullptr;
  bool cancelled = false;
  while ( ( feat = OGR_L_GetNextFeature( layer ) ) != nullptr )
  {
    ++out.featuresRead;
    if ( !reportProgress( progress, 0.0,
                          QStringLiteral( "Reading training features" ) ) )
    {
      cancelled = true;
      OGR_F_Destroy( feat );
      break;
    }

    const int classId = OGR_F_GetFieldAsInteger( feat, fieldIdx );
    if ( classId <= 0 )
    {
      OGR_F_Destroy( feat );
      continue;
    }

    OGRGeometryH geom = OGR_F_GetGeometryRef( feat );
    if ( geom )
    {
      const int wkbSize = OGR_G_WkbSize( geom );
      QByteArray wkb( wkbSize, 0 );
      if ( OGR_G_ExportToWkb( geom, wkbNDR,
                              reinterpret_cast<unsigned char *>( wkb.data() ) ) == OGRERR_NONE )
      {
        RsTrainingGeometry tg;
        tg.classId = classId;
        tg.geometry.fromWkb( wkb );
        geometries.push_back( tg );
      }
    }
    OGR_F_Destroy( feat );
  }
  GDALClose( vecDs );

  if ( cancelled )
  {
    out.error = RsTrainingDataResult::Error::Cancelled;
    out.errorMessage = QStringLiteral( "Cancelled" );
    return out;
  }

  GDALDataset *ds = openRaster( rasterPath, bands, out );
  if ( !ds )
    return out;

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  double gt[6] = { 0, 1, 0, 0, 0, 1 };
  ds->GetGeoTransform( gt );

  QHash<quint64, int> pixelClass;
  if ( !collectPixels( geometries, gt, W, H, pixelClass, progress ) )
  {
    out.error = RsTrainingDataResult::Error::Cancelled;
    out.errorMessage = QStringLiteral( "Cancelled" );
    GDALClose( ds );
    return out;
  }

  buildMatrices( ds, bands, pixelClass, options, progress, out );
  GDALClose( ds );
  return out;
}
