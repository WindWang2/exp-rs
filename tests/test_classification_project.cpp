// Unit tests for RsClassificationProject JSON round-trip.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "rs_classification_project.h"

using Catch::Approx;

namespace
{
int argc = 1;
char arg0[] = "test";
char *argv[] = { arg0, nullptr };
void ensureCore()
{
  if ( !QCoreApplication::instance() )
    static QCoreApplication app( argc, argv );
}
} // namespace

TEST_CASE( "ClassificationProject: full field round-trip", "[classify][project]" )
{
  ensureCore();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "demo.rscproj" ) );

  RsClassificationProjectData in;
  in.version = 1;
  in.workflowStep = 6;
  in.workflowMode = QStringLiteral( "expert" );
  in.sourceRasterPath = QStringLiteral( "/data/src.tif" );
  in.roisPath = QStringLiteral( "/data/rois.shp" );
  in.classifiedRasterPath = QStringLiteral( "/data/class.tif" );
  in.postProcessRasterPath = QStringLiteral( "/data/post.tif" );
  in.postProcessVectorPath = QStringLiteral( "/data/post.gpkg" );
  in.evaluateReviewed = true;
  in.accuracySource = QStringLiteral( "holdout" );
  in.overallAccuracy = 0.912;
  in.kappa = 0.875;

  REQUIRE( RsClassificationProject::save( path, in ) );
  REQUIRE( QFile::exists( path ) );

  RsClassificationProjectData out;
  REQUIRE( RsClassificationProject::load( path, out ) );
  REQUIRE( out.version == 1 );
  REQUIRE( out.workflowStep == 6 );
  REQUIRE( out.workflowMode == QStringLiteral( "expert" ) );
  REQUIRE( out.sourceRasterPath == QStringLiteral( "/data/src.tif" ) );
  REQUIRE( out.roisPath == QStringLiteral( "/data/rois.shp" ) );
  REQUIRE( out.classifiedRasterPath == QStringLiteral( "/data/class.tif" ) );
  REQUIRE( out.postProcessRasterPath == QStringLiteral( "/data/post.tif" ) );
  REQUIRE( out.postProcessVectorPath == QStringLiteral( "/data/post.gpkg" ) );
  REQUIRE( out.evaluateReviewed );
  REQUIRE( out.accuracySource == QStringLiteral( "holdout" ) );
  REQUIRE( out.overallAccuracy == Approx( 0.912 ) );
  REQUIRE( out.kappa == Approx( 0.875 ) );
}

TEST_CASE( "ClassificationProject: missing keys keep defaults", "[classify][project]" )
{
  ensureCore();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );
  const QString path = dir.filePath( QStringLiteral( "legacy.rscproj" ) );

  // Minimal legacy-style object with only version.
  {
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    f.write( R"({"version":1})" );
  }

  RsClassificationProjectData out;
  out.workflowStep = 99; // will be reset on load
  out.evaluateReviewed = true;
  REQUIRE( RsClassificationProject::load( path, out ) );
  REQUIRE( out.workflowStep == 0 );
  REQUIRE( out.workflowMode == QStringLiteral( "wizard" ) );
  REQUIRE( out.classifiedRasterPath.isEmpty() );
  REQUIRE( out.postProcessRasterPath.isEmpty() );
  REQUIRE( out.postProcessVectorPath.isEmpty() );
  REQUIRE_FALSE( out.evaluateReviewed );
  REQUIRE( out.accuracySource.isEmpty() );
  REQUIRE( out.overallAccuracy == Approx( -1.0 ) );
  REQUIRE( out.kappa == Approx( -1.0 ) );
}

TEST_CASE( "ClassificationProject: load rejects missing file", "[classify][project]" )
{
  ensureCore();
  RsClassificationProjectData data;
  REQUIRE_FALSE( RsClassificationProject::load(
    QStringLiteral( "/tmp/does-not-exist-classify-project.rscproj" ), data ) );
}
