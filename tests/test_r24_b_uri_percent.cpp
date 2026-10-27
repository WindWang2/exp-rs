// tests/test_r24_b_uri_percent.cpp — R24-B (WP-B1, GAP-G1): the '%'-literal
// fix (987161c25, issue #1579) shipped only refusal-side tests; a legitimate
// filename carrying a literal '%' ("100%_coverage.tif") had no positive
// regression, so an over-eager "any % is an escape" regression would pass
// the existing suite. Pins the positive contract.
#include <catch2/catch_test_macros.hpp>

#include "geospatial/util/resource_uri.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <string>

TEST_CASE( "literal percent filenames resolve against the base (R24-B)",
           "[r24_b][geospatial][uri][percent]" )
{
  QTemporaryDir dir;
  const QString base = dir.path();
  REQUIRE( QFile::exists( base ) );
  // resolveAgainst classifies an existing regular file as LocalFile, so the
  // positive cases carry real files.
  for ( const char *name : { "scene-100%_coverage.tif", "file%Q1.tif", "band%.tif" } )
  {
    QFile f( base + QLatin1Char( '/' ) + QString::fromLatin1( name ) );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "x" );
    f.close();
  }

  using sicnu::geo::ResourceUri;
  using sicnu::geo::ResourceKind;

  // A malformed percent escape is NOT a traversal — the decoded text stays
  // verbatim and only genuinely unsafe decodings are refused (#1505 T9).
  const ResourceUri mid = ResourceUri::resolveAgainst(
    base.toStdString(), "scene-100%_coverage.tif" );
  CHECK( mid.kind == ResourceKind::LocalFile );
  REQUIRE( mid.path.find( "scene-100%_coverage.tif" ) != std::string::npos );

  const ResourceUri edge = ResourceUri::resolveAgainst( base.toStdString(), "file%Q1.tif" );
  CHECK( edge.kind == ResourceKind::LocalFile );

  const ResourceUri trailing = ResourceUri::resolveAgainst( base.toStdString(), "band%.tif" );
  CHECK( trailing.kind == ResourceKind::LocalFile );

  // Control: an encoded traversal is STILL refused next to the positives.
  const ResourceUri escape =
    ResourceUri::resolveAgainst( base.toStdString(), "%2e%2e/outside.tif" );
  CHECK( escape.kind == ResourceKind::Invalid );
}
