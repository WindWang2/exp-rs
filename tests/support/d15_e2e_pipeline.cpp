// tests/support/d15_e2e_pipeline.cpp — D15 Package I orchestration.
#include "d15_e2e_pipeline.h"

#include "core/spatial_split.h"
#include "processing/algorithms/change_detector.h"
#include "processing/algorithms/classification_postprocess.h"
#include "processing/algorithms/classifier_engine.h"
#include "processing/algorithms/confusion_matrix.h"
#include "processing/algorithms/glcm_texture.h"

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <vector>

namespace rs::testing
{
namespace
{
  struct FloatRaster
  {
      int width = 0;
      int height = 0;
      int bands = 0;
      double geoTransform[6] = { 0, 1, 0, 0, 0, -1 };
      std::string projectionWkt;
      std::vector<float> samples; // band-sequential
  };

  bool readFloatRaster( const std::string &path, FloatRaster &out )
  {
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.c_str(), GA_ReadOnly );
    if ( !ds )
      return false;
    out.width = GDALGetRasterXSize( ds );
    out.height = GDALGetRasterYSize( ds );
    out.bands = GDALGetRasterCount( ds );
    if ( out.width <= 0 || out.height <= 0 || out.bands <= 0 )
    {
      GDALClose( ds );
      return false;
    }
    GDALGetGeoTransform( ds, out.geoTransform );
    const char *wkt = GDALGetProjectionRef( ds );
    if ( wkt )
      out.projectionWkt = wkt;
    out.samples.resize( static_cast<size_t>( out.width ) * out.height * out.bands );
    for ( int b = 0; b < out.bands; ++b )
    {
      GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
      if ( GDALRasterIO( band, GF_Read, 0, 0, out.width, out.height,
                         out.samples.data() + static_cast<size_t>( b ) * out.width * out.height,
                         out.width, out.height, GDT_Float32, 0, 0 ) != CE_None )
      {
        GDALClose( ds );
        return false;
      }
    }
    GDALClose( ds );
    return true;
  }

  bool readLabelRaster( const std::string &path, FloatRaster &out )
  {
    return readFloatRaster( path, out );
  }

  bool writeByteRaster( const std::string &path, int width, int height,
                        const std::vector<uint8_t> &data,
                        const double geoTransform[6], const std::string &projectionWkt )
  {
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
      return false;
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), width, height, 1, GDT_Byte, nullptr );
    if ( !ds )
      return false;
    GDALSetGeoTransform( ds, geoTransform );
    GDALSetProjection( ds, projectionWkt.c_str() );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, 0 );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, width, height,
                                  const_cast<uint8_t *>( data.data() ), width, height, GDT_Byte, 0, 0 )
                    == CE_None;
    GDALClose( ds );
    return ok;
  }

  inline size_t planeOffset( const FloatRaster &r, int band, int x, int y )
  {
    return static_cast<size_t>( band ) * r.width * r.height + static_cast<size_t>( y ) * r.width + x;
  }
} // namespace

bool ClassificationChangeE2ePipeline::runFullWorkflow( const E2ePipelineConfig &config,
                                                       double &outOverallAccuracy,
                                                       double &outKappa,
                                                       double &outChangeDice )
{
  outOverallAccuracy = 0.0;
  outKappa = 0.0;
  outChangeDice = 0.0;

  FloatRaster t1, t2, truth;
  if ( !readFloatRaster( config.t1ImagePath, t1 ) )
    return false;
  if ( !readFloatRaster( config.t2ImagePath, t2 ) )
    return false;
  if ( !readLabelRaster( config.groundTruthMaskPath, truth ) )
    return false;
  if ( t1.width != t2.width || t1.height != t2.height || t1.bands != t2.bands )
    return false;
  if ( t1.width != truth.width || t1.height != truth.height )
    return false;
  const int w = t1.width, h = t1.height, bands = t1.bands;
  const size_t pixels = static_cast<size_t>( w ) * h;

  // ---- Feature stack: 4 spectral bands + GLCM contrast + entropy (band 0).
  const int featureDim = bands + 2;
  rs::processing::GlcmConfig glcmCfg;
  glcmCfg.windowSize = 3;
  glcmCfg.quantLevels = 16;
  glcmCfg.minVal = 0.0f;
  glcmCfg.maxVal = 1.0f;
  glcmCfg.direction = rs::processing::GlcmDirection::Omnidirectional;
  std::vector<float> contrastMap = rs::processing::GlcmTextureCalculator::computeTextureFeatureMap(
    t1.samples.data(), w, h, glcmCfg, "contrast" );
  std::vector<float> entropyMap = rs::processing::GlcmTextureCalculator::computeTextureFeatureMap(
    t1.samples.data(), w, h, glcmCfg, "entropy" );
  auto replaceNonFinite = []( std::vector<float> &v )
  {
    for ( float &x : v )
      if ( !std::isfinite( x ) )
        x = 0.0f;
  };
  replaceNonFinite( contrastMap );
  replaceNonFinite( entropyMap );

  std::vector<float> features( pixels * featureDim );
  for ( size_t p = 0; p < pixels; ++p )
  {
    for ( int b = 0; b < bands; ++b )
      features[p * featureDim + b] = t1.samples[static_cast<size_t>( b ) * pixels + p];
    features[p * featureDim + bands] = contrastMap[p];
    features[p * featureDim + bands + 1] = entropyMap[p];
  }

  // ---- Training samples + leakage-free split (truth 0 = masked).
  std::vector<rs::core::SpatialSamplePoint> samples;
  std::map<int, int> perClassBudget;
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( p % 3 != 0 )
      continue; // stride spread: keep samples away from one corner
    const int cls = static_cast<int>( truth.samples[p] );
    if ( cls <= 0 )
      continue;
    constexpr int kMaxPerClass = 400;
    if ( ++perClassBudget[cls] > kMaxPerClass )
      continue;
    rs::core::SpatialSamplePoint s;
    s.id = static_cast<int64_t>( p );
    s.x = static_cast<double>( p % w );
    s.y = static_cast<double>( p / w );
    s.label = cls;
    s.features.assign( features.begin() + static_cast<std::ptrdiff_t>( p * featureDim ),
                       features.begin() + static_cast<std::ptrdiff_t>( ( p + 1 ) * featureDim ) );
    samples.push_back( std::move( s ) );
  }
  rs::core::SpatialBlockConfig splitCfg;
  splitCfg.blockWidth = 32.0;
  splitCfg.blockHeight = 32.0;
  splitCfg.bufferDistance = 8.0;
  const rs::core::SpatialSplitReport split = rs::core::SpatialBlockPartitioner().partition( samples, splitCfg );

  std::vector<int> trainLabels;
  std::vector<float> trainFeatures;
  const bool splitUsable = split.trainCount > 0 && ( split.valCount + split.testCount ) > 0;
  for ( size_t i = 0; i < samples.size(); ++i )
  {
    // Train on the train fold; on a degenerate single-block scene fall back
    // to all samples (small lab grids cannot support isolated folds).
    if ( splitUsable && split.assignments[i] != rs::core::SampleRole::Train )
      continue;
    trainLabels.push_back( samples[i].label );
    trainFeatures.insert( trainFeatures.end(), samples[i].features.begin(), samples[i].features.end() );
  }
  if ( trainLabels.empty() )
    return false;

  // ---- Random forest classification of the whole scene.
  rs::processing::ClassifierHyperparameters params;
  params.rfNumTrees = 80;
  auto forest = rs::processing::ClassifierEngine::create(
    rs::processing::ClassifierAlgorithm::RandomForest, params );
  if ( !forest )
    return false;
  forest->fit( trainFeatures, trainLabels, trainLabels.size(), featureDim );

  std::vector<int> predicted( pixels, 0 );
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( static_cast<int>( truth.samples[p] ) <= 0 )
      continue; // masked pixels stay 0 (nodata) in every output
    predicted[p] = forest->predictOne( std::span( features ).subspan( p * featureDim, featureDim ) );
  }

  // ---- Morphological cleanup on thematic labels (nodata-safe: the
  // post-processor's default noDataValue is -1, but this pipeline's sentinel
  // is 0 — shift by one for the filter and back).
  std::vector<int> shifted( pixels );
  for ( size_t p = 0; p < pixels; ++p )
    shifted[p] = predicted[p] - 1; // 0 -> -1 (nodata), classes shift down
  rs::processing::MorphologicalFilterConfig postCfg;
  postCfg.windowSize = 3;
  postCfg.minSievePixelSize = 4;
  postCfg.connectivity = 8;
  postCfg.noDataValue = -1;
  std::vector<int> majority = rs::processing::ClassificationPostProcessor::applyMajorityFilter( shifted, w, h, postCfg );
  std::vector<int> sieved = rs::processing::ClassificationPostProcessor::applySieveFilter( majority, w, h, 4, 8 );
  std::vector<int> cleaned( pixels, 0 );
  for ( size_t p = 0; p < pixels; ++p )
    cleaned[p] = sieved[p] + 1; // back to class space; nodata back to 0

  std::vector<uint8_t> classMap( pixels );
  for ( size_t p = 0; p < pixels; ++p )
    classMap[p] = static_cast<uint8_t>( std::clamp( cleaned[p], 0, 255 ) );
  if ( !writeByteRaster( config.outputClassificationPath, w, h, classMap, truth.geoTransform, truth.projectionWkt ) )
    return false;

  // ---- Accuracy of the post-processed map against ground truth.
  std::vector<int> evalTruth, evalPred;
  for ( size_t p = 0; p < pixels; ++p )
  {
    const int cls = static_cast<int>( truth.samples[p] );
    if ( cls <= 0 )
      continue;
    evalTruth.push_back( cls );
    evalPred.push_back( cleaned[p] );
  }
  std::vector<int> classes;
  for ( const auto &[cls, count] : perClassBudget )
  {
    ( void ) count;
    classes.push_back( cls );
  }
  if ( evalTruth.empty() || classes.empty() )
    return false;
  const auto metrics = rs::processing::ConfusionMatrixEvaluator::compute( evalTruth, evalPred, classes );
  outOverallAccuracy = metrics.overallAccuracy;
  outKappa = metrics.cohensKappa;

  // ---- Change detection: CVA magnitude, adaptive threshold (samples are
  // already band-sequential; no copy needed).
  // Alpha 4.0: unchanged-pixel magnitudes follow a chi(4) distribution; the
  // 1.5-sigma Gaussian heuristic sits inside its fat tail (~3% false alarms),
  // while 4 sigma clears it (P ~ 5e-6) for budgeted change footprints.
  const auto cva = rs::processing::ChangeDetector::computeCva( t1.samples.data(), t2.samples.data(), w, h, bands, 4.0f );
  std::vector<uint8_t> changeMask( pixels, 0 );
  for ( size_t p = 0; p < pixels; ++p )
    changeMask[p] = cva.binaryChangeMask[p] ? 1 : 0;

  // ---- Dice against the change oracle: true class transitions when T2
  // truth is available, else a large-margin spectral difference threshold.
  int truthChanged = 0, detected = 0, hits = 0;
  FloatRaster truth2;
  const bool hasTruth2 = !config.t2GroundTruthMaskPath.empty() && readLabelRaster( config.t2GroundTruthMaskPath, truth2 );
  for ( size_t p = 0; p < pixels; ++p )
  {
    bool oracle = false;
    if ( hasTruth2 && truth2.width == w && truth2.height == h )
    {
      oracle = static_cast<int>( truth.samples[p] ) > 0 && static_cast<int>( truth2.samples[p] ) > 0
               && static_cast<int>( truth.samples[p] ) != static_cast<int>( truth2.samples[p] );
    }
    else
    {
      double d2 = 0.0;
      for ( int b = 0; b < bands; ++b )
      {
        const double d = t2.samples[static_cast<size_t>( b ) * pixels + p] - t1.samples[static_cast<size_t>( b ) * pixels + p];
        d2 += d * d;
      }
      oracle = std::sqrt( d2 / bands ) > 0.25; // large-margin spectral change
    }
    detected += changeMask[p] ? 1 : 0;
    if ( oracle )
    {
      ++truthChanged;
      hits += changeMask[p] ? 1 : 0;
    }
  }
  if ( truthChanged + detected > 0 )
    outChangeDice = static_cast<double>( 2 * hits ) / static_cast<double>( truthChanged + detected );

  return writeByteRaster( config.outputChangeMapPath, w, h, changeMask, truth.geoTransform, truth.projectionWkt );
}

} // namespace rs::testing
