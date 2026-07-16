// rs_classification_task.cpp — Phase 10A Task 10.8.

#include "rs_classification_task.h"

#include "core/sicnu_logging.h"
#include "rs_hungarian_assignment.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QList>
#include <QSet>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "processing/gdal/gdal_dataset_wrapper.h"

#include <cpl_string.h>
#include <gdal_priv.h>

RsClassificationTask::RsClassificationTask( Config cfg )
  : QgsTask( tr( "Classifying %1" ).arg( QFileInfo( cfg.sourceRaster ).fileName() ),
             QgsTask::CanCancel )
  , mCfg( std::move( cfg ) )
{
  connect( &mFb, &QgsFeedback::progressChanged,
           this, [this]( double p ) { setProgress( p ); } );
}

void RsClassificationTask::cancel()
{
  mFb.cancel();
  QgsTask::cancel();
}

bool RsClassificationTask::run()
{
  // Progress milestones: training finishes at 30%, tile prediction spans
  // 30%-95%, and final bookkeeping fills to 100%.
  constexpr double kProgressAfterTrain   = 30.0;
  constexpr double kProgressPredictSpan  = 65.0;
  constexpr double kProgressComplete     = 100.0;

  QElapsedTimer timer;
  timer.start();

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "Classification task started: algo=%1, bands=%2, output=%3" )
    .arg( mCfg.algoName ).arg( mCfg.bandIndices.size() ).arg( QFileInfo( mCfg.outputRaster ).fileName() ) );

  if ( !mCfg.backend )
  {
    mResult.errorMessage = QStringLiteral( "No classifier backend supplied" );
    return false;
  }

  // 1. Train — skipped when the backend was loaded from disk
  //    (Phase 10A.1.3). When fit is required, empty trainX/trainY is fatal.
  if ( !mCfg.backend->isFitted() )
  {
    if ( mCfg.trainX.empty() || mCfg.trainY.empty() )
    {
      mResult.errorMessage = QStringLiteral(
        "Backend not fitted and no training data supplied" );
      return false;
    }
    if ( !mCfg.backend->fit( mCfg.trainX, mCfg.trainY ) )
    {
      mResult.errorMessage = QStringLiteral( "Backend training failed" );
      return false;
    }
  }
  mFb.setProgress( kProgressAfterTrain );
  if ( mFb.isCanceled() )
  {
    mResult.errorMessage = QStringLiteral( "Cancelled" );
    return false;
  }

  // Hungarian remapping table for K-Means to align cluster IDs (1..K) with true class IDs.
  QHash<int, int> kmeansRemap;
  if ( mCfg.algoName == QStringLiteral( "KMeans" ) && !mCfg.trainX.empty() && !mCfg.trainY.empty() )
  {
    try
    {
      const cv::Mat trainPred = mCfg.backend->predict( mCfg.trainX );
      QSet<int> trueSet, clusterSet;
      for ( int i = 0; i < mCfg.trainY.rows; ++i )
        trueSet.insert( mCfg.trainY.at<int>( i, 0 ) );
      for ( int i = 0; i < trainPred.rows; ++i )
        clusterSet.insert( trainPred.at<int>( i, 0 ) );

      if ( !trueSet.isEmpty() )
      {
        QList<int> tList( trueSet.begin(), trueSet.end() );
        QList<int> cList( clusterSet.begin(), clusterSet.end() );
        std::sort( tList.begin(), tList.end() );
        std::sort( cList.begin(), cList.end() );
        const int N = tList.size();
        const int M = cList.size();

        // Rectangular N×M cost; RsHungarianAssignment pads to square safely.
        cv::Mat cost = cv::Mat::zeros( N, M, CV_64F );
        for ( int i = 0; i < mCfg.trainY.rows; ++i )
        {
          const int ti = tList.indexOf( mCfg.trainY.at<int>( i, 0 ) );
          const int ci = cList.indexOf( trainPred.at<int>( i, 0 ) );
          if ( ti >= 0 && ci >= 0 )
            cost.at<double>( ti, ci ) -= 1.0;
        }

        const QVector<int> assign = RsHungarianAssignment::solve( cost );
        for ( int i = 0; i < N && i < assign.size(); ++i )
        {
          const int clusterIdx = assign[i];
          if ( clusterIdx >= 0 && clusterIdx < M )
            kmeansRemap[cList[clusterIdx]] = tList[i];
        }
      }
    }
    catch ( ... )
    {
      qWarning() << "KMeans Hungarian remap failed, using un-remapped cluster IDs";
    }
  }

  // 1b. Phase 10A Task 10.9 + 10A.1.1 — accuracy assessment on the held-out split.
  // NormalBayes / SVM: predictions are already in class-ID space.
  // K-Means: cluster IDs are remapped to true class IDs.
  if ( mCfg.testX.rows > 0 && mCfg.testY.rows > 0 )
  {
    try
    {
      const cv::Mat pred = mCfg.backend->predict( mCfg.testX );
      QVector<int> yt;
      QVector<int> yp;

      if ( mCfg.algoName == QStringLiteral( "KMeans" ) )
      {
        yt.reserve( mCfg.testY.rows );
        yp.reserve( mCfg.testY.rows );
        for ( int i = 0; i < mCfg.testY.rows; ++i )
        {
          yt.append( mCfg.testY.at<int>( i, 0 ) );
          yp.append( kmeansRemap.value( pred.at<int>( i, 0 ), pred.at<int>( i, 0 ) ) );
        }
      }
      else
      {
        // Supervised: predictions already in class-ID space.
        yt.reserve( pred.rows );
        yp.reserve( pred.rows );
        for ( int i = 0; i < pred.rows; ++i )
        {
          yt.append( mCfg.testY.at<int>( i, 0 ) );
          yp.append( pred.at<int>( i, 0 ) );
        }
      }

      if ( !yt.isEmpty() )
        mResult.accuracy = RsAccuracyAssessment::compute( yt, yp );
    }
    catch ( const cv::Exception &e )
    {
      qWarning() << "Accuracy assessment failed:" << e.what();
    }
  }

  // 2. Open source raster
  ensureGdalInit();
  GDALDataset *srcDs = static_cast<GDALDataset *>(
    GDALOpen( mCfg.sourceRaster.toUtf8().constData(), GA_ReadOnly ) );
  if ( !srcDs )
  {
    mResult.errorMessage = QStringLiteral( "Cannot open source raster: %1" )
                             .arg( mCfg.sourceRaster );
    return false;
  }
  const int W = srcDs->GetRasterXSize();
  const int H = srcDs->GetRasterYSize();
  double gt[6];
  srcDs->GetGeoTransform( gt );
  const char *proj = srcDs->GetProjectionRef();

  // Optional viewport crop (preview). Full apply leaves cropToWindow=false.
  const bool useCrop = mCfg.cropToWindow && mCfg.window.valid;
  int x0 = 0;
  int y0 = 0;
  int x1 = W;
  int y1 = H;
  if ( useCrop )
  {
    x0 = std::clamp( mCfg.window.x0, 0, W );
    y0 = std::clamp( mCfg.window.y0, 0, H );
    x1 = std::clamp( mCfg.window.x1, 0, W );
    y1 = std::clamp( mCfg.window.y1, 0, H );
    if ( x1 <= x0 || y1 <= y0 )
    {
      GDALClose( srcDs );
      mResult.errorMessage = QStringLiteral( "Crop window is empty after clamp" );
      return false;
    }
  }
  const int Wout = x1 - x0;
  const int Hout = y1 - y0;
  double outGt[6] = { gt[0], gt[1], gt[2], gt[3], gt[4], gt[5] };
  if ( useCrop )
  {
    // Origin at source pixel (x0, y0).
    outGt[0] = gt[0] + x0 * gt[1] + y0 * gt[2];
    outGt[3] = gt[3] + x0 * gt[4] + y0 * gt[5];
  }

  // Sanity-check requested bands
  const int nBands = srcDs->GetRasterCount();
  for ( int b : mCfg.bandIndices )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( srcDs );
      mResult.errorMessage = QStringLiteral( "Band index %1 out of range (1..%2)" )
                               .arg( b )
                               .arg( nBands );
      return false;
    }
  }

  // 3. Create destination GTiff
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
  {
    GDALClose( srcDs );
    mResult.errorMessage = QStringLiteral( "GTiff driver unavailable" );
    return false;
  }
  char **papsz = nullptr;
  for ( const QString &o : mCfg.creationOptions )
    papsz = CSLAddString( papsz, o.toUtf8().constData() );
  GDALDataset *dstDs = drv->Create(
    mCfg.outputRaster.toUtf8().constData(), Wout, Hout, 1, GDT_Byte, papsz );
  if ( !dstDs && papsz )
  {
    CSLDestroy( papsz );
    papsz = nullptr;
    qWarning() << "Create with options failed; retrying without options";
    dstDs = drv->Create(
      mCfg.outputRaster.toUtf8().constData(), Wout, Hout, 1, GDT_Byte, nullptr );
  }
  else
  {
    CSLDestroy( papsz );
  }
  if ( !dstDs )
  {
    GDALClose( srcDs );
    mResult.errorMessage = QStringLiteral( "Cannot create output: %1" )
                             .arg( mCfg.outputRaster );
    return false;
  }
  dstDs->SetGeoTransform( outGt );
  if ( proj && *proj )
    dstDs->SetProjection( proj );

  // Attach ColorTable. Phase 10A review patch: index 0 is reserved for the
  // "unclassified" / background pixel and rendered transparent — previously
  // it defaulted to opaque black which masked unclassified areas.
  GDALColorTable ct( GPI_RGB );
  {
    GDALColorEntry bg;
    bg.c1 = 0;
    bg.c2 = 0;
    bg.c3 = 0;
    bg.c4 = 0; // alpha 0 → transparent
    ct.SetColorEntry( 0, &bg );
  }
  for ( auto it = mCfg.classColors.constBegin(); it != mCfg.classColors.constEnd(); ++it )
  {
    GDALColorEntry e;
    e.c1 = static_cast<short>( it.value().red() );
    e.c2 = static_cast<short>( it.value().green() );
    e.c3 = static_cast<short>( it.value().blue() );
    e.c4 = 255;
    ct.SetColorEntry( it.key(), &e );
  }
  dstDs->GetRasterBand( 1 )->SetColorTable( &ct );
  dstDs->GetRasterBand( 1 )->SetColorInterpretation( GCI_PaletteIndex );

  // 4. Tile-streamed predict — loop only the (possibly cropped) window.
  constexpr int kTileSize = 256;
  const int B = mCfg.bandIndices.size();
  const int totalTiles =
    ( ( Wout + kTileSize - 1 ) / kTileSize ) * ( ( Hout + kTileSize - 1 ) / kTileSize );
  int doneTiles = 0;

  std::vector<float> tileBuf( static_cast<size_t>( kTileSize ) * kTileSize );
  std::vector<uint8_t> outBuf( static_cast<size_t>( kTileSize ) * kTileSize );

  for ( int ty = y0; ty < y1; ty += kTileSize )
  {
    const int th = std::min( kTileSize, y1 - ty );
    for ( int tx = x0; tx < x1; tx += kTileSize )
    {
      if ( mFb.isCanceled() )
      {
        GDALClose( srcDs );
        GDALClose( dstDs );
        QFile::remove( mCfg.outputRaster );
        mResult.errorMessage = QStringLiteral( "Cancelled" );
        return false;
      }
      const int tw = std::min( kTileSize, x1 - tx );
      const int npx = th * tw;

      cv::Mat X( npx, B, CV_32F );
      for ( int bi = 0; bi < B; ++bi )
      {
        const int bandIdx = mCfg.bandIndices[bi];
        const CPLErr err = srcDs->GetRasterBand( bandIdx )->RasterIO(
          GF_Read, tx, ty, tw, th, tileBuf.data(),
          tw, th, GDT_Float32, 0, 0 );
        if ( err != CE_None )
        {
          GDALClose( srcDs );
          GDALClose( dstDs );
          QFile::remove( mCfg.outputRaster );
          mResult.errorMessage =
            QStringLiteral( "RasterIO read failed at tile (%1,%2)" ).arg( tx ).arg( ty );
          return false;
        }
        for ( int p = 0; p < npx; ++p )
          X.at<float>( p, bi ) = tileBuf[p];
      }

      if ( mCfg.scaler.isFitted() )
      {
        X = mCfg.scaler.transform( X );
        if ( X.empty() )
        {
          GDALClose( srcDs );
          GDALClose( dstDs );
          QFile::remove( mCfg.outputRaster );
          mResult.errorMessage = QStringLiteral(
            "Feature scaling failed at tile (%1,%2)" ).arg( tx ).arg( ty );
          return false;
        }
      }

      cv::Mat pred;
      try
      {
        pred = mCfg.backend->predict( X );
      }
      catch ( const cv::Exception &ex )
      {
        GDALClose( srcDs );
        GDALClose( dstDs );
        QFile::remove( mCfg.outputRaster );
        mResult.errorMessage = QStringLiteral( "Classifier prediction threw OpenCV exception: %1" )
                                 .arg( QString::fromStdString( ex.what() ) );
        return false;
      }
      catch ( ... )
      {
        GDALClose( srcDs );
        GDALClose( dstDs );
        QFile::remove( mCfg.outputRaster );
        mResult.errorMessage = QStringLiteral( "Classifier prediction threw unknown exception" );
        return false;
      }

      // Verify prediction output size matches tile pixel count
      if ( pred.rows < npx )
      {
        GDALClose( srcDs );
        GDALClose( dstDs );
        QFile::remove( mCfg.outputRaster );
        mResult.errorMessage = QStringLiteral( "Classifier returned fewer predictions (%1) than expected (%2)" )
                                   .arg( pred.rows ).arg( npx );
        return false;
      }

      for ( int p = 0; p < npx; ++p )
      {
        int v = pred.at<int>( p, 0 );
        if ( mCfg.algoName == QStringLiteral( "KMeans" ) )
        {
          v = kmeansRemap.value( v, v );
        }
        outBuf[p] = static_cast<uint8_t>( std::clamp( v, 0, 255 ) );
      }
      // Source tile at absolute (tx,ty); dest at window-relative offset.
      const int ox = tx - x0;
      const int oy = ty - y0;
      dstDs->GetRasterBand( 1 )->RasterIO(
        GF_Write, ox, oy, tw, th, outBuf.data(),
        tw, th, GDT_Byte, 0, 0 );

      ++doneTiles;
      mFb.setProgress( kProgressAfterTrain + kProgressPredictSpan * doneTiles / totalTiles );
    }
  }

  GDALClose( srcDs );
  GDALClose( dstDs );

  mResult.totalPixels = Wout * Hout;
  mResult.durationMs = static_cast<int>( timer.elapsed() );
  mResult.ok = true;
  mFb.setProgress( kProgressComplete );
  SICNU_LOG_SUCCESS( SicnuLogTags::Classification, QString( "Classification completed: %1 pixels, %2 ms" )
    .arg( mResult.totalPixels ).arg( mResult.durationMs ) );
  return true;
}
