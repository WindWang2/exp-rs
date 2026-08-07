// rs_classification_pipeline.cpp — ADR 0019 slice S2: classification pipeline
// core. Ported from the Phase 10A Task 10.8 app-layer task run() body. The
// QgsTask adapter was deleted in ADR 0053 — the GUI now calls this seam
// directly (RsClassificationPipeline::Config, one config vocabulary).

#include "rs_classification_pipeline.h"

#include "rs_classification_split.h"
#include "rs_classifier_backend_factory.h"
#include "rs_classification_utils.h"
#include "rs_hungarian_assignment.h"
#include "rs_training_data_extraction.h"
#include "sicnu_logging.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <cpl_string.h>
#include <gdal_priv.h>

namespace
{

/// ADR 0019 decision 3 — superset sidecar format version.
constexpr int kSidecarVersion = 1;

bool reportProgress( const RsClassificationPipeline::Progress &progress,
                     double fraction, const QString &message )
{
  return !progress || progress( fraction, message );
}

} // namespace

QString RsClassificationPipeline::sidecarPathForModel( const QString &modelPath )
{
  const QFileInfo mi( modelPath );
  return mi.absolutePath() + QLatin1Char( '/' )
         + mi.completeBaseName() + QStringLiteral( ".meta.json" );
}

bool RsClassificationPipeline::saveModelSidecar( const QString &modelPath,
                                                 const QString &methodName,
                                                 const RsFeatureScaler &scaler,
                                                 const QHash<int, QColor> &classColors,
                                                 const QVector<int> &bandIndices,
                                                 const RsAccuracyAssessment::Result &accuracy )
{
  QJsonObject root;
  root.insert( QStringLiteral( "version" ), kSidecarVersion );
  root.insert( QStringLiteral( "method" ), methodName );
  if ( scaler.isFitted() )
    root.insert( QStringLiteral( "scaler" ), scaler.toJson() );
  if ( !classColors.isEmpty() )
  {
    QList<int> ids = classColors.keys();
    std::sort( ids.begin(), ids.end() );
    QJsonArray classes;
    for ( int id : ids )
    {
      QJsonObject c;
      c.insert( QStringLiteral( "id" ), id );
      c.insert( QStringLiteral( "color" ), classColors.value( id ).name() );
      classes.append( c );
    }
    root.insert( QStringLiteral( "classes" ), classes );
  }
  // Feature schema: the 1-based training bands. Used to validate the target
  // raster's band count when the model is applied elsewhere.
  if ( !bandIndices.isEmpty() )
  {
    QJsonArray features;
    for ( int b : bandIndices )
      features.append( b );
    root.insert( QStringLiteral( "features" ), features );
  }
  // Holdout validation metrics (overall accuracy / kappa / per-class P-R-F1).
  if ( !accuracy.classIds.isEmpty() )
  {
    QJsonObject validation;
    validation.insert( QStringLiteral( "overallAccuracy" ), accuracy.overallAccuracy );
    validation.insert( QStringLiteral( "kappa" ), accuracy.kappa );
    QJsonObject perClass;
    for ( int id : accuracy.classIds )
    {
      QJsonObject c;
      c.insert( QStringLiteral( "producerAccuracy" ), accuracy.producerAcc.value( id, 0.0 ) );
      c.insert( QStringLiteral( "userAccuracy" ), accuracy.userAcc.value( id, 0.0 ) );
      c.insert( QStringLiteral( "f1" ), accuracy.f1.value( id, 0.0 ) );
      perClass.insert( QString::number( id ), c );
    }
    validation.insert( QStringLiteral( "perClass" ), perClass );
    root.insert( QStringLiteral( "validation" ), validation );
  }

  QFile f( sidecarPathForModel( modelPath ) );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  f.write( QJsonDocument( root ).toJson( QJsonDocument::Compact ) );
  return true;
}

bool RsClassificationPipeline::loadModelSidecar( const QString &modelPath,
                                                 QString &methodName,
                                                 RsFeatureScaler &scaler,
                                                 QHash<int, QColor> &classColors,
                                                 QVector<int> &bandIndices,
                                                 RsAccuracyAssessment::Result &accuracy )
{
  methodName.clear();
  scaler = RsFeatureScaler();
  classColors.clear();
  bandIndices.clear();
  accuracy = RsAccuracyAssessment::Result();

  QFile f( sidecarPathForModel( modelPath ) );
  if ( !f.open( QIODevice::ReadOnly ) )
    return false;
  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isObject() )
    return false;
  const QJsonObject root = doc.object();
  if ( root.value( QStringLiteral( "version" ) ).toInt() != kSidecarVersion )
    return false;

  methodName = root.value( QStringLiteral( "method" ) ).toString();

  const QJsonValue scalerVal = root.value( QStringLiteral( "scaler" ) );
  if ( scalerVal.isObject() && !scaler.fromJson( scalerVal.toObject() ) )
    return false;

  for ( const QJsonValue &v : root.value( QStringLiteral( "classes" ) ).toArray() )
  {
    const QJsonObject c = v.toObject();
    const QColor color( c.value( QStringLiteral( "color" ) ).toString() );
    if ( color.isValid() )
      classColors.insert( c.value( QStringLiteral( "id" ) ).toInt(), color );
  }

  for ( const QJsonValue &v : root.value( QStringLiteral( "features" ) ).toArray() )
  {
    if ( v.isDouble() )
      bandIndices.append( v.toInt() );
  }

  const QJsonObject validation = root.value( QStringLiteral( "validation" ) ).toObject();
  if ( !validation.isEmpty() )
  {
    accuracy.overallAccuracy = validation.value( QStringLiteral( "overallAccuracy" ) ).toDouble();
    accuracy.kappa = validation.value( QStringLiteral( "kappa" ) ).toDouble();
    const QJsonObject perClass = validation.value( QStringLiteral( "perClass" ) ).toObject();
    for ( auto it = perClass.constBegin(); it != perClass.constEnd(); ++it )
    {
      const int id = it.key().toInt();
      const QJsonObject c = it.value().toObject();
      accuracy.classIds.append( id );
      accuracy.producerAcc.insert( id, c.value( QStringLiteral( "producerAccuracy" ) ).toDouble() );
      accuracy.userAcc.insert( id, c.value( QStringLiteral( "userAccuracy" ) ).toDouble() );
      accuracy.f1.insert( id, c.value( QStringLiteral( "f1" ) ).toDouble() );
    }
    std::sort( accuracy.classIds.begin(), accuracy.classIds.end() );
  }
  return true;
}

RsClassificationPipelineResult RsClassificationPipeline::run(
  Config config, const Progress &progress )
{
  // Progress milestones (fraction form of the former QgsTask percents):
  // training finishes at 0.30, tile prediction spans 0.30–0.95, and final
  // bookkeeping fills to 1.0.
  constexpr double kProgressAfterTrain   = 0.30;
  constexpr double kProgressPredictSpan  = 0.65;
  constexpr double kProgressComplete     = 1.0;

  RsClassificationPipelineResult result;

  QElapsedTimer timer;
  timer.start();

  // Predict-only mode: auto-load model and sidecar when modelLoadPath is specified
  if ( config.trainX.empty() && !config.modelLoadPath.isEmpty() )
  {
    QString sidecarMethod;
    RsFeatureScaler sidecarScaler;
    QHash<int, QColor> sidecarColors;
    QVector<int> sidecarFeatures;
    RsAccuracyAssessment::Result sidecarAccuracy;
    if ( loadModelSidecar( config.modelLoadPath, sidecarMethod, sidecarScaler,
                           sidecarColors, sidecarFeatures, sidecarAccuracy ) )
    {
      if ( sidecarScaler.isFitted() )
        config.scaler = sidecarScaler;
      if ( config.classColors.isEmpty() )
        config.classColors = sidecarColors;
      if ( config.methodName.isEmpty() )
        config.methodName = sidecarMethod;
      if ( config.bandIndices.isEmpty() )
        config.bandIndices = sidecarFeatures;
    }

    // Model compatibility check: the target raster's band selection must match
    // the model's training feature schema (when the sidecar records one).
    if ( !sidecarFeatures.isEmpty() && !config.bandIndices.isEmpty()
         && sidecarFeatures.size() != config.bandIndices.size() )
    {
      QStringList modelBands;
      for ( int b : sidecarFeatures )
        modelBands.append( QString::number( b ) );
      QStringList targetBands;
      for ( int b : config.bandIndices )
        targetBands.append( QString::number( b ) );

      result.ok = false;
      result.error = RsClassificationPipelineResult::Error::InvalidBand;
      result.errorMessage = QStringLiteral(
        "Model %1 was trained on %2 features (bands %3) but the target raster "
        "provides %4 bands (%5). Select the same bands before applying the model." )
        .arg( config.modelLoadPath )
        .arg( sidecarFeatures.size() )
        .arg( modelBands.join( QStringLiteral( "," ) ) )
        .arg( config.bandIndices.size() )
        .arg( targetBands.join( QStringLiteral( "," ) ) );
      return result;
    }

    if ( !config.backend )
    {
      // ADR 0061 — backend construction is owned by the factory (single
      // method-name → backend mapping; preserves the historical "bayes"
      // sidecar sniff).
      config.backend = RsClassifierBackendFactory::create( config.methodName );
    }

    if ( !config.backend->load( config.modelLoadPath ) )
    {
      result.ok = false;
      result.error = RsClassificationPipelineResult::Error::ModelOpenFailed;
      result.errorMessage = QStringLiteral( "Failed to load model from %1" ).arg( config.modelLoadPath );
      return result;
    }
  }

  if ( !config.backend )
  {
    result.error = RsClassificationPipelineResult::Error::NoBackend;
    result.errorMessage = QStringLiteral( "No classifier backend supplied" );
    return result;
  }

  // Auto-extract training data from vector polygons when specified and trainX is empty
  if ( config.trainX.empty() && !config.trainingVector.isEmpty() )
  {
    RsTrainingDataExtraction::Options exOptions;
    exOptions.maxSamplesPerClass = config.maxSamplesPerClass;
    const RsTrainingDataResult ex = RsTrainingDataExtraction::extractFromVector(
      config.sourceRaster,
      config.bandIndices,
      config.trainingVector,
      config.classField,
      exOptions,
      [&progress]( double fraction, const QString &message ) {
        return reportProgress( progress, 0.15 * fraction, message );
      } );

    if ( !ex.ok )
    {
      result.ok = false;
      result.errorMessage = ex.errorMessage;
      switch ( ex.error )
      {
        case RsTrainingDataResult::Error::VectorOpenFailed:
          result.error = RsClassificationPipelineResult::Error::VectorOpenFailed;
          break;
        case RsTrainingDataResult::Error::VectorNoLayers:
          result.error = RsClassificationPipelineResult::Error::VectorNoLayers;
          break;
        case RsTrainingDataResult::Error::ClassFieldNotFound:
          result.error = RsClassificationPipelineResult::Error::ClassFieldNotFound;
          break;
        case RsTrainingDataResult::Error::NoValidPixels:
          result.error = RsClassificationPipelineResult::Error::NoValidPixels;
          break;
        case RsTrainingDataResult::Error::Cancelled:
          result.error = RsClassificationPipelineResult::Error::Cancelled;
          break;
        default:
          result.error = RsClassificationPipelineResult::Error::VectorOpenFailed;
          break;
      }
      return result;
    }

    if ( ex.X.rows < 2 )
    {
      result.ok = false;
      result.error = RsClassificationPipelineResult::Error::InsufficientSamples;
      result.errorMessage = QStringLiteral( "Insufficient training samples" );
      return result;
    }

    if ( config.classColors.isEmpty() )
    {
      for ( auto it = ex.classCounts.constBegin(); it != ex.classCounts.constEnd(); ++it )
      {
        config.classColors[it.key()] = rsSynthesizedClassColor( it.key() );
      }
    }

    result.featuresExtracted = ex.featuresRead;
    result.trainSamples = ex.X.rows;
    result.classCount = static_cast<int>( ex.classCounts.size() );
    result.trainSamplesByClass = ex.classCounts;

    cv::Mat trainX = ex.X;
    cv::Mat trainY = ex.y;
    if ( config.testSplit > 0.0 )
    {
      const RsTrainTestSplit split = RsClassificationSplit::stratifiedSplit( ex.X, ex.y, 1.0 - config.testSplit );
      trainX = split.trainX;
      trainY = split.trainY;
      config.testX = split.testX;
      config.testY = split.testY;
    }

    if ( config.fitScaler )
    {
      if ( !config.scaler.fit( trainX ) )
      {
        result.ok = false;
        result.error = RsClassificationPipelineResult::Error::ScalingFailed;
        result.errorMessage = QStringLiteral( "Feature scaling failed" );
        return result;
      }
      trainX = config.scaler.transform( trainX );
      if ( !config.testX.empty() )
        config.testX = config.scaler.transform( config.testX );
    }

    config.trainX = trainX;
    config.trainY = trainY;
  }

  // 1. Train — skipped when the backend was loaded from disk
  //    (Phase 10A.1.3). When fit is required, empty trainX/trainY is fatal.
  if ( !config.backend->isFitted() )
  {
    if ( config.trainX.empty() || config.trainY.empty() )
    {
      result.error = RsClassificationPipelineResult::Error::NotFittedNoTrainingData;
      result.errorMessage = QStringLiteral(
        "Backend not fitted and no training data supplied" );
      return result;
    }
    if ( !config.backend->fit( config.trainX, config.trainY ) )
    {
      result.error = RsClassificationPipelineResult::Error::TrainingFailed;
      result.errorMessage = QStringLiteral( "Backend training failed" );
      return result;
    }
  }

  // Optional model persistence — write OpenCV YAML + the superset sidecar
  // (method + scaler + class metadata) next to it so the model-load path can
  // restore both. Hard-fail when modelSavePath is set and either half fails;
  // if the model wrote successfully but the sidecar did not, remove the
  // orphan model so callers never load a model without its matching
  // .meta.json.
  if ( !config.modelSavePath.isEmpty() )
  {
    if ( !config.backend->save( config.modelSavePath ) )
    {
      result.error = RsClassificationPipelineResult::Error::ModelSaveFailed;
      result.errorMessage = QStringLiteral( "Failed to save classifier model: %1" )
                              .arg( config.modelSavePath );
      SICNU_LOG_ERROR( SicnuLogTags::Classification, result.errorMessage );
      return result;
    }
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "Classifier model saved: %1" )
                      .arg( config.modelSavePath ) );
    if ( !saveModelSidecar( config.modelSavePath, config.methodName,
                            config.scaler, config.classColors,
                            config.bandIndices, result.accuracy ) )
    {
      QFile::remove( config.modelSavePath );
      result.error = RsClassificationPipelineResult::Error::SidecarSaveFailed;
      result.errorMessage =
        QStringLiteral( "Failed to save model sidecar: %1 (model file removed)" )
          .arg( sidecarPathForModel( config.modelSavePath ) );
      SICNU_LOG_ERROR( SicnuLogTags::Classification, result.errorMessage );
      return result;
    }
    SICNU_LOG_INFO( SicnuLogTags::Classification,
                    QString( "Model sidecar saved: %1" )
                      .arg( sidecarPathForModel( config.modelSavePath ) ) );
  }

  if ( !reportProgress( progress, kProgressAfterTrain,
                        QStringLiteral( "Training finished" ) ) )
  {
    result.error = RsClassificationPipelineResult::Error::Cancelled;
    result.errorMessage = QStringLiteral( "Cancelled" );
    return result;
  }

  // Hungarian remapping table for backends whose predicted labels are not in
  // the training-label space (K-Means cluster ids 1..K). Built only when the
  // backend declares needsLabelRemap() and training data is available —
  // ADR 0061, the former methodName == "KMeans" branch is gone.
  QHash<int, int> kmeansRemap;
  if ( config.backend->needsLabelRemap() && !config.trainX.empty() && !config.trainY.empty() )
  {
    try
    {
      const cv::Mat trainPred = config.backend->predict( config.trainX );
      QSet<int> trueSet, clusterSet;
      for ( int i = 0; i < config.trainY.rows; ++i )
        trueSet.insert( config.trainY.at<int>( i, 0 ) );
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
        // Rectangular n×m cost (n=true classes, m=clusters). Hungarian pads
        // safely with kPadCost=1e9 when N != M; pad matches return -1.
        cv::Mat cost = cv::Mat::zeros( N, M, CV_64F );
        for ( int i = 0; i < config.trainY.rows; ++i )
        {
          const int ti = tList.indexOf( config.trainY.at<int>( i, 0 ) );
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
  if ( config.testX.rows > 0 && config.testY.rows > 0 )
  {
    try
    {
      const cv::Mat pred = config.backend->predict( config.testX );
      QVector<int> yt;
      QVector<int> yp;

      if ( config.backend->needsLabelRemap() )
      {
        // K-Means: cluster IDs are remapped to true class IDs.
        yt.reserve( config.testY.rows );
        yp.reserve( config.testY.rows );
        for ( int i = 0; i < config.testY.rows; ++i )
        {
          yt.append( config.testY.at<int>( i, 0 ) );
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
          yt.append( config.testY.at<int>( i, 0 ) );
          yp.append( pred.at<int>( i, 0 ) );
        }
      }

      if ( !yt.isEmpty() )
        result.accuracy = RsAccuracyAssessment::compute( yt, yp );
    }
    catch ( const cv::Exception &e )
    {
      qWarning() << "Accuracy assessment failed:" << e.what();
    }
  }

  // 2. Open source raster
  GDALAllRegister();
  GDALDataset *srcDs = static_cast<GDALDataset *>(
    GDALOpen( config.sourceRaster.toUtf8().constData(), GA_ReadOnly ) );
  if ( !srcDs )
  {
    result.error = RsClassificationPipelineResult::Error::RasterOpenFailed;
    result.errorMessage = QStringLiteral( "Cannot open source raster: %1" )
                            .arg( config.sourceRaster );
    return result;
  }
  const int srcW = srcDs->GetRasterXSize();
  const int srcH = srcDs->GetRasterYSize();
  double gt[6];
  srcDs->GetGeoTransform( gt );
  const char *proj = srcDs->GetProjectionRef();

  // Optional viewport crop (preview). Full-raster Apply leaves crop off.
  const bool crop = config.cropToWindow && config.window.valid
                    && config.window.width() > 0 && config.window.height() > 0;
  // Clamp half-open window into source extents (0,0 / full size when !crop).
  const int x0 = std::clamp( crop ? config.window.x0 : 0, 0, srcW );
  const int y0 = std::clamp( crop ? config.window.y0 : 0, 0, srcH );
  const int x1 = std::clamp( crop ? config.window.x1 : srcW, 0, srcW );
  const int y1 = std::clamp( crop ? config.window.y1 : srcH, 0, srcH );
  if ( x1 <= x0 || y1 <= y0 )
  {
    GDALClose( srcDs );
    result.error = RsClassificationPipelineResult::Error::EmptyCropWindow;
    result.errorMessage = QStringLiteral( "Crop window is empty or outside the source raster" );
    return result;
  }
  const int outW = x1 - x0;
  const int outH = y1 - y0;

  // Destination geotransform: shift origin to window top-left pixel.
  double outGt[6] = { gt[0], gt[1], gt[2], gt[3], gt[4], gt[5] };
  if ( crop )
  {
    outGt[0] = gt[0] + x0 * gt[1] + y0 * gt[2];
    outGt[3] = gt[3] + x0 * gt[4] + y0 * gt[5];
  }

  // Sanity-check requested bands
  const int nBands = srcDs->GetRasterCount();
  for ( int b : config.bandIndices )
  {
    if ( b < 1 || b > nBands )
    {
      GDALClose( srcDs );
      result.error = RsClassificationPipelineResult::Error::InvalidBand;
      result.errorMessage = QStringLiteral( "Band index %1 out of range (1..%2)" )
                              .arg( b )
                              .arg( nBands );
      return result;
    }
  }

  // Max class id drives output datatype: Byte only when all ids fit in 0..255.
  // Never silently clamp large class IDs into uint8.
  int maxClassId = 0;
  for ( auto it = config.classColors.constBegin(); it != config.classColors.constEnd(); ++it )
    maxClassId = std::max( maxClassId, it.key() );
  for ( int i = 0; i < config.trainY.rows; ++i )
    maxClassId = std::max( maxClassId, config.trainY.at<int>( i, 0 ) );
  for ( int i = 0; i < config.testY.rows; ++i )
    maxClassId = std::max( maxClassId, config.testY.at<int>( i, 0 ) );

  GDALDataType outType = GDT_Byte;
  if ( maxClassId > 65535 )
    outType = GDT_Int32;
  else if ( maxClassId > 255 )
    outType = GDT_UInt16;

  // 3. Create destination GTiff (tiled+DEFLATE by default; fall back on fail)
  GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !drv )
  {
    GDALClose( srcDs );
    result.error = RsClassificationPipelineResult::Error::OutputDriverUnavailable;
    result.errorMessage = QStringLiteral( "GTiff driver unavailable" );
    return result;
  }
  char **papsz = nullptr;
  for ( const QString &o : config.creationOptions )
    papsz = CSLAddString( papsz, o.toUtf8().constData() );
  GDALDataset *dstDs = drv->Create(
    config.outputRaster.toUtf8().constData(), outW, outH, 1, outType, papsz );
  if ( !dstDs && papsz )
  {
    CSLDestroy( papsz );
    papsz = nullptr;
    qWarning() << "RsClassificationPipeline: Create with options failed; retrying without options";
    dstDs = drv->Create(
      config.outputRaster.toUtf8().constData(), outW, outH, 1, outType, nullptr );
  }
  else
  {
    CSLDestroy( papsz );
    papsz = nullptr;
  }
  if ( !dstDs )
  {
    GDALClose( srcDs );
    result.error = RsClassificationPipelineResult::Error::OutputCreateFailed;
    result.errorMessage = QStringLiteral( "Cannot create output: %1" )
                            .arg( config.outputRaster );
    return result;
  }
  dstDs->SetGeoTransform( outGt );
  if ( proj && *proj )
    dstDs->SetProjection( proj );

  // ColorTable is palette-index based and only meaningful for Byte output.
  // Index 0 is reserved for unclassified / NoData (transparent).
  if ( outType == GDT_Byte )
  {
    GDALColorTable ct( GPI_RGB );
    {
      GDALColorEntry bg;
      bg.c1 = 0;
      bg.c2 = 0;
      bg.c3 = 0;
      bg.c4 = 0; // alpha 0 → transparent
      ct.SetColorEntry( 0, &bg );
    }
    for ( auto it = config.classColors.constBegin(); it != config.classColors.constEnd(); ++it )
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
  }
  const int unclassified = config.ignoreOptions.unclassifiedValue;
  if ( config.ignoreOptions.writeOutputNodata )
    dstDs->GetRasterBand( 1 )->SetNoDataValue( unclassified );

  // Per-band source NoData (optional) + user ignore values → unclassified.
  // ADR 0061 — NoData discovery is owned by rsCollectBandNodata (shared with
  // training-data extraction).
  const int B = config.bandIndices.size();
  std::vector<bool> bandHasNodata;
  std::vector<float> bandNodata;
  rsCollectBandNodata( srcDs, config.bandIndices, config.ignoreOptions,
                       bandHasNodata, bandNodata );

  // Every failure past this point removes the partially-written output.
  const auto failWithPartialOutput = [&result, &srcDs, &dstDs, &config](
    RsClassificationPipelineResult::Error error, const QString &message )
  {
    GDALClose( srcDs );
    GDALClose( dstDs );
    QFile::remove( config.outputRaster );
    result.error = error;
    result.errorMessage = message;
    return result;
  };

  // 4. Tile-streamed predict over [x0,x1)×[y0,y1) in source pixel space;
  // write relative to the destination origin (0,0).
  constexpr int kTileSize = 256;
  const int totalTiles = std::max( 1,
    ( ( outW + kTileSize - 1 ) / kTileSize )
    * ( ( outH + kTileSize - 1 ) / kTileSize ) );
  int doneTiles = 0;

  std::vector<float> tileBuf( static_cast<size_t>( kTileSize ) * kTileSize );
  // Int32 write buffer; GDAL converts to the band datatype on RasterIO.
  std::vector<int32_t> outBuf( static_cast<size_t>( kTileSize ) * kTileSize );
  std::vector<uint8_t> pixelNodata( static_cast<size_t>( kTileSize ) * kTileSize );

  for ( int ty = y0; ty < y1; ty += kTileSize )
  {
    const int th = std::min( kTileSize, y1 - ty );
    for ( int tx = x0; tx < x1; tx += kTileSize )
    {
      const int tw = std::min( kTileSize, x1 - tx );
      const int npx = th * tw;

      std::fill( pixelNodata.begin(), pixelNodata.begin() + npx, 0 );

      cv::Mat X( npx, B, CV_32F );
      for ( int bi = 0; bi < B; ++bi )
      {
        const int bandIdx = config.bandIndices[bi];
        const CPLErr err = srcDs->GetRasterBand( bandIdx )->RasterIO(
          GF_Read, tx, ty, tw, th, tileBuf.data(),
          tw, th, GDT_Float32, 0, 0 );
        if ( err != CE_None )
        {
          return failWithPartialOutput(
            RsClassificationPipelineResult::Error::RasterReadFailed,
            QStringLiteral( "RasterIO read failed at tile (%1,%2)" ).arg( tx ).arg( ty ) );
        }
        for ( int p = 0; p < npx; ++p )
        {
          const float v = tileBuf[static_cast<size_t>( p )];
          X.at<float>( p, bi ) = v;
        }
      }
      // Mark ignore / edge pixels after all bands are filled.
      std::vector<float> feat( static_cast<size_t>( B ) );
      for ( int p = 0; p < npx; ++p )
      {
        for ( int bi = 0; bi < B; ++bi )
          feat[static_cast<size_t>( bi )] = X.at<float>( p, bi );
        if ( config.ignoreOptions.isIgnorePixel( feat.data(), B, bandHasNodata, bandNodata ) )
          pixelNodata[static_cast<size_t>( p )] = 1;
      }

      if ( config.scaler.isFitted() )
      {
        X = config.scaler.transform( X );
        if ( X.empty() )
        {
          return failWithPartialOutput(
            RsClassificationPipelineResult::Error::ScalingFailed,
            QStringLiteral( "Feature scaling failed at tile (%1,%2)" ).arg( tx ).arg( ty ) );
        }
      }

      cv::Mat pred;
      try
      {
        pred = config.backend->predict( X );
      }
      catch ( const cv::Exception &ex )
      {
        return failWithPartialOutput(
          RsClassificationPipelineResult::Error::PredictionFailed,
          QStringLiteral( "Classifier prediction threw OpenCV exception: %1" )
            .arg( QString::fromStdString( ex.what() ) ) );
      }
      catch ( ... )
      {
        return failWithPartialOutput(
          RsClassificationPipelineResult::Error::PredictionFailed,
          QStringLiteral( "Classifier prediction threw unknown exception" ) );
      }

      // Verify prediction output size matches tile pixel count
      if ( pred.rows < npx )
      {
        return failWithPartialOutput(
          RsClassificationPipelineResult::Error::PredictionSizeMismatch,
          QStringLiteral( "Classifier returned fewer predictions (%1) than expected (%2)" )
            .arg( pred.rows ).arg( npx ) );
      }

      for ( int p = 0; p < npx; ++p )
      {
        if ( pixelNodata[static_cast<size_t>( p )] )
        {
          outBuf[static_cast<size_t>( p )] = static_cast<int32_t>( unclassified );
          continue;
        }
        int v = pred.at<int>( p, 0 );
        if ( config.backend->needsLabelRemap() )
        {
          v = kmeansRemap.value( v, v );
        }
        if ( v < 0 )
          v = unclassified;
        outBuf[static_cast<size_t>( p )] = static_cast<int32_t>( v );
      }
      // Destination offsets are relative to the crop window origin.
      // GDAL converts Int32 buffer to the band datatype (Byte / UInt16 / Int32).
      const int dstX = tx - x0;
      const int dstY = ty - y0;
      dstDs->GetRasterBand( 1 )->RasterIO(
        GF_Write, dstX, dstY, tw, th, outBuf.data(),
        tw, th, GDT_Int32, 0, 0 );

      ++doneTiles;
      if ( !reportProgress( progress,
                            kProgressAfterTrain + kProgressPredictSpan * doneTiles / totalTiles,
                            QStringLiteral( "Predicting tiles" ) ) )
      {
        return failWithPartialOutput(
          RsClassificationPipelineResult::Error::Cancelled,
          QStringLiteral( "Cancelled" ) );
      }
    }
  }

  GDALClose( srcDs );
  GDALClose( dstDs );

  result.totalPixels = outW * outH;
  result.durationMs = static_cast<int>( timer.elapsed() );
  result.ok = true;
  reportProgress( progress, kProgressComplete, QStringLiteral( "Classification complete" ) );
  SICNU_LOG_SUCCESS( SicnuLogTags::Classification, QString( "Classification completed: %1 pixels, %2 ms" )
    .arg( result.totalPixels ).arg( result.durationMs ) );
  return result;
}
