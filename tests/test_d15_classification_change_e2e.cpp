// tests/test_d15_classification_change_e2e.cpp — D15 Package I.
//
// End-to-end acceptance for the whole D15 stack plus the teaching-lab
// grading contracts.  Scene layout, class-change geometry and threshold
// calibration are fixture facts derived in the comments; the lab slices
// grade pipeline outputs through the REAL OutputVerifier and the committed
// rules (no grader mocking).
#include <catch2/catch_test_macros.hpp>

#include <json/json.h>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include "agent/output_verifier.h"
#include "processing/algorithms/change_detector.h"
#include "support/d15_e2e_pipeline.h"

namespace
{
  constexpr const char *kSourceDir = CMAKE_SOURCE_DIR;

  void ensureGdal()
  {
    static bool registered = ( GDALAllRegister(), true );
    ( void ) registered;
  }

  std::string epsg4326Wkt()
  {
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    OSRImportFromEPSG( srs, 4326 );
    char *wkt = nullptr;
    OSRExportToWkt( srs, &wkt );
    OSRDestroySpatialReference( srs );
    std::string out = wkt ? wkt : "";
    CPLFree( wkt );
    return out;
  }

  bool writeFloatTif( const std::string &path, int w, int h, int bands,
                      const std::vector<float> &planes, const std::string &wkt )
  {
    ensureGdal();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
      return false;
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), w, h, bands, GDT_Float32, nullptr );
    if ( !ds )
      return false;
    double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds, wkt.c_str() );
    for ( int b = 0; b < bands; ++b )
    {
      GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
      if ( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                         const_cast<float *>( planes.data() ) + static_cast<size_t>( b ) * w * h,
                         w, h, GDT_Float32, 0, 0 ) != CE_None )
      {
        GDALClose( ds );
        return false;
      }
    }
    GDALClose( ds );
    return true;
  }

  bool writeClassTif( const std::string &path, int w, int h,
                      const std::vector<uint8_t> &values, const std::string &wkt,
                      double noDataValue )
  {
    ensureGdal();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
      return false;
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), w, h, 1, GDT_Byte, nullptr );
    if ( !ds )
      return false;
    double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds, wkt.c_str() );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, noDataValue );
    const bool ok = GDALRasterIO( band, GF_Write, 0, 0, w, h, const_cast<uint8_t *>( values.data() ),
                                  w, h, GDT_Byte, 0, 0 )
                    == CE_None;
    GDALClose( ds );
    return ok;
  }

  // Deterministic gaussian noise (Box-Muller over an LCG).
  class Noise
  {
    public:
      explicit Noise( uint64_t seed ) : m_state( seed ) {}
      double normal()
      {
        if ( m_hasSpare )
        {
          m_hasSpare = false;
          return m_spare;
        }
        double u = 0, v = 0, s = 0;
        do
        {
          u = uniform() * 2 - 1;
          v = uniform() * 2 - 1;
          s = u * u + v * v;
        } while ( s >= 1.0 || s == 0.0 );
        const double r = std::sqrt( -2.0 * std::log( s ) / s );
        m_spare = v * r;
        m_hasSpare = true;
        return u * r;
      }

    private:
      double uniform()
      {
        m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>( ( m_state >> 33 ) & 0xFFFFFF ) / 16777216.0;
      }
      uint64_t m_state;
      double m_spare = 0.0;
      bool m_hasSpare = false;
  };

  // Analytic class signatures over 4 bands (B, G, R, NIR); separations are
  // >= 20 sigma for the fixture sigma of 0.01.
  constexpr double kSignatures[5][4] = {
    { 0.00, 0.00, 0.00, 0.00 },   // 0 masked
    { 0.05, 0.03, 0.02, 0.01 },   // 1 water
    { 0.04, 0.08, 0.05, 0.60 },   // 2 vegetation
    { 0.30, 0.32, 0.35, 0.40 },   // 3 built
    { 0.45, 0.42, 0.40, 0.35 },   // 4 bare soil
  };

  struct Scene
  {
    std::vector<float> t1, t2;
    std::vector<uint8_t> truth, truth2;
  };

  Scene makeQuadrantScene( int w, int h, Noise &noise )
  {
    Scene s;
    s.t1.resize( static_cast<size_t>( w ) * h * 4 );
    s.t2.resize( static_cast<size_t>( w ) * h * 4 );
    s.truth.assign( static_cast<size_t>( w ) * h, 0 );
    s.truth2.assign( static_cast<size_t>( w ) * h, 0 );
    for ( int y = 0; y < h; ++y )
      for ( int x = 0; x < w; ++x )
      {
        const size_t p = static_cast<size_t>( y ) * w + x;
        const int cls = ( x < w / 2 ) ? ( y < h / 2 ? 1 : 3 ) : ( y < h / 2 ? 2 : 4 );
        s.truth[p] = static_cast<uint8_t>( cls );
        s.truth2[p] = static_cast<uint8_t>( cls );
        for ( int b = 0; b < 4; ++b )
        {
          // Band-sequential planes (the pipeline reads BSQ).
          s.t1[static_cast<size_t>( b ) * w * h + p] = static_cast<float>( kSignatures[cls][b] + 0.01 * noise.normal() );
          s.t2[static_cast<size_t>( b ) * w * h + p] = static_cast<float>( kSignatures[cls][b] + 0.01 * noise.normal() );
        }
      }
    // T2: rewrite a 32x32 vegetation block (rows 96..127, cols 160..191,
    // inside the vegetation quadrant) as built.
    for ( int y = 96; y <= 127; ++y )
      for ( int x = 160; x <= 191; ++x )
      {
        const size_t p = static_cast<size_t>( y ) * w + x;
        s.truth2[p] = 3;
        for ( int b = 0; b < 4; ++b )
          s.t2[static_cast<size_t>( b ) * w * h + p] = static_cast<float>( kSignatures[3][b] + 0.01 * noise.normal() );
      }
    return s;
  }

  Json::Value readJsonFile( const std::string &path )
  {
    Json::Value root;
    std::ifstream in( path );
    if ( !in )
      return {};
    Json::CharReaderBuilder builder;
    std::string errors;
    Json::parseFromStream( builder, in, &root, &errors );
    return root;
  }
} // namespace

TEST_CASE( "D15 full pipeline clears the OA / kappa / Dice gates",
           "[d15][e2e]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Noise noise( 2026091400ULL );

  const int w = 256, h = 256;
  const Scene scene = makeQuadrantScene( w, h, noise );
  const std::string wkt = epsg4326Wkt();
  const std::string t1 = dir.filePath( "t1.tif" ).toStdString();
  const std::string t2 = dir.filePath( "t2.tif" ).toStdString();
  const std::string truth = dir.filePath( "truth.tif" ).toStdString();
  const std::string truth2 = dir.filePath( "truth2.tif" ).toStdString();
  REQUIRE( writeFloatTif( t1, w, h, 4, scene.t1, wkt ) );
  REQUIRE( writeFloatTif( t2, w, h, 4, scene.t2, wkt ) );
  REQUIRE( writeClassTif( truth, w, h, scene.truth, wkt, 0 ) );
  REQUIRE( writeClassTif( truth2, w, h, scene.truth2, wkt, 0 ) );

  rs::testing::E2ePipelineConfig cfg;
  cfg.t1ImagePath = t1;
  cfg.t2ImagePath = t2;
  cfg.groundTruthMaskPath = truth;
  cfg.t2GroundTruthMaskPath = truth2;
  cfg.outputClassificationPath = dir.filePath( "class.tif" ).toStdString();
  cfg.outputChangeMapPath = dir.filePath( "change.tif" ).toStdString();

  double oa = 0.0, kappa = 0.0, dice = 0.0;
  REQUIRE( rs::testing::ClassificationChangeE2ePipeline::runFullWorkflow( cfg, oa, kappa, dice ) );
  REQUIRE( oa >= 0.88 );
  REQUIRE( kappa >= 0.82 );
  REQUIRE( dice >= 0.90 );
}

TEST_CASE( "Lab03 landcover contract grades the classified artifact at 100",
           "[d15][e2e][lab]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Noise noise( 777ULL );

  // Read the committed 32x32 truth fixture and re-render it spectrally.
  const std::string truthPath = std::string( kSourceDir ) + "/tests/fixtures/lab/landcover_truth.tif";
  ensureGdal();
  GDALDatasetH truthDs = GDALOpen( truthPath.c_str(), GA_ReadOnly );
  REQUIRE( truthDs );
  const int w = GDALGetRasterXSize( truthDs );
  const int h = GDALGetRasterYSize( truthDs );
  std::vector<uint8_t> truth( static_cast<size_t>( w ) * h );
  GDALRasterIO( GDALGetRasterBand( truthDs, 1 ), GF_Read, 0, 0, w, h, truth.data(), w, h, GDT_Byte, 0, 0 );
  const std::string wkt = GDALGetProjectionRef( truthDs );
  GDALClose( truthDs );

  std::vector<float> t1( static_cast<size_t>( w ) * h * 4 );
  for ( size_t p = 0; p < truth.size(); ++p )
    for ( int b = 0; b < 4; ++b )
      t1[static_cast<size_t>( b ) * truth.size() + p] = static_cast<float>( kSignatures[truth[p]][b] + 0.01 * noise.normal() );

  const std::string t1Path = dir.filePath( "lab_t1.tif" ).toStdString();
  const std::string t2Path = dir.filePath( "lab_t2.tif" ).toStdString();
  REQUIRE( writeFloatTif( t1Path, w, h, 4, t1, wkt ) );
  REQUIRE( writeFloatTif( t2Path, w, h, 4, t1, wkt ) ); // no change date pair

  rs::testing::E2ePipelineConfig cfg;
  cfg.t1ImagePath = t1Path;
  cfg.t2ImagePath = t2Path;
  cfg.groundTruthMaskPath = truthPath;
  cfg.outputClassificationPath = dir.filePath( "lab_class.tif" ).toStdString();
  cfg.outputChangeMapPath = dir.filePath( "lab_change.tif" ).toStdString();
  double oa = 0.0, kappa = 0.0, dice = 0.0;
  REQUIRE( rs::testing::ClassificationChangeE2ePipeline::runFullWorkflow( cfg, oa, kappa, dice ) );
  REQUIRE( oa >= 0.85 ); // rules floor, by construction we land far above

  sicnu::agent::OutputVerifier verifier;
  sicnu::agent::OutputVerifier::LabGradeOptions options;
  options.rulesDir = QString::fromStdString( std::string( kSourceDir ) + "/data/labs/grading" );
  const auto result = verifier.gradeForTeaching( QStringLiteral( "landcover_classify" ),
                                                 QString::fromStdString( cfg.outputClassificationPath ),
                                                 options );
  REQUIRE( result.graded );
  INFO( "score=" << result.score << " verdict=" << result.verdict.toStdString() );
  REQUIRE( result.score == 100.0 );
}

TEST_CASE( "Lab04 change contract grades the CVA artifact at 100",
           "[d15][e2e][lab]" )
{
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  Noise noise( 4242ULL );

  // 128x128 pair: flat scene plus a 48x64 rectangle (rows 40..87, cols
  // 32..95) shifted by [0.3, 0.4, 0, 0] (magnitude 0.5 >> unchanged ~0.02);
  // a 16x16 corner block (rows 100..115, cols 0..15 = 256 px = 1.56%) is
  // declared nodata in the artifact.
  const int w = 128, h = 128;
  const size_t pixels = static_cast<size_t>( w ) * h;
  std::vector<float> t1( pixels * 4, 0.2f ), t2( pixels * 4, 0.2f );
  for ( size_t p = 0; p < pixels; ++p )
    for ( int b = 0; b < 4; ++b )
    {
      t1[b * pixels + p] = static_cast<float>( 0.2 + 0.005 * noise.normal() );
      t2[b * pixels + p] = static_cast<float>( 0.2 + 0.005 * noise.normal() );
    }
  for ( int y = 40; y <= 87; ++y )
    for ( int x = 32; x <= 95; ++x )
    {
      const size_t p = static_cast<size_t>( y ) * w + x;
      t2[0 * pixels + p] = t1[0 * pixels + p] + 0.3f;
      t2[1 * pixels + p] = t1[1 * pixels + p] + 0.4f;
    }

  const std::string wkt = epsg4326Wkt();
  const std::string t1Path = dir.filePath( "ch_t1.tif" ).toStdString();
  const std::string t2Path = dir.filePath( "ch_t2.tif" ).toStdString();
  REQUIRE( writeFloatTif( t1Path, w, h, 4, t1, wkt ) );
  REQUIRE( writeFloatTif( t2Path, w, h, 4, t2, wkt ) );

  // CVA + fixed-budget ranking: the scheduled change budget is 48*64=3072
  // px; threshold at the 3072nd largest finite magnitude (DECISIONS I3).
  std::vector<float> magnitude;
  {
    const auto cva = rs::processing::ChangeDetector::computeCva( t1.data(), t2.data(), w, h, 4 );
    magnitude = cva.changeMagnitude;
  }
  REQUIRE( magnitude.size() == pixels );
  for ( int y = 100; y <= 115; ++y )
    for ( int x = 0; x <= 15; ++x )
      magnitude[static_cast<size_t>( y ) * w + x] = -1.0f; // nodata rect: excluded
  std::vector<float> sorted( magnitude );
  std::nth_element( sorted.begin(), sorted.begin() + ( pixels - 3072 ), sorted.end() );
  const float threshold = sorted[pixels - 3072];
  REQUIRE( threshold > 0.25f ); // true-changed magnitudes are ~0.5

  std::vector<uint8_t> artifact( pixels, 0 );
  int changed = 0;
  for ( size_t p = 0; p < pixels; ++p )
  {
    if ( magnitude[p] < 0.0f )
      artifact[p] = 255; // nodata
    else if ( magnitude[p] >= threshold )
    {
      artifact[p] = 1;
      ++changed;
    }
  }
  REQUIRE( changed == 3072 ); // exactly the scheduled budget

  const std::string artifactPath = dir.filePath( "lab_change.tif" ).toStdString();
  ensureGdal();
  {
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDatasetH ds = GDALCreate( driver, artifactPath.c_str(), w, h, 1, GDT_Byte, nullptr );
    REQUIRE( ds );
    double gt[6] = { 100.0, 0.001, 0.0, 40.0, 0.0, -0.001 };
    GDALSetGeoTransform( ds, gt );
    GDALSetProjection( ds, wkt.c_str() );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    GDALSetRasterNoDataValue( band, 255 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h, artifact.data(), w, h, GDT_Byte, 0, 0 ) == CE_None );
    GDALClose( ds );
  }

  sicnu::agent::OutputVerifier verifier;
  sicnu::agent::OutputVerifier::LabGradeOptions options;
  options.rulesDir = QString::fromStdString( std::string( kSourceDir ) + "/data/labs/grading" );
  const auto result = verifier.gradeForTeaching( QStringLiteral( "change_detect" ),
                                                 QString::fromStdString( artifactPath ),
                                                 options );
  REQUIRE( result.graded );
  INFO( "score=" << result.score << " verdict=" << result.verdict.toStdString() );
  REQUIRE( result.score == 100.0 );
}

TEST_CASE( "Lab03 / 04 / 11 specs stay loadable with intact grading refs",
           "[d15][e2e][lab]" )
{
  static const char *labs[] = {
    "lab03_classification",
    "lab04_change_detection",
    "lab11_obia_classification",
  };
  for ( const char *lab : labs )
  {
    const std::string path = std::string( kSourceDir ) + "/data/labs/" + lab + ".lab.json";
    const Json::Value root = readJsonFile( path );
    INFO( lab );
    REQUIRE( !root.isNull() );
    REQUIRE( root.isMember( "id" ) );
    REQUIRE( root.isMember( "title" ) );
    REQUIRE( root.isMember( "steps" ) );
    REQUIRE( root["steps"].isArray() );
    REQUIRE( root["steps"].size() >= 2 );
    REQUIRE( root["steps"][0].isMember( "title" ) );

    if ( root.isMember( "grading_ref" ) && root["grading_ref"].isObject()
         && root["grading_ref"].isMember( "pipeline" ) )
    {
      const std::string pipeline = root["grading_ref"]["pipeline"].asString();
      std::ifstream pipeFile( std::string( kSourceDir ) + "/" + pipeline );
      INFO( "pipeline: " << pipeline );
      REQUIRE( pipeFile.good() );
    }
  }
}
