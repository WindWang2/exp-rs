#include <catch2/catch_test_macros.hpp>

#include "data/asset_types.h"
#include "data/data_result.h"
#include "data/source_descriptor.h"

using sicnu::data::AssetCapability;
using sicnu::data::AssetCapabilities;
using sicnu::data::AssetId;
using sicnu::data::AssetRevision;
using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;
using sicnu::data::Result;
using sicnu::data::SourceDescriptor;

TEST_CASE( "Asset IDs survive project serialization", "[data_manager]" )
{
  const AssetId original = AssetId::generate();

  REQUIRE_FALSE( original.isNull() );

  const auto restored = AssetId::fromString( original.toString() );
  REQUIRE( restored.has_value() );
  CHECK( *restored == original );
  CHECK_FALSE( AssetId::fromString( QStringLiteral( "not-an-asset-id" ) ).has_value() );
}

TEST_CASE( "Asset revisions distinguish unresolved from first resolved data", "[data_manager]" )
{
  const AssetRevision unresolved;
  const AssetRevision first = AssetRevision::initial();

  CHECK_FALSE( unresolved.isValid() );
  CHECK( first.isValid() );
  CHECK( first.value() == 1 );
}

TEST_CASE( "Source identity excludes authentication binding but includes data options",
           "[data_manager]" )
{
  SourceDescriptor first;
  first.providerKey = QStringLiteral( "gdal" );
  first.canonicalSource = QStringLiteral( "/data/scene.tif" );
  first.dataOptions.insert( QStringLiteral( "interpretation" ), QStringLiteral( "raw" ) );
  first.authConfigId = QStringLiteral( "auth-on-this-machine" );

  SourceDescriptor sameData = first;
  sameData.authConfigId = QStringLiteral( "auth-on-another-machine" );
  CHECK( sameData.sourceKey() == first.sourceKey() );

  SourceDescriptor differentInterpretation = first;
  differentInterpretation.dataOptions[QStringLiteral( "interpretation" )] =
    QStringLiteral( "scaled" );
  CHECK_FALSE( differentInterpretation.sourceKey() == first.sourceKey() );
}

TEST_CASE( "Asset capabilities compose without implying unsupported operations",
           "[data_manager]" )
{
  const AssetCapabilities rasterCapabilities =
    AssetCapability::Renderable | AssetCapability::ReadablePixels |
    AssetCapability::BandMetadata;

  CHECK( rasterCapabilities.testFlag( AssetCapability::Renderable ) );
  CHECK( rasterCapabilities.testFlag( AssetCapability::ReadablePixels ) );
  CHECK_FALSE( rasterCapabilities.testFlag( AssetCapability::EditableFeatures ) );
}

TEST_CASE( "Data errors are returned as structured diagnostics", "[data_manager]" )
{
  const Result<int> failed = Result<int>::failure(
    Diagnostic{ QStringLiteral( "source.missing" ),
                QStringLiteral( "The source cannot be found" ),
                DiagnosticSeverity::Error } );

  REQUIRE_FALSE( failed );
  REQUIRE( failed.diagnostics().size() == 1 );
  CHECK( failed.diagnostics().first().code == QStringLiteral( "source.missing" ) );
  CHECK( failed.diagnostics().first().severity == DiagnosticSeverity::Error );

  const Result<int> succeeded = Result<int>::success( 42 );
  REQUIRE( succeeded );
  CHECK( succeeded.value() == 42 );
}
