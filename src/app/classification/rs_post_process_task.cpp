// rs_post_process_task.cpp — worker-thread post-process pipeline.

#include "rs_post_process_task.h"

#include "core/sicnu_logging.h"
#include "rs_post_process.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRgb>
#include <QVector>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{

/// Copy palette entries from the first band of \a path into \a table.
/// Indices are dense from 0..maxEntry; missing slots stay transparent black.
void loadColorTable( const QString &path, QVector<QRgb> &table )
{
  table.clear();
  GDALAllRegister();
  GDALDataset *ds = static_cast<GDALDataset *>(
    GDALOpen( path.toUtf8().constData(), GA_ReadOnly ) );
  if ( !ds )
    return;
  GDALRasterBand *band = ds->GetRasterBand( 1 );
  GDALColorTable *ct = band ? band->GetColorTable() : nullptr;
  if ( ct )
  {
    const int n = ct->GetColorEntryCount();
    table.resize( std::max( 0, n ) );
    for ( int i = 0; i < n; ++i )
    {
      const GDALColorEntry *e = ct->GetColorEntry( i );
      if ( e )
        table[i] = qRgba( e->c1, e->c2, e->c3, e->c4 );
      else
        table[i] = qRgba( 0, 0, 0, 0 );
    }
  }
  GDALClose( ds );
}

/// After recode old→new, rebuild palette so new ids keep old colors.
void remappedColorTable( QVector<QRgb> &table, const QMap<int, int> &map )
{
  if ( table.isEmpty() || map.isEmpty() )
    return;

  QVector<QRgb> out = table;
  int maxNew = table.size() - 1;
  for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
    maxNew = std::max( maxNew, it.value() );
  if ( maxNew >= out.size() )
    out.resize( maxNew + 1 );

  for ( auto it = map.constBegin(); it != map.constEnd(); ++it )
  {
    const int oldId = it.key();
    const int newId = it.value();
    if ( oldId >= 0 && oldId < table.size() && newId >= 0 )
      out[newId] = table[oldId];
  }
  table = std::move( out );
}

bool cancelled( QgsFeedback &fb, RsPostProcessTask::Result &result )
{
  if ( !fb.isCanceled() )
    return false;
  result.errorMessage = QStringLiteral( "Cancelled" );
  return true;
}

} // namespace

RsPostProcessTask::RsPostProcessTask( RsPostProcessConfig cfg )
  : QgsTask( tr( "Post-processing %1" ).arg( QFileInfo( cfg.inputPath ).fileName() ),
             QgsTask::CanCancel )
  , mCfg( std::move( cfg ) )
{
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

void RsPostProcessTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

bool RsPostProcessTask::run()
{
  QElapsedTimer timer;
  timer.start();

  SICNU_LOG_INFO( SicnuLogTags::Classification,
                  QStringLiteral( "Post-process task started: input=%1" )
                    .arg( QFileInfo( mCfg.inputPath ).fileName() ) );

  if ( mCfg.inputPath.isEmpty() )
  {
    mResult.errorMessage = QStringLiteral( "Empty input path" );
    return false;
  }
  if ( mCfg.outputRasterPath.isEmpty() )
  {
    mResult.errorMessage = QStringLiteral( "Empty output raster path" );
    return false;
  }
  if ( mCfg.runPolygonize && mCfg.outputVectorPath.isEmpty() )
  {
    mResult.errorMessage = QStringLiteral( "Polygonize enabled but output vector path is empty" );
    return false;
  }
  if ( mCfg.runRecode && mCfg.recodeMap.isEmpty() )
  {
    mResult.errorMessage = QStringLiteral( "Recode enabled but mapping table is empty" );
    return false;
  }

  // --- Load ----------------------------------------------------------------
  cv::Mat labels;
  double gt[6] = { 0, 1, 0, 0, 0, -1 };
  QString wkt;
  QString err;
  if ( !RsPostProcess::loadLabelRaster( mCfg.inputPath, labels, gt, wkt, &err ) )
  {
    mResult.errorMessage = err;
    return false;
  }
  QVector<QRgb> colorTable;
  loadColorTable( mCfg.inputPath, colorTable );
  mFb.setProgress( 10.0 );
  if ( cancelled( mFb, mResult ) )
    return false;

  // Progress milestones depend on which stages run.
  const bool stages[] = {
    mCfg.runSieve, mCfg.runMajority, mCfg.runClump, mCfg.runRecode
  };
  int nStages = 0;
  for ( bool s : stages )
  {
    if ( s )
      ++nStages;
  }
  // load=10, ops span 10→80, save=90, polygonize=100
  double prog = 10.0;
  const double stepSpan = nStages > 0 ? ( 70.0 / nStages ) : 70.0;

  auto advance = [&]() {
    prog = std::min( 80.0, prog + stepSpan );
    mFb.setProgress( prog );
  };

  // --- Sieve ---------------------------------------------------------------
  if ( mCfg.runSieve )
  {
    cv::Mat out;
    if ( !RsPostProcess::sieve( labels, out, mCfg.sieveThreshold,
                                mCfg.connectedness, &err ) )
    {
      mResult.errorMessage = err.isEmpty()
                               ? QStringLiteral( "Sieve failed" )
                               : err;
      return false;
    }
    labels = out;
    advance();
    if ( cancelled( mFb, mResult ) )
      return false;
  }

  // --- Majority ------------------------------------------------------------
  if ( mCfg.runMajority )
  {
    cv::Mat out;
    if ( !RsPostProcess::majorityFilter(
           labels, out, mCfg.majorityKernel, &err,
           [this]() { return mFb.isCanceled(); } ) )
    {
      mResult.errorMessage = err.isEmpty()
                               ? QStringLiteral( "Majority filter failed" )
                               : err;
      return false;
    }
    labels = out;
    advance();
    if ( cancelled( mFb, mResult ) )
      return false;
  }

  // --- Clump ---------------------------------------------------------------
  if ( mCfg.runClump )
  {
    cv::Mat out;
    if ( !RsPostProcess::clump( labels, out, mCfg.connectedness, &err ) )
    {
      mResult.errorMessage = err.isEmpty()
                               ? QStringLiteral( "Clump failed" )
                               : err;
      return false;
    }
    labels = out;
    // Component IDs are no longer class palette indices.
    colorTable.clear();
    advance();
    if ( cancelled( mFb, mResult ) )
      return false;
  }

  // --- Recode --------------------------------------------------------------
  if ( mCfg.runRecode )
  {
    cv::Mat out;
    if ( !RsPostProcess::recode( labels, out, mCfg.recodeMap, &err ) )
    {
      mResult.errorMessage = err.isEmpty()
                               ? QStringLiteral( "Recode failed" )
                               : err;
      return false;
    }
    labels = out;
    remappedColorTable( colorTable, mCfg.recodeMap );
    advance();
    if ( cancelled( mFb, mResult ) )
      return false;
  }

  // --- Save raster ---------------------------------------------------------
  mFb.setProgress( 85.0 );
  // NaN → no GDAL NoData marker (caller preserves the source's semantics).
  if ( !RsPostProcess::saveLabelRaster( mCfg.outputRasterPath, labels, gt, wkt,
                                        colorTable, mCfg.creationOptions,
                                        std::numeric_limits<double>::quiet_NaN(), &err ) )
  {
    mResult.errorMessage = err.isEmpty()
                             ? QStringLiteral( "Failed to save output raster" )
                             : err;
    return false;
  }
  mFb.setProgress( 92.0 );
  if ( cancelled( mFb, mResult ) )
    return false;

  // --- Polygonize ----------------------------------------------------------
  if ( mCfg.runPolygonize )
  {
    if ( !RsPostProcess::polygonize( mCfg.outputRasterPath, mCfg.outputVectorPath,
                                     QStringLiteral( "class_id" ), &err ) )
    {
      mResult.errorMessage = err.isEmpty()
                               ? QStringLiteral( "Polygonize failed" )
                               : err;
      return false;
    }
  }

  mResult.ok = true;
  mResult.durationMs = static_cast<int>( timer.elapsed() );
  mFb.setProgress( 100.0 );
  SICNU_LOG_SUCCESS( SicnuLogTags::Classification,
                      QStringLiteral( "Post-process completed: %1 (%2 ms)" )
                        .arg( QFileInfo( mCfg.outputRasterPath ).fileName() )
                        .arg( mResult.durationMs ) );
  return true;
}
