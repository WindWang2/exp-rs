#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>

#include "data/asset_types.h"
#include "data/data_asset.h"
#include "data/data_manager.h"
#include "data/derivation_record.h"
#include "data/processing_asset_resolver.h"
#include "data/source_descriptor.h"

#include "operators/framework/asset_index_pipeline.h"
#include "processing/framework/output_committer.h"

using namespace sicnu::data;
using sicnu::operators::SpectralIndexParams;
using sicnu::operators::StableOutputSpec;
using sicnu::operators::runSpectralIndexFromAsset;
using sicnu::OutputCommitter;

namespace
{

/// Resolve a fixture path relative to this source file (tests/ -> ../data).
QString fixturePath( const QString &relative )
{
  const QString here = QFileInfo( __FILE__ ).absolutePath();
  return QFileInfo( here + QStringLiteral( "/../data/" ) + relative ).absoluteFilePath();
}

/// Registers a real (GDAL-resolved) raster asset and returns its id + revision.
struct RegisteredAsset
{
  AssetId id;
  AssetRevision revision;
};

RegisteredAsset registerRasterAsset( DataManager &manager, const QString &fixture )
{
  SourceDescriptor source;
  source.providerKey = QStringLiteral( "gdal" );
  source.canonicalSource = fixturePath( fixture );
  RegisterRequest request;
  request.source = source;
  const RegisterResult result = manager.registerSource( request );
  REQUIRE( !result.assetId.isNull() );
  const auto snapshot = manager.asset( result.assetId );
  REQUIRE( snapshot.has_value() );
  return { result.assetId, snapshot->revision() };
}

StableOutputSpec makeOutput( QTemporaryDir &dir, const QString &name, bool autoLoad )
{
  StableOutputSpec output;
  output.tempPath = dir.filePath( QStringLiteral( "scratch.tif" ) );
  output.stablePath = dir.filePath( name );
  output.autoLoad = autoLoad;
  return output;
}

} // namespace

TEST_CASE( "Spectral index runs from a Data Asset and registers a derived output",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  // Lease count is zero before the run.
  CHECK( manager.leaseCount( input.id ) == 0 );

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/true ),
    resolver, committer );

  REQUIRE( result );
  CHECK( !result.value().isNull() );

  // The Task lease was released when the run completed.
  CHECK( manager.leaseCount( input.id ) == 0 );

  // Output registered as a distinct raster asset.
  CHECK( result.value() != input.id );
  const auto snapshot = manager.asset( result.value() );
  REQUIRE( snapshot.has_value() );
  CHECK( snapshot->kind() == AssetKind::Raster );

  // Provenance ties the output to the algorithm and its input.
  const auto provenance = manager.provenance( result.value() );
  REQUIRE( provenance.has_value() );
  CHECK( provenance->algorithmId == QStringLiteral( "rs:spectral_index" ) );
  CHECK( provenance->outputAssetId == result.value() );
  REQUIRE( provenance->inputs.size() == 1 );
  CHECK( provenance->inputs.first().assetId == input.id );
  CHECK( provenance->inputs.first().revision == input.revision );

  // Display was requested (autoLoad=true).
  REQUIRE( displaySpy.count() == 1 );
  CHECK( displaySpy.takeFirst().at( 0 ).value<AssetId>() == result.value() );

  // The temp was consumed; the stable output exists.
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "scratch.tif" ) ) ) );
  CHECK( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}

TEST_CASE( "Display of a spectral-index output is opt-in",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/false ),
    resolver, committer );

  REQUIRE( result );
  CHECK( displaySpy.count() == 0 );
}

TEST_CASE( "A stale input revision is rejected before work starts",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );
  QSignalSpy displaySpy( &committer, &OutputCommitter::displayRequested );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  // Queue against a revision that has already been superseded.
  const AssetRevision staleRevision = AssetRevision::fromValue( input.revision.value() + 1 );

  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 5;
  params.red = 4;

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, staleRevision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/true ),
    resolver, committer );

  REQUIRE_FALSE( result );
  REQUIRE_FALSE( result.diagnostics().isEmpty() );

  // Nothing was registered, nothing displayed, no stable output.
  CHECK( manager.assets().size() == 1 ); // only the input asset
  CHECK( displaySpy.count() == 0 );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}

TEST_CASE( "A failed run registers nothing and discards the temporary output",
           "[spectral_index_asset_pipeline]" )
{
  QTemporaryDir dir;
  DataManager manager;
  ProcessingAssetResolver resolver( &manager );
  OutputCommitter committer( &manager );

  const RegisteredAsset input = registerRasterAsset(
    manager, QStringLiteral( "samples/landsat_sample.tif" ) );

  // An impossible band number: the operator validates and throws before
  // writing any output.
  SpectralIndexParams params;
  params.index = QStringLiteral( "NDVI" );
  params.nir = 99;
  params.red = 99;

  // Pre-create a temp output so discardTemporary has something to remove.
  {
    QFile scratch( dir.filePath( QStringLiteral( "scratch.tif" ) ) );
    REQUIRE( scratch.open( QIODevice::WriteOnly ) );
    scratch.write( "placeholder" );
  }

  const auto result = runSpectralIndexFromAsset(
    AssetRef{ input.id, input.revision }, params,
    makeOutput( dir, QStringLiteral( "ndvi_stable.tif" ), /*autoLoad=*/false ),
    resolver, committer );

  REQUIRE_FALSE( result );

  // No derived output registered; the input asset is the only one.
  CHECK( manager.assets().size() == 1 );
  // The temporary output was discarded.
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "scratch.tif" ) ) ) );
  CHECK_FALSE( QFile::exists( dir.filePath( QStringLiteral( "ndvi_stable.tif" ) ) ) );
}
