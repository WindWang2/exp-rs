// rs_classification_task.cpp — Phase 10A Task 10.8.

#include "rs_classification_task.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdint>
#include <vector>

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
  QElapsedTimer timer;
  timer.start();

  if ( !mCfg.backend )
  {
    mResult.errorMessage = QStringLiteral( "No classifier backend supplied" );
    return false;
  }
  if ( mCfg.trainX.empty() || mCfg.trainY.empty() )
  {
    mResult.errorMessage = QStringLiteral( "Empty training data" );
    return false;
  }

  // 1. Train
  if ( !mCfg.backend->fit( mCfg.trainX, mCfg.trainY ) )
  {
    mResult.errorMessage = QStringLiteral( "Backend training failed" );
    return false;
  }
  mFb.setProgress( 30.0 );
  if ( mFb.isCanceled() )
  {
    mResult.errorMessage = QStringLiteral( "Cancelled" );
    return false;
  }

  // 1b. Phase 10A Task 10.9 — accuracy assessment on the held-out split.
  // KMeans is skipped because its cluster IDs are an arbitrary permutation
  // of the ROI class IDs; confusion against testY would be misleading
  // until Hungarian-style cluster→class alignment is added in 10A.1.
  if ( mCfg.testX.rows > 0 && mCfg.testY.rows > 0
       && mCfg.algoName != QStringLiteral( "KMeans" ) )
  {
    try
    {
      const cv::Mat pred = mCfg.backend->predict( mCfg.testX );
      QVector<int> yt;
      QVector<int> yp;
      yt.reserve( pred.rows );
      yp.reserve( pred.rows );
      for ( int i = 0; i < pred.rows; ++i )
      {
        yt.append( mCfg.testY.at<int>( i, 0 ) );
        yp.append( pred.at<int>( i, 0 ) );
      }
      mResult.accuracy = RsAccuracyAssessment::compute( yt, yp );
    }
    catch ( const cv::Exception & )
    {
      // Accuracy failure must not abort the rest of the classification.
    }
  }

  // 2. Open source raster
  GDALAllRegister();
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
  GDALDataset *dstDs = drv->Create(
    mCfg.outputRaster.toUtf8().constData(), W, H, 1, GDT_Byte, nullptr );
  if ( !dstDs )
  {
    GDALClose( srcDs );
    mResult.errorMessage = QStringLiteral( "Cannot create output: %1" )
                             .arg( mCfg.outputRaster );
    return false;
  }
  dstDs->SetGeoTransform( gt );
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

  // 4. Tile-streamed predict
  constexpr int TILE = 256;
  const int B = mCfg.bandIndices.size();
  const int totalTiles =
    ( ( W + TILE - 1 ) / TILE ) * ( ( H + TILE - 1 ) / TILE );
  int doneTiles = 0;

  std::vector<float> tileBuf( static_cast<size_t>( TILE ) * TILE );
  std::vector<uint8_t> outBuf( static_cast<size_t>( TILE ) * TILE );

  for ( int ty = 0; ty < H; ty += TILE )
  {
    const int th = std::min( TILE, H - ty );
    for ( int tx = 0; tx < W; tx += TILE )
    {
      if ( mFb.isCanceled() )
      {
        GDALClose( srcDs );
        GDALClose( dstDs );
        QFile::remove( mCfg.outputRaster );
        mResult.errorMessage = QStringLiteral( "Cancelled" );
        return false;
      }
      const int tw = std::min( TILE, W - tx );
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

      const cv::Mat pred = mCfg.backend->predict( X );
      for ( int p = 0; p < npx; ++p )
      {
        const int v = pred.at<int>( p, 0 );
        outBuf[p] = static_cast<uint8_t>( std::clamp( v, 0, 255 ) );
      }
      dstDs->GetRasterBand( 1 )->RasterIO(
        GF_Write, tx, ty, tw, th, outBuf.data(),
        tw, th, GDT_Byte, 0, 0 );

      ++doneTiles;
      mFb.setProgress( 30.0 + 65.0 * doneTiles / totalTiles );
    }
  }

  GDALClose( srcDs );
  GDALClose( dstDs );

  mResult.totalPixels = W * H;
  mResult.durationMs = static_cast<int>( timer.elapsed() );
  mResult.ok = true;
  mFb.setProgress( 100.0 );
  return true;
}
