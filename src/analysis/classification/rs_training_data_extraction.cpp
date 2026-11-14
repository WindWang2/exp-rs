// rs_training_data_extraction.cpp — see header for design notes.
#include "rs_training_data_extraction.h"

#include "rs_classification_utils.h"
#include "rs_pixel_rasterizer.h"
#include "sicnu_logging.h"

#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <limits>
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

/// Outcome of the geometry → pixel-index collection pass.
enum class CollectStatus
{
  Ok,
  Cancelled,
  RasterizeFailed,
};

/**
 * R29-T01 (WP04): pixel → (classId, geometryIndex) in ONE hash. The two
 * former same-key hashes (pixelClass + pixelGroup) doubled the per-candidate
 * node+bucket overhead of the candidate index; every insert touched both,
 * so the pair was always consistent and merges losslessly (last geometry
 * wins for both fields simultaneously).
 */
using PixelOwner = QPair<int, int>; // (classId, geometryIndex)

/**
 * Dedup geometries into the pixel → owner map (last geometry wins), using
 * each entry's pixel-index cache when present and the windowed rasterizer
 * otherwise. Returns Cancelled when cancelled via the progress sink and
 * RasterizeFailed when the rasterizer reports an infrastructure failure —
 * a failed geometry must never silently drop out of the training set
 * (#1056), so there is no "skip and continue" path here.
 * R29-T01 (WP05): the per-geometry pixel loop carries a bounded-work
 * cancellation checkpoint every kCancelCheckPixels pixels — one ROI covering
 * a whole image used to be uninterruptible inside a single geometry.
 */
CollectStatus collectPixels( const QVector<RsTrainingGeometry> &geometries,
                             const double gt[6], int W, int H,
                             QHash<quint64, PixelOwner> &pixelOwner,
                             const RsTrainingDataExtraction::Progress &progress,
                             const RsTrainingDataExtraction::Options &options,
                             int &failedGeometryIndex )
{
  // Checkpoint spacing: a counter compare per pixel, one sink call per 64k
  // pixels — measured-noise-free against the per-pixel hash insert while
  // bounding the uninterruptible stretch to 64k inserts (~sub-millisecond).
  constexpr quint64 kCancelCheckPixels = 65536;
  const quint64 nPix = static_cast<quint64>( W ) * static_cast<quint64>( H );
  for ( int i = 0; i < geometries.size(); ++i )
  {
    if ( !reportProgress( progress,
                          0.4 * static_cast<double>( i ) / geometries.size(),
                          QStringLiteral( "Rasterizing training geometries" ) ) )
      return CollectStatus::Cancelled;

    const RsTrainingGeometry &tg = geometries[i];
    if ( tg.classId <= 0 )
      continue;
    QVector<quint64> idx = tg.pixelIndices;
    if ( idx.isEmpty() )
    {
      bool rasterizeOk = false;
      bool rasterizeCancelled = false;
      // R30-T01 (WP04): the burn is cancellable mid-geometry and its MEM
      // band is bounded by Options::rasterizerWindowPixels — one huge ROI
      // no longer allocates its whole bbox or blocks the cancel sink.
      const QSet<quint64> px = RsPixelRasterizer::rasterize(
        tg.geometry, gt, W, H, &rasterizeOk,
        [&progress, i, n = geometries.size()]() {
          return !reportProgress( progress,
                                  0.4 * static_cast<double>( i ) / n,
                                  QStringLiteral( "Rasterizing training geometries" ) );
        },
        &rasterizeCancelled, options.rasterizerWindowPixels );
      if ( rasterizeCancelled )
        return CollectStatus::Cancelled;
      if ( !rasterizeOk )
      {
        failedGeometryIndex = i;
        return CollectStatus::RasterizeFailed;
      }
      idx = QVector<quint64>( px.begin(), px.end() );
    }
    quint64 sinceCheck = 0;
    for ( quint64 p : idx )
    {
      if ( p < nPix )
        pixelOwner.insert( p, PixelOwner( tg.classId, i ) );
      if ( ++sinceCheck >= kCancelCheckPixels )
      {
        sinceCheck = 0;
        if ( !reportProgress( progress,
                              0.4 * static_cast<double>( i ) / geometries.size(),
                              QStringLiteral( "Rasterizing training geometries" ) ) )
          return CollectStatus::Cancelled;
      }
    }
  }
  return CollectStatus::Ok;
}

/**
 * Shared tail: sample list (+ optional per-class cap), scanline-grouped band
 * reads, NoData/ignore filtering and the minimum-samples policy.
 *
 * R28-T01 memory contract: the feature matrix is allocated for the KEPT
 * rows only. Validity is established in a first scanline pass that records
 * per-candidate ignore matches (NoData/ignore/non-finite per band —
 * RsPixelIgnoreOptions::isIgnorePixel decomposes exactly into per-band
 * isIgnoreScalar calls), and a second pass re-reads only the rows that
 * still carry kept samples. Previously out.X was allocated for every ROI
 * candidate (N × B floats, ≈1.2 GB at 100 Mpx × 3 bands) before filtering
 * and the per-class cap (r26 PERF_BASELINE residual).
 *
 * R29-T01 (WP04) candidate-index contract: ONE candidate record per pixel
 * (class, pixel, group, shuffle rank — the rank is only meaningful on the
 * capped path), sorted IN PLACE by (classId, pixelIdx). The former layout
 * kept two same-key pixel hashes plus three parallel per-sample arrays and
 * an index-sort with whole-vector copies on the uncapped path; the output
 * (kept set, row order, groups, rng consumption) is bit-identical.
 */
struct SampleCandidate
{
  quint64 pixel = 0;
  int classId = 0;
  int group = -1;
  int rank = 0; // capped path: position in the class shuffle order
};

void buildMatrices( GDALDataset *ds,
                    const QVector<int> &bands,
                    const QHash<quint64, PixelOwner> &pixelOwner,
                    const RsTrainingDataExtraction::Options &options,
                    const RsTrainingDataExtraction::Progress &progress,
                    RsTrainingDataResult &out )
{
  const int W = ds->GetRasterXSize();
  const int B = bands.size();

  // R30-T01 (WP05): admission BEFORE the candidate vector / feature matrix
  // is built (they scale with the pixel set and dominate memory beyond it).
  // The per-pixel owner hash itself is bounded only by the raster size and
  // is already materialized at this point — the budget does not (and cannot)
  // shrink that allocation.
  if ( options.maxCandidates > 0
       && static_cast<qint64>( pixelOwner.size() ) > options.maxCandidates )
  {
    out.error = RsTrainingDataResult::Error::BudgetExceeded;
    out.errorMessage =
      QStringLiteral( "Training ROI set holds %1 candidate pixels (budget %2); "
                      "narrow the ROIs, raise maxCandidates, or lower the per-class cap" )
        .arg( pixelOwner.size() )
        .arg( options.maxCandidates );
    return;
  }
  // The candidate vector and the feature matrix are int-indexed (cv::Mat
  // rows are int); refuse past INT_MAX instead of truncating the size cast.
  if ( static_cast<qint64>( pixelOwner.size() ) > std::numeric_limits<int>::max() )
  {
    out.error = RsTrainingDataResult::Error::BudgetExceeded;
    out.errorMessage = QStringLiteral( "Training ROI set holds %1 candidate pixels — beyond the "
                                       "int-indexed feature matrix limit (%2)" )
                         .arg( pixelOwner.size() )
                         .arg( std::numeric_limits<int>::max() );
    return;
  }

  // Flatten to candidates. With a per-class cap, bucket pixels by class
  // (ascending pixel order for determinism) and subsample each bucket with
  // the shared mt19937(seed) sequence; otherwise keep all pixels in sorted
  // (classId, pixelIdx) order so the row order is reproducible (QHash
  // iteration is per-process random).
  std::vector<SampleCandidate> samples;
  samples.reserve( static_cast<size_t>( pixelOwner.size() ) );
  if ( options.maxSamplesPerClass > 0 )
  {
    std::map<int, std::vector<std::pair<quint64, int>>> byClass;
    for ( auto it = pixelOwner.constBegin(); it != pixelOwner.constEnd(); ++it )
      byClass[it.value().first].emplace_back( it.key(), it.value().second );

    std::mt19937 rng( options.seed );
    for ( auto &kv : byClass )
    {
      auto &vec = kv.second;
      std::sort( vec.begin(), vec.end(),
                 []( const auto &a, const auto &b ) { return a.first < b.first; } );
      // Shuffle the pair vector. Portable Fisher-Yates (#1685): std::shuffle
      // is implementation-defined, which would make the kept sample subset
      // differ across STLs for the same seed. No truncation here — the cap
      // moves after NoData filtering (R26-S2).
      rs::algorithms::portable::fisherYatesShuffle( rng, vec );
      // Each pixel carries its shuffle rank; the cap below keeps the first
      // maxSamplesPerClass VALID pixels in this order (R26-S2).
      for ( size_t k = 0; k < vec.size(); ++k )
        samples.push_back( SampleCandidate{ vec[k].first, kv.first, vec[k].second,
                                            static_cast<int>( k ) } );
    }
  }
  else
  {
    for ( auto it = pixelOwner.constBegin(); it != pixelOwner.constEnd(); ++it )
      samples.push_back(
        SampleCandidate{ it.key(), it.value().first, it.value().second, 0 } );
  }
  // One in-place sort by (classId, pixelIdx) covers both branches: pixels
  // are unique, so the order is a strict total order and the result equals
  // the historical per-class ascending layout (std::map iterates classes
  // ascending; the pair sort inside each class is fused into this pass).
  std::sort( samples.begin(), samples.end(), []( const SampleCandidate &a,
                                                 const SampleCandidate &b ) {
    if ( a.classId != b.classId )
      return a.classId < b.classId;
    return a.pixel < b.pixel;
  } );

  if ( samples.empty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage = QStringLiteral( "No valid training pixels extracted" );
    return;
  }

  // Group sample columns by row so each unique row is read once per band
  // (scanline) instead of 1×1 RasterIO. R30-T01 (WP06): each row also
  // records its [minCol, maxCol] span so the read covers only the columns
  // the row's samples actually occupy — on a wide raster with sparse ROIs
  // the per-row read used to drag the whole W-wide scanline (per band, per
  // pass) for a handful of samples. Sample membership is untouched, so the
  // computed values are identical.
  struct RowGroup
  {
    QVector<QPair<int, int>> samples; // (sample index, column)
    int minCol = 0;
    int maxCol = 0;
  };
  QHash<int, RowGroup> byRow;
  byRow.reserve( static_cast<int>( samples.size() ) );
  for ( int s = 0; s < static_cast<int>( samples.size() ); ++s )
  {
    const quint64 idx = samples[static_cast<size_t>( s )].pixel;
    const int r = static_cast<int>( idx / static_cast<quint64>( W ) );
    const int c = static_cast<int>( idx % static_cast<quint64>( W ) );
    RowGroup &group = byRow[r];
    if ( group.samples.isEmpty() )
    {
      group.minCol = c;
      group.maxCol = c;
    }
    else
    {
      group.minCol = std::min( group.minCol, c );
      group.maxCol = std::max( group.maxCol, c );
    }
    group.samples.append( qMakePair( s, c ) );
  }
  int widestSpan = 1;
  for ( auto it = byRow.constBegin(); it != byRow.constEnd(); ++it )
    widestSpan = std::max( widestSpan, it.value().maxCol - it.value().minCol + 1 );

  // ADR 0061 — per-band NoData discovery is owned by rsCollectBandNodata
  // (shared with the pipeline tile path).
  const RsPixelIgnoreOptions &ignore = options.ignore;
  std::vector<bool> bandHasNodata;
  std::vector<float> bandNodata;
  rsCollectBandNodata( ds, bands, ignore, bandHasNodata, bandNodata );

  // Pass 1 — validity only. isIgnorePixel's per-band decomposition is exact:
  // AnyBand ignores a pixel iff some band matches isIgnoreScalar, AllBands
  // iff every band does, so the full feature vector never needs to coexist
  // in memory (the matrix below therefore stays at kept × B, not N × B).
  // matchCount alone carries both verdicts (AnyBand = any match), which
  // keeps the per-candidate byte budget at one int (R29-T01 WP04).
  std::vector<int> matchCount( samples.size(), 0 );
  std::vector<float> rowBuf( static_cast<size_t>( widestSpan ) );
  // R29-T01 (WP05): per-row cancellation checkpoints inside each band scan —
  // the old loop only checked after a WHOLE band, so a tall raster could not
  // be interrupted mid-band; giant single-row rasters are covered by the
  // per-sample spacing below. Spacing: one sink call per 64k samples.
  constexpr quint64 kCancelCheckSamples = 65536;
  for ( int bi = 0; bi < B; ++bi )
  {
    GDALRasterBand *band = ds->GetRasterBand( bands[bi] );
    const bool hasNd = bandHasNodata[static_cast<size_t>( bi )];
    const float nd = bandNodata[static_cast<size_t>( bi )];
    const double bandFraction = 0.4 + 0.25 * static_cast<double>( bi + 1 ) / B;
    for ( auto it = byRow.constBegin(); it != byRow.constEnd(); ++it )
    {
      const int r = it.key();
      const RowGroup &group = it.value();
      const int spanW = group.maxCol - group.minCol + 1;
      const CPLErr err = band->RasterIO(
        GF_Read, group.minCol, r, spanW, 1, rowBuf.data(),
        spanW, 1, GDT_Float32, 0, 0 );
      if ( err != CE_None )
      {
        out.error = RsTrainingDataResult::Error::RasterReadFailed;
        out.errorMessage = QStringLiteral( "Failed to read raster band %1" ).arg( bands[bi] );
        return;
      }
      quint64 sinceCheck = 0;
      for ( const QPair<int, int> &sc : group.samples )
      {
        if ( ignore.isIgnoreScalar( rowBuf[static_cast<size_t>( sc.second - group.minCol )],
                                    hasNd, nd ) )
          ++matchCount[static_cast<size_t>( sc.first )];
        if ( ++sinceCheck >= kCancelCheckSamples )
        {
          sinceCheck = 0;
          if ( !reportProgress( progress, bandFraction,
                                QStringLiteral( "Reading training samples" ) ) )
          {
            out.error = RsTrainingDataResult::Error::Cancelled;
            out.errorMessage = QStringLiteral( "Cancelled" );
            return;
          }
        }
      }
      // Per-row checkpoint: bounds the tall-raster scan (one RasterIO per
      // row) even when no single row reaches the per-sample spacing.
      if ( !reportProgress( progress, bandFraction,
                            QStringLiteral( "Reading training samples" ) ) )
      {
        out.error = RsTrainingDataResult::Error::Cancelled;
        out.errorMessage = QStringLiteral( "Cancelled" );
        return;
      }
    }
    if ( !reportProgress( progress,
                          bandFraction,
                          QStringLiteral( "Reading training samples" ) ) )
    {
      out.error = RsTrainingDataResult::Error::Cancelled;
      out.errorMessage = QStringLiteral( "Cancelled" );
      return;
    }
  }

  QVector<int> keepRows;
  keepRows.reserve( static_cast<int>( samples.size() ) );
  for ( int s = 0; s < static_cast<int>( samples.size() ); ++s )
  {
    const bool ignored = ( ignore.mode == RsPixelIgnoreOptions::Mode::AnyBand )
                           ? matchCount[static_cast<size_t>( s )] > 0
                           : matchCount[static_cast<size_t>( s )] == B;
    if ( !ignored )
      keepRows.push_back( s );
  }

  if ( keepRows.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage = QStringLiteral( "No valid training pixels extracted" );
    return;
  }

  // R26-S2 — cap *kept* samples per class (the header contract): iterate the
  // kept rows of each class in the recorded shuffle order and keep the first
  // maxSamplesPerClass valid ones. With no invalid pixels this selects
  // exactly the historical prefix of the shuffle, so the no-NoData behaviour
  // (and its pinned test) is unchanged.
  if ( options.maxSamplesPerClass > 0 )
  {
    std::map<int, std::vector<std::pair<int, int>>> keptByClass; // class → (rank, sampleIdx)
    for ( int s : keepRows )
      keptByClass[samples[static_cast<size_t>( s )].classId].emplace_back(
        samples[static_cast<size_t>( s )].rank, s );
    std::vector<char> keepFlag( samples.size(), 0 );
    for ( auto &kv : keptByClass )
    {
      auto &kept = kv.second;
      std::sort( kept.begin(), kept.end() );
      const size_t cap = static_cast<size_t>( options.maxSamplesPerClass );
      for ( size_t k = 0; k < kept.size() && k < cap; ++k )
        keepFlag[static_cast<size_t>( kept[k].second )] = 1;
    }
    QVector<int> cappedRows;
    cappedRows.reserve( keepRows.size() );
    for ( int s : keepRows )
      if ( keepFlag[static_cast<size_t>( s )] )
        cappedRows.push_back( s );
    keepRows = cappedRows;
    // Defensive only: cap >= 1 and a non-empty keptByClass keep at least one
    // row per class, so keepRows cannot empty out here.
    if ( keepRows.isEmpty() )
    {
      out.error = RsTrainingDataResult::Error::NoValidPixels;
      out.errorMessage = QStringLiteral( "No valid training pixels extracted" );
      return;
    }
  }

  if ( keepRows.size() < options.minSamples )
  {
    out.error = RsTrainingDataResult::Error::InsufficientSamples;
    out.errorMessage = QStringLiteral( "Insufficient training samples (%1 kept, %2 required)" )
                         .arg( keepRows.size() ).arg( options.minSamples );
    return;
  }

  // Pass 2 — fill the kept-rows-only feature matrix. Re-group so rows whose
  // samples were all dropped (NoData or capped away) are not read again;
  // same span-scoped reads as pass 1 (R30-T01 WP06).
  QHash<int, RowGroup> byRowKept;
  byRowKept.reserve( keepRows.size() );
  for ( int i = 0; i < keepRows.size(); ++i )
  {
    const quint64 idx = samples[static_cast<size_t>( keepRows[i] )].pixel;
    const int r = static_cast<int>( idx / static_cast<quint64>( W ) );
    const int c = static_cast<int>( idx % static_cast<quint64>( W ) );
    RowGroup &group = byRowKept[r];
    if ( group.samples.isEmpty() )
    {
      group.minCol = c;
      group.maxCol = c;
    }
    else
    {
      group.minCol = std::min( group.minCol, c );
      group.maxCol = std::max( group.maxCol, c );
    }
    group.samples.append( qMakePair( i, c ) );
  }

  out.X.create( keepRows.size(), B, CV_32F );
  out.y.create( keepRows.size(), 1, CV_32S );
  out.sampleGroupIds.clear();
  out.sampleGroupIds.reserve( keepRows.size() );
  for ( int i = 0; i < keepRows.size(); ++i )
  {
    out.y.at<int>( i, 0 ) = samples[static_cast<size_t>( keepRows[i] )].classId;
    out.sampleGroupIds.push_back( samples[static_cast<size_t>( keepRows[i] )].group );
  }
  // R30-T01 (WP06): the no-usable-results contract. X/y are released on the
  // failure paths below; sampleGroupIds used to survive fully populated,
  // handing a partial matrix to anything that ignored `ok`.
  const auto releasePartialResults = [&out]() {
    out.X.release();
    out.y.release();
    out.sampleGroupIds.clear();
  };

  for ( int bi = 0; bi < B; ++bi )
  {
    GDALRasterBand *band = ds->GetRasterBand( bands[bi] );
    const double bandFraction = 0.65 + 0.35 * static_cast<double>( bi + 1 ) / B;
    for ( auto it = byRowKept.constBegin(); it != byRowKept.constEnd(); ++it )
    {
      const int r = it.key();
      const RowGroup &group = it.value();
      const int spanW = group.maxCol - group.minCol + 1;
      const CPLErr err = band->RasterIO(
        GF_Read, group.minCol, r, spanW, 1, rowBuf.data(),
        spanW, 1, GDT_Float32, 0, 0 );
      if ( err != CE_None )
      {
        out.error = RsTrainingDataResult::Error::RasterReadFailed;
        out.errorMessage = QStringLiteral( "Failed to read raster band %1" ).arg( bands[bi] );
        releasePartialResults();
        return;
      }
      for ( const QPair<int, int> &sc : group.samples )
        out.X.at<float>( sc.first, bi ) =
          rowBuf[static_cast<size_t>( sc.second - group.minCol )];
      // R29-T01 (WP05): same per-row cancellation checkpoint as pass 1.
      if ( !reportProgress( progress, bandFraction,
                            QStringLiteral( "Reading training samples" ) ) )
      {
        out.error = RsTrainingDataResult::Error::Cancelled;
        out.errorMessage = QStringLiteral( "Cancelled" );
        releasePartialResults();
        return;
      }
    }
    if ( !reportProgress( progress,
                          bandFraction,
                          QStringLiteral( "Reading training samples" ) ) )
    {
      out.error = RsTrainingDataResult::Error::Cancelled;
      out.errorMessage = QStringLiteral( "Cancelled" );
      releasePartialResults();
      return;
    }
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

  QHash<quint64, PixelOwner> pixelOwner;
  int failedGeometryIndex = -1;
  const CollectStatus collected = collectPixels( geometries, gt, W, H,
                                                 pixelOwner,
                                                 progress, options, failedGeometryIndex );
  if ( collected != CollectStatus::Ok )
  {
    GDALClose( ds );
    if ( collected == CollectStatus::RasterizeFailed )
    {
      out.error = RsTrainingDataResult::Error::RasterizeFailed;
      out.errorMessage =
        QStringLiteral( "Failed to rasterize training geometry %1" )
          .arg( failedGeometryIndex );
    }
    else
    {
      out.error = RsTrainingDataResult::Error::Cancelled;
      out.errorMessage = QStringLiteral( "Cancelled" );
    }
    return out;
  }

  buildMatrices( ds, bands, pixelOwner, options, progress, out );
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
      // Pin both CRSs to traditional GIS axis order (x/lon, y/lat): GDAL 3+
      // defaults to authority-compliant order, so an EPSG:4326 raster would
      // axis-swap transformed coordinates behind the GeoTransform's back.
      // A GPKG layer written without an SRS reports the undefined geographic
      // SRS (srs_id 0); PROJ still "transforms" between it and EPSG:4326 and
      // silently moves training polygons outside the raster footprint, which
      // extracted zero training pixels. Mirrors rs_roi_labeler.
      OGRSpatialReferenceH vecSrsClone = OSRClone( vecSrs );
      OGRSpatialReferenceH rasSrs = OSRNewSpatialReference( nullptr );
      if ( vecSrsClone && rasSrs
           && OSRImportFromWkt( rasSrs, const_cast<char **>( &rasWkt ) ) == OGRERR_NONE )
      {
        OSRSetAxisMappingStrategy( vecSrsClone, OAMS_TRADITIONAL_GIS_ORDER );
        OSRSetAxisMappingStrategy( rasSrs, OAMS_TRADITIONAL_GIS_ORDER );
        if ( !OSRIsSame( vecSrsClone, rasSrs ) )
        {
          coordTrans = OCTNewCoordinateTransformation( vecSrsClone, rasSrs );
        }
      }
      OSRDestroySpatialReference( vecSrsClone );
      OSRDestroySpatialReference( rasSrs );
    }
    GDALClose( rasDs );
  }

  QVector<RsTrainingGeometry> geometries;
  OGR_L_ResetReading( layer );
  OGRFeatureH feat = nullptr;
  bool cancelled = false;
  int transformFailed = 0;
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
        // Fail closed (#1461): an untransformed geometry silently votes
        // pixels in the wrong CRS (rs_roi_labeler discipline) — skip the
        // sample and count it instead of falling back to the raw geometry.
        if ( !clonedGeom
             || OGR_G_Transform( clonedGeom, coordTrans ) != OGRERR_NONE )
        {
          ++transformFailed;
          if ( clonedGeom )
            OGR_G_DestroyGeometry( clonedGeom );
          OGR_F_Destroy( feat );
          continue;
        }
        geomToUse = clonedGeom;
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

  // The skip count must stay visible (#1461): silent dropping was the bug.
  if ( transformFailed > 0 )
  {
    SICNU_LOG_WARN( SicnuLogTags::Classification,
                    QStringLiteral( "Skipped %1 of %2 training geometries whose reprojection to the raster CRS failed" )
                      .arg( transformFailed )
                      .arg( out.featuresRead ) );
  }

  if ( geometries.empty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage =
      QStringLiteral( "No valid training geometries extracted (featuresRead=%1, transformFailed=%2)" )
        .arg( out.featuresRead )
        .arg( transformFailed );
    return out;
  }

  GDALDataset *ds = openRaster( rasterPath, bands, out );
  if ( !ds )
    return out;

  const int W = ds->GetRasterXSize();
  const int H = ds->GetRasterYSize();
  double gt[6] = { 0, 1, 0, 0, 0, 1 };
  ds->GetGeoTransform( gt );

  QHash<quint64, PixelOwner> pixelOwner;
  int failedGeometryIndex = -1;
  const CollectStatus collected = collectPixels( geometries, gt, W, H,
                                                 pixelOwner,
                                                 progress, options, failedGeometryIndex );
  if ( collected != CollectStatus::Ok )
  {
    GDALClose( ds );
    if ( collected == CollectStatus::RasterizeFailed )
    {
      out.error = RsTrainingDataResult::Error::RasterizeFailed;
      out.errorMessage =
        QStringLiteral( "Failed to rasterize training geometry %1" )
          .arg( failedGeometryIndex );
    }
    else
    {
      out.error = RsTrainingDataResult::Error::Cancelled;
      out.errorMessage = QStringLiteral( "Cancelled" );
    }
    return out;
  }

  if ( pixelOwner.isEmpty() )
  {
    out.error = RsTrainingDataResult::Error::NoValidPixels;
    out.errorMessage =
      QStringLiteral( "No valid training pixels extracted (featuresRead=%1, geometries=%2, raster=%3x%4, transformFailed=%5)" )
        .arg( out.featuresRead )
        .arg( geometries.size() )
        .arg( W )
        .arg( H )
        .arg( transformFailed );
    GDALClose( ds );
    return out;
  }

  buildMatrices( ds, bands, pixelOwner, options, progress, out );
  GDALClose( ds );
  return out;
}
