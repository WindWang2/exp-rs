#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "data/derivation_record.h"

using namespace sicnu::data;

namespace
{

DerivationRecord populatedRecord()
{
  DerivationRecord record;
  record.algorithmId = QStringLiteral( "sicnu:ndvi" );
  record.algorithmVersion = QStringLiteral( "1.2.0" );
  // JSON-native values of every shape, including an integer: Qt 6 QJsonValue
  // preserves integers exactly, so the snapshot is lossless by construction.
  record.parameters = QJsonObject{
    { QStringLiteral( "redBand" ), 1 },
    { QStringLiteral( "nirBand" ), 2 },
    { QStringLiteral( "scale" ), 1.5 },
    { QStringLiteral( "clamp" ), true },
    { QStringLiteral( "maskValues" ), QJsonArray{ 0.0, 255.0 } },
    { QStringLiteral( "metadata" ), QJsonObject{
        { QStringLiteral( "note" ), QStringLiteral( "nightly run" ) },
      } },
  };

  DerivationInput red;
  red.assetId = AssetId::generate();
  red.revision = AssetRevision::initial();
  red.bandReferences = { QStringLiteral( "B4" ) };
  red.valueDomain = QStringLiteral( "reflectance" );

  DerivationInput nir;
  nir.assetId = AssetId::generate();
  nir.revision = AssetRevision::fromValue( 7 );
  nir.bandReferences = { QStringLiteral( "B8" ), QStringLiteral( "B8A" ) };

  record.inputs = { red, nir };
  record.outputAssetId = AssetId::generate();
  record.taskReference = QStringLiteral( "task-2026-07-25-0042" );
  record.softwareVersion = QStringLiteral( "SICNU GEO RS 0.9.1" );
  record.completedAtUtc = QDateTime::fromString( QStringLiteral( "2026-07-25T10:11:12.345Z" ),
                                                 Qt::ISODateWithMs );
  record.authConfigId = QStringLiteral( "auth-cfg-7" );
  return record;
}

QSet<QString> keySet( const QJsonObject &json )
{
  const QStringList keys = json.keys();
  return QSet<QString>( keys.begin(), keys.end() );
}

} // namespace

TEST_CASE( "Derivation record round trips a fully populated record losslessly",
           "[data][derivation]" )
{
  const DerivationRecord record = populatedRecord();

  const QJsonObject json = record.toJson();
  const auto restored = DerivationRecord::fromJson( json );

  REQUIRE( restored );
  CHECK( restored.value() == record );
}

TEST_CASE( "Derivation record round trips a minimal record", "[data][derivation]" )
{
  const DerivationRecord record;

  const auto restored = DerivationRecord::fromJson( record.toJson() );

  REQUIRE( restored );
  CHECK( restored.value() == record );
}

TEST_CASE( "Derivation record serialization excludes credential material",
           "[data][derivation]" )
{
  const DerivationRecord record = populatedRecord();
  const QJsonObject json = record.toJson();

  // The shape is fixed by construction: only these keys may ever appear at the
  // top level. Authentication material is limited to the non-secret
  // authConfigId reference; there is no field that could carry a password,
  // token, or secret.
  const QSet<QString> allowedTopLevel{
    QStringLiteral( "algorithmId" ),
    QStringLiteral( "algorithmVersion" ),
    QStringLiteral( "parameters" ),
    QStringLiteral( "inputs" ),
    QStringLiteral( "outputAssetId" ),
    QStringLiteral( "taskReference" ),
    QStringLiteral( "softwareVersion" ),
    QStringLiteral( "completedAt" ),
    QStringLiteral( "authConfigId" ),
  };
  CHECK( keySet( json ) == allowedTopLevel );

  const QSet<QString> allowedInputKeys{
    QStringLiteral( "assetId" ),
    QStringLiteral( "revision" ),
    QStringLiteral( "bandReferences" ),
    QStringLiteral( "valueDomain" ),
  };
  const QJsonArray inputs = json.value( QStringLiteral( "inputs" ) ).toArray();
  REQUIRE( inputs.size() == 2 );
  for ( const QJsonValue &input : inputs )
    CHECK( keySet( input.toObject() ) == allowedInputKeys );

  CHECK( json.value( QStringLiteral( "authConfigId" ) ).toString()
         == QStringLiteral( "auth-cfg-7" ) );
}

TEST_CASE( "Derivation record revision survives large values losslessly",
           "[data][derivation]" )
{
  DerivationRecord record;
  DerivationInput input;
  input.assetId = AssetId::generate();
  input.revision = AssetRevision::fromValue( ( quint64( 1 ) << 53 ) + 1 );
  record.inputs = { input };

  const auto restored = DerivationRecord::fromJson( record.toJson() );

  REQUIRE( restored );
  CHECK( restored.value() == record );
}

TEST_CASE( "Derivation record round trips losslessly through JSON text",
           "[data][derivation]" )
{
  const DerivationRecord record = populatedRecord();

  const QByteArray text = QJsonDocument( record.toJson() ).toJson( QJsonDocument::Compact );
  const auto restored = DerivationRecord::fromJson(
    QJsonDocument::fromJson( text ).object() );

  REQUIRE( restored );
  CHECK( restored.value() == record );
}

TEST_CASE( "Derivation record rejects an invalid completion timestamp",
           "[data][derivation]" )
{
  QJsonObject json = populatedRecord().toJson();
  json.insert( QStringLiteral( "completedAt" ), QStringLiteral( "last tuesday" ) );

  const auto restored = DerivationRecord::fromJson( json );

  REQUIRE_FALSE( restored );
  CHECK( restored.diagnostics().first().code
         == QStringLiteral( "derivation.invalid" ) );
}

TEST_CASE( "Derivation record rejects an invalid asset id", "[data][derivation]" )
{
  QJsonObject json = populatedRecord().toJson();
  QJsonArray inputs = json.value( QStringLiteral( "inputs" ) ).toArray();
  QJsonObject first = inputs.first().toObject();
  first.insert( QStringLiteral( "assetId" ), QStringLiteral( "not-a-uuid" ) );
  inputs.replace( 0, first );
  json.insert( QStringLiteral( "inputs" ), inputs );

  const auto restored = DerivationRecord::fromJson( json );

  REQUIRE_FALSE( restored );
  REQUIRE_FALSE( restored.diagnostics().isEmpty() );
  CHECK( restored.diagnostics().first().code
         == QStringLiteral( "derivation.invalid" ) );
}

TEST_CASE( "Derivation record rejects an invalid output asset id",
           "[data][derivation]" )
{
  QJsonObject json = populatedRecord().toJson();
  json.insert( QStringLiteral( "outputAssetId" ), QStringLiteral( "garbage" ) );

  const auto restored = DerivationRecord::fromJson( json );

  REQUIRE_FALSE( restored );
  CHECK( restored.diagnostics().first().code
         == QStringLiteral( "derivation.invalid" ) );
}

TEST_CASE( "makeTaskDerivation builds the shared task provenance shape", "[data][derivation]" )
{
  const DerivationRecord record = makeTaskDerivation(
    QStringLiteral( "rs:change_detection" ),
    QJsonObject{ { QStringLiteral( "method" ), QStringLiteral( "cva" ) } },
    QStringLiteral( "task-7" ) );

  CHECK( record.algorithmId == QStringLiteral( "rs:change_detection" ) );
  CHECK( record.parameters.value( QStringLiteral( "method" ) ).toString()
         == QStringLiteral( "cva" ) );
  CHECK( record.taskReference == QStringLiteral( "task-7" ) );
  CHECK( record.completedAtUtc.isValid() );
  CHECK( record.inputs.isEmpty() );
  // The output asset id is stamped by attachDerivationRecord, not by the helper.
  CHECK( record.outputAssetId.isNull() );
}
