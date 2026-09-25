// test_feature_schema.cpp — F12: named feature schema, fingerprint stability,
// and the NaN/NoData assembly contract.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_feature_schema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <limits>
#include <vector>
#include <QJsonValue>

using Catch::Approx;

TEST_CASE( "schema: append rejects duplicates and empty names", "[classify][features]" )
{
  RsFeatureSchema schema;
  REQUIRE( schema.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band,
                          QStringLiteral( "band:1" ) ) );
  REQUIRE( !schema.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Index,
                           QStringLiteral( "ndvi" ) ) );
  REQUIRE( !schema.append( QString(), RsFeatureSchema::Kind::Band, QString() ) );
  REQUIRE( schema.count() == 1 );
}

TEST_CASE( "fingerprint: stable, order-sensitive, content-sensitive", "[classify][features]" )
{
  RsFeatureSchema a;
  REQUIRE( a.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:1" ) ) );
  REQUIRE( a.append( QStringLiteral( "b2" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:2" ) ) );

  RsFeatureSchema same;
  REQUIRE( same.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:1" ) ) );
  REQUIRE( same.append( QStringLiteral( "b2" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:2" ) ) );
  REQUIRE( a.fingerprint() == same.fingerprint() );
  REQUIRE( a.fingerprint().size() == 16 );

  // Order swap must change the fingerprint (column order is semantic).
  RsFeatureSchema swapped;
  REQUIRE( swapped.append( QStringLiteral( "b2" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:2" ) ) );
  REQUIRE( swapped.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:1" ) ) );
  REQUIRE( a.fingerprint() != swapped.fingerprint() );

  // Source change must change it too.
  RsFeatureSchema otherSource;
  REQUIRE( otherSource.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:3" ) ) );
  REQUIRE( otherSource.append( QStringLiteral( "b2" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:2" ) ) );
  REQUIRE( a.fingerprint() != otherSource.fingerprint() );

  REQUIRE( RsFeatureSchema().fingerprint().isEmpty() );
}

TEST_CASE( "fingerprint: literal known-answer for the reference vector", "[classify][features]" )
{
  // Independent oracle: FNV-1a 64 (offset 14695981039346656037, prime
  // 1099511628211) over "exp-rs-feature-schema/1|b1|band|band:1", computed
  // with an external reference implementation (python); recorded as a hex
  // literal. If this ever changes, the change is a schema-version bump.
  RsFeatureSchema schema;
  REQUIRE( schema.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:1" ) ) );
  REQUIRE( schema.fingerprint() == QStringLiteral( "ca872072c88050e5" ) );
}

TEST_CASE( "schema JSON round-trip and drift gate", "[classify][features]" )
{
  RsFeatureSchema schema;
  REQUIRE( schema.append( QStringLiteral( "b1" ), RsFeatureSchema::Kind::Band, QStringLiteral( "band:1" ) ) );
  REQUIRE( schema.append( QStringLiteral( "ndvi" ), RsFeatureSchema::Kind::Index,
                          QStringLiteral( "formula:ndvi" ) ) );
  REQUIRE( schema.append( QStringLiteral( "glcm_contrast" ), RsFeatureSchema::Kind::Texture,
                          QStringLiteral( "glcm:d1:a0:q32" ) ) );

  const QJsonObject obj = schema.toJson();
  RsFeatureSchema restored;
  REQUIRE( restored.fromJson( obj ) );
  REQUIRE( restored.names() == schema.names() );
  REQUIRE( restored.fingerprint() == schema.fingerprint() );

  // Drift: flip feature order in the document → recorded fingerprint no
  // longer matches → fromJson fails closed.
  QJsonObject tampered = obj;
  QJsonArray feats = tampered.value( QStringLiteral( "features" ) ).toArray();
  const QJsonValue first = feats.at( 0 );
  feats[0] = feats.at( 1 );
  feats[1] = first;
  tampered.insert( QStringLiteral( "features" ), feats );
  RsFeatureSchema drifted;
  REQUIRE( !drifted.fromJson( tampered ) );
  REQUIRE( drifted.isEmpty() );

  // Unknown kind fails closed.
  QJsonObject badKind = obj;
  QJsonArray arr = badKind.value( QStringLiteral( "features" ) ).toArray();
  QJsonObject f0 = arr.at( 0 ).toObject();
  f0.insert( QStringLiteral( "kind" ), QStringLiteral( "quantum" ) );
  arr[0] = f0;
  badKind.insert( QStringLiteral( "features" ), arr );
  RsFeatureSchema bad;
  REQUIRE( !bad.fromJson( badKind ) );
}

TEST_CASE( "assembler: row-major assembly, NaN sentinel, valid counts", "[classify][features]" )
{
  RsFeatureAssembler asm1;
  RsFeatureAssembler::Column band1;
  band1.name = QStringLiteral( "b1" );
  band1.kind = RsFeatureSchema::Kind::Band;
  band1.source = QStringLiteral( "band:1" );
  band1.values = { 1.0f, 2.0f, -9999.0f };
  RsFeatureAssembler::Column ndvi;
  ndvi.name = QStringLiteral( "ndvi" );
  ndvi.kind = RsFeatureSchema::Kind::Index;
  ndvi.source = QStringLiteral( "formula:ndvi" );
  ndvi.values = { 0.5f, -9999.0f, 0.7f };

  REQUIRE( asm1.addColumn( band1 ) );
  REQUIRE( asm1.addColumn( ndvi ) );
  asm1.setNoDataValue( -9999.0f );

  std::vector<float> rowMajor;
  std::vector<int> validCounts;
  REQUIRE( asm1.assemble( rowMajor, validCounts ) );
  REQUIRE( asm1.rowCount() == 3 );
  REQUIRE( asm1.columnCount() == 2 );
  REQUIRE( rowMajor.size() == 6 );
  // Row-major: row0 = (1.0, 0.5), row1 = (2.0, NaN), row2 = (NaN, 0.7).
  REQUIRE( rowMajor[0] == Approx( 1.0 ).margin( 1e-6 ) );
  REQUIRE( rowMajor[1] == Approx( 0.5 ).margin( 1e-6 ) );
  REQUIRE( rowMajor[2] == Approx( 2.0 ).margin( 1e-6 ) );
  REQUIRE( std::isnan( rowMajor[3] ) );
  REQUIRE( std::isnan( rowMajor[4] ) );
  REQUIRE( rowMajor[5] == Approx( 0.7 ).margin( 1e-6 ) );
  REQUIRE( validCounts == std::vector<int> { 2, 2 } );

  const RsFeatureSchema schema = asm1.schema();
  REQUIRE( schema.count() == 2 );
  REQUIRE( schema.at( 0 ).name == QLatin1String( "b1" ) );
  REQUIRE( schema.at( 1 ).kind == RsFeatureSchema::Kind::Index );
}

TEST_CASE( "assembler: rejects duplicates and ragged lengths", "[classify][features]" )
{
  RsFeatureAssembler asm1;
  RsFeatureAssembler::Column c1;
  c1.name = QStringLiteral( "b1" );
  c1.values = { 1.0f, 2.0f };
  REQUIRE( asm1.addColumn( c1 ) );

  RsFeatureAssembler::Column dup = c1;
  REQUIRE( !asm1.addColumn( dup ) );

  RsFeatureAssembler::Column ragged;
  ragged.name = QStringLiteral( "b2" );
  ragged.values = { 1.0f, 2.0f, 3.0f };
  REQUIRE( !asm1.addColumn( ragged ) );

  RsFeatureAssembler::Column empty;
  empty.name = QString();
  REQUIRE( !asm1.addColumn( empty ) );
  REQUIRE( asm1.columnCount() == 1 );
}

TEST_CASE( "assembler: without NoData config only non-finite input is NaN", "[classify][features]" )
{
  RsFeatureAssembler asm1;
  RsFeatureAssembler::Column c1;
  c1.name = QStringLiteral( "x" );
  c1.values = { 1.0f, -9999.0f, std::numeric_limits<float>::infinity() };
  REQUIRE( asm1.addColumn( c1 ) );
  std::vector<float> out;
  std::vector<int> counts;
  REQUIRE( asm1.assemble( out, counts ) );
  REQUIRE( out[0] == Approx( 1.0 ).margin( 1e-6 ) );
  REQUIRE( out[1] == Approx( -9999.0 ).margin( 1e-3 ) ); // kept as data
  REQUIRE( std::isnan( out[2] ) ); // inf → NaN sentinel
  REQUIRE( counts[0] == 2 );
}

TEST_CASE( "assembler: empty assembly fails", "[classify][features]" )
{
  RsFeatureAssembler asm1;
  std::vector<float> out;
  std::vector<int> counts;
  REQUIRE( !asm1.assemble( out, counts ) );
}
