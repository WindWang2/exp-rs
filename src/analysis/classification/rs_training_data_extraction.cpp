// rs_training_data_extraction.cpp — see header for design notes.
#include "rs_training_data_extraction.h"

#include "rs_classification_utils.h"
#include "rs_pixel_rasterizer.h"

#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>

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
                    QHash<quint64, int> &pixelGroup,
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
      {
        pixelClass.insert( p, tg.classId );
        pixelGroup.insert( p, i );
      }
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
                    const QHash<quint64, int> &pixelGroup,
                    const RsTrainingDataExtraction::Options &options,
                    const RsTrainingDataExtraction::Progress &progress,
                    RsTrainingDataResult &out )
{
  const int W = ds->GetRasterXSize();
  const int B = bands.size();

  // Flatten to (classId, pixelIdx) + parallel group ids. With a per-class
  // cap, bucket pixels by class (ascending pixel order for determinism) and
  // subsample each bucket with the shared mt19937(seed) sequence; otherwise
  // keep all pixels in sorted (classId, pixelIdx) order so the row order
  // is reproducible (QHash iteration is per-process random).
  QVector<QPair<int, quint64>> samples;
  std::vector<int> sampleGroups;
  sampleGroups.reserve( pixelClass.size() );
  if ( options.maxSamplesPerClass > 0 )
  {
    std::map<int, std::vector<std::pair<quint64, int>>> byClass;
    for ( auto it = pixelClass.constBegin(); it != pixelClass.constEnd(); ++it )
    {
      const quint64 p = it.key();
      const int grp = pixelGroup.value( p, -1 );
      byClass[it.value()].emplace_back( p, grp );
    }

    std::mt19937 rng( options.seed );
    for ( auto &kv : byClass )
    {
      auto &vec = kv.second;
      std::sort( vec.begin(), vec.end(),
                 []( const auto &a, const auto &b ) { return a.first < b.first; } );
      // Shuffle the pair vector, then truncate
      std::shuffle( vec.begin(), vec.end(), rng );
      if ( vec.size() > static_cast<size_t>( options.maxSamplesPerClass ) )
        vec.resize( static_cast<size_t>( options.maxSamplesPerClass ) );
      std::sort( vec.begin(), vec.end(),
                 []( const auto &a, const auto &b ) { return a.first < b.first; } );
      for ( auto &pr : vec )
      {
        samples.push_back( qMakePair( kv.first, pr.first ) );
        sampleGroups.push_back( pr.second );
      }
    }
  }
  else
  {
    samples.reserve( pixelClass.size() );
    sampleGroups.reserve( pixelClass.size() );
    for ( auto it = pixelClass.constBegin(); it != pixelClass.constEnd(); ++it )
    {
      samples.push_back( qMakePair( it.value(), it.key() ) );
      sampleGroups.push_back( pixelGroup.value( it.key(), -1 ) );
    }
    // Sort samples + groups together by (classId, pixelIdx)
    QVector<int> order( samples.size() );
    for ( int i = 0; i < order.size(); ++i )
      order[i] = i;
    std::sort( order.begin(), order.end(),
               [&]( int a, int b ) { return samples[a] < samples[b]; } );
    QVector<QPair<int, quint64>> sortedSamples;
    std::vector<int> sortedGroups;
    sortedSamples.reserve( samples.size() );
    sortedGroups.reserve( sampleGroups.size() );
    for ( int idx : order )
    {
      sortedSamples.push_back( samples[idx] );
      sortedGroups.push_back( sampleGroups[static_cast<size_t>( idx )] );
    }
    samples = sortedSamples;
    sampleGroups = sortedGroups;
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

  // Build per-sample group id vector before filtering
  std::vector<int> allGroupIds = sampleGroups;
  if ( keepRows.size() != samples.size() )
  {
    cv::Mat X2( keepRows.size(), B, CV_32F );
    cv::Mat y2( keepRows.size(), 1, CV_32S );
    std::vector<int> keptGroups;
    keptGroups.reserve( keepRows.size() );
    for ( int i = 0; i < keepRows.size(); ++i )
    {
      out.X.row( keepRows[i] ).copyTo( X2.row( i ) );
      y2.at<int>( i, 0 ) = out.y.at<int>( keepRows[i], 0 );
      keptGroups.push_back( allGroupIds[static_cast<size_t>( keepRows[i] )] );
    }
    out.X = X2;
    out.y = y2;
    out.sampleGroupIds = std::move( keptGroups );
  }
  else
  {
    out.sampleGroupIds = std::move( allGroupIds );
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
  QHash<quint64, int> pixelGroup;
  if ( !collectPixels( geometries, gt, W, H, pixelClass, pixelGroup, progress ) )
  {
    out.error = RsTrainingDataResult::Error::Cancelled;
    out.errorMessage = QStringLiteral( "Cancelled" );
    GDALClose( ds );
    return out;
  }

  buildMatrices( ds, bands, pixelClass, pixelGroup, options, progress, out );
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

  OGRSpatialReferenceH vecSrs = OGR_L_GetSpatialRef( layer );
  OGRCoordinateTransformationH coordTrans = nullptr;

  GDALDatasetH rasDs = GDALOpen( rasterPath.toUtf8().constData(), GA_ReadOnly );
  if ( rasDs )
  {
    const char *rasWkt = GDALGetProjectionRef( rasDs );
    if ( rasWkt && std::strlen( rasWkt ) > 0 && vecSrs )
    {
      OGRSpatialReferenceH rasSrs = OSRNewSpatialReference( nullptr );
      if ( OSRImportFromWkt( rasSrs, const_cast<char **>( &rasWkt ) ) == OGRERR_NONE )
      {
        if ( !OSRIsSame( vecSrs, rasSrs ) )
        {
          coordTrans = OCTNewCoordinateTransformation( vecSrs, rasSrs );
        }
      }
      OSRDestroySpatialReference( rasSrs );
    }
    GDALClose( rasDs );
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
      OGRGeometryH geomToUse = geom;
      OGRGeometryH clonedGeom = nullptr;
      if ( coordTrans )
      {
        clonedGeom = OGR_G_Clone( geom );
        if ( OGR_G_Transform( clonedGeom, coordTrans ) == OGRERR_NONE )
        {
          geomToUse = clonedGeom;
        }
      }

      const int wkbSize = OGR_G_WkbSize( geomToUse );
      QByteArray wkb( wkbSize, 0 );
      if ( OGR_G_ExportToWkb( geomToUse, wkbNDR,
                              reinterpret_cast<unsigned char *>( wkb.data() ) ) == OGRERR_NONE )
      {
        RsTrainingGeometry tg;
        tg.classId = classId;
        tg.geometry.fromWkb( wkb );
        geometries.push_back( tg );
      }
      if ( clonedGeom )
        OGR_G_DestroyGeometry( clonedGeom );
    }
    OGR_F_Destroy( feat );
  }
  GDALClose( vecDs );
  if ( coordTrans )
    OCTDestroyCoordinateTransformation( coordTrans );

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
  QHash<quint64, int> pixelGroup;
  if ( !collectPixels( geometries, gt, W, H, pixelClass, pixelGroup, progress ) )
  {
    out.error = RsTrainingDataResult::Error::Cancelled;
    out.errorMessage = QStringLiteral( "Cancelled" );
    GDALClose( ds );
    return out;
  }

  buildMatrices( ds, bands, pixelClass, pixelGroup, options, progress, out );
  GDALClose( ds );
  return out;
}
