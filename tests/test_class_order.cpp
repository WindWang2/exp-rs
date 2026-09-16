// test_class_order.cpp — F12 Oracle 2: class order contract is machine-verifiable.
// Independent oracle: hand-written expected arrays; no classifier involved.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "rs_class_order.h"

#include <QJsonDocument>

using Catch::Approx;

TEST_CASE( "sortedClassIds: dedups, sorts, preserves distinct ids", "[classify][classorder]" )
{
  const QVector<int> raw = { 7, 1, 3, 7, 1, 10 };
  const QVector<int> expected = { 1, 3, 7, 10 };
  REQUIRE( RsClassOrder::sortedClassIds( raw ) == expected );
  REQUIRE( RsClassOrder::sortedClassIds( {} ).isEmpty() );
  // Negative ids are legal class labels.
  const QVector<int> negative = { 2, -1, 2 };
  REQUIRE( RsClassOrder::sortedClassIds( negative ) == QVector<int> { -1, 2 } );
}

TEST_CASE( "isValid: strict ascending, rejects duplicates and empty", "[classify][classorder]" )
{
  REQUIRE( RsClassOrder::isValid( { 1, 3, 7 } ) );
  REQUIRE( !RsClassOrder::isValid( {} ) );
  REQUIRE( !RsClassOrder::isValid( { 1, 1, 2 } ) );
  REQUIRE( !RsClassOrder::isValid( { 3, 1 } ) );
  REQUIRE( RsClassOrder::isValid( { 42 } ) );
}

TEST_CASE( "matchesColumnCount and columnOf", "[classify][classorder]" )
{
  const QVector<int> order = { 2, 5, 9 };
  REQUIRE( RsClassOrder::matchesColumnCount( order, 3 ) );
  REQUIRE( !RsClassOrder::matchesColumnCount( order, 2 ) );
  REQUIRE( !RsClassOrder::matchesColumnCount( { 5, 5 }, 2 ) );
  REQUIRE( RsClassOrder::columnOf( order, 5 ) == 1 );
  REQUIRE( RsClassOrder::columnOf( order, 4 ) == -1 );
  REQUIRE( RsClassOrder::columnOf( order, 10 ) == -1 );
}

TEST_CASE( "JSON round-trip uses the bare-array companion format", "[classify][classorder]" )
{
  const QVector<int> order = { 1, 3, 7 };
  const QJsonDocument doc = RsClassOrder::toJson( order );
  // Independent oracle: the literal format documented in
  // rs_classifier_random_forest.cpp (bare array, compact).
  REQUIRE( doc.toJson( QJsonDocument::Compact ) == QByteArrayLiteral( "[1,3,7]" ) );

  QVector<int> parsed;
  REQUIRE( RsClassOrder::fromJson( doc, parsed ) );
  REQUIRE( parsed == order );
}

TEST_CASE( "fromJson: fails closed on malformed or non-ascending input", "[classify][classorder]" )
{
  QVector<int> parsed;
  REQUIRE( !RsClassOrder::fromJson( QJsonDocument(), parsed ) );
  REQUIRE( parsed.isEmpty() );

  const QJsonDocument objectDoc = QJsonDocument( QJsonObject { { "a", 1 } } );
  REQUIRE( !RsClassOrder::fromJson( objectDoc, parsed ) );

  REQUIRE( !RsClassOrder::fromJsonArray( QJsonArray { 3, 1 }, parsed ) );
  REQUIRE( parsed.isEmpty() );
  REQUIRE( !RsClassOrder::fromJsonArray( QJsonArray { 1, 1 }, parsed ) );
  REQUIRE( parsed.isEmpty() );
  REQUIRE( !RsClassOrder::fromJsonArray( QJsonArray { 1.5 }, parsed ) );
  REQUIRE( parsed.isEmpty() );
  REQUIRE( !RsClassOrder::fromJsonArray( QJsonArray { QJsonValue( "x" ) }, parsed ) );
}

TEST_CASE( "order survives a document round-trip with value fidelity", "[classify][classorder]" )
{
  const QVector<int> order = { -3, 0, 12, 4096 };
  const QJsonDocument doc = RsClassOrder::toJson( order );
  const QJsonDocument reparsed = QJsonDocument::fromJson( doc.toJson() );
  QVector<int> parsed;
  REQUIRE( RsClassOrder::fromJson( reparsed, parsed ) );
  REQUIRE( parsed == order );
}
