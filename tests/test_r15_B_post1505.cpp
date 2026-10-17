// R15-B item 8: oracles for the #1481 / #1505 review threads on files that
// were read-only while #1505 was open (resource_uri.cpp, param_guard). #1505
// has merged, so each thread is checked against the live code here: a
// thread already closed on master stays GREEN on first run (FIXED_ON_MASTER),
// one that fails is fixed in this track (RED -> GREEN).
#include "geospatial/cog/cog_validator.h"
#include "geospatial/common.h"
#include "geospatial/convert/raster_convert.h"
#include "geospatial/io/param_guard.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/util/resource_uri.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gdal.h>

#include <functional>
#include <string>

using sicnu::geo::ResourceKind;
using sicnu::geo::ResourceUri;

TEST_CASE( "1481-T6: file://LOCALHOST is the local authority (case-insensitive)", "[r15b][post1505]" )
{
  const ResourceUri upper = ResourceUri::parse( "file://LOCALHOST/tmp/r15b/x.tif" );
  CHECK( upper.kind != ResourceKind::Invalid );
  CHECK( upper.gdalOpenTarget() == "/tmp/r15b/x.tif" );
  const ResourceUri mixed = ResourceUri::parse( "file://LocalHost/tmp/r15b/x.tif" );
  CHECK( mixed.gdalOpenTarget() == "/tmp/r15b/x.tif" );
}

TEST_CASE( "1481-T7: decoded NUL in a file URI is refused", "[r15b][post1505]" )
{
  const ResourceUri uri = ResourceUri::parse( "file:///safe/data.tif%00.attacker" );
  CHECK( uri.kind == ResourceKind::Invalid );
  CHECK_THROWS( sicnu::geo::io::checkSourcePath( "file:///safe/data.tif%00.attacker" ) );
}

TEST_CASE( "1481-T8: CheckedPath opens the decoded file-URI path, not the URI", "[r15b][post1505]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString file = QDir( tmp.path() ).filePath( QStringLiteral( "a b.tif" ) );
  {
    QFile f( file );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( "x" );
  }
  const std::string uri = "file://" + QDir( tmp.path() ).absolutePath().toStdString() + "/a%20b.tif";
  const auto checked = sicnu::geo::io::checkSourcePath( uri );
  CHECK( checked.openPath() == file.toStdString() );
}

TEST_CASE( "1481-T9: an absolute file URI in a catalog is anchored, not joined", "[r15b][post1505]" )
{
  const ResourceUri resolved = ResourceUri::resolveAgainst( "/base/catalog", "file:///abs/data/x.tif" );
  CHECK( resolved.kind != ResourceKind::Invalid );
  CHECK( resolved.gdalOpenTarget() == "/abs/data/x.tif" );
}

TEST_CASE( "1481-T19: file-URI roots keep their root separator in canonical()", "[r15b][post1505]" )
{
  const ResourceUri root = ResourceUri::parse( "file:///" );
  CHECK( root.kind != ResourceKind::Invalid );
  CHECK( root.canonical() != "file:" );
  const ResourceUri reparsed = ResourceUri::parse( root.canonical() );
  CHECK( reparsed.kind != ResourceKind::Invalid );
  CHECK( reparsed.gdalOpenTarget() == "/" );
  const ResourceUri drive = ResourceUri::parse( "file:///C:/" );
  CHECK( drive.canonical() != "file:///C:" );
}

TEST_CASE( "1505-T9: literal / malformed percent in a relative filename still resolves", "[r15b][post1505]" )
{
  const ResourceUri a = ResourceUri::resolveAgainst( "/base", "reports/100%_coverage.tif" );
  CHECK( a.kind != ResourceKind::Invalid );
  CHECK( a.gdalOpenTarget() == "/base/reports/100%_coverage.tif" );
  const ResourceUri b = ResourceUri::resolveAgainst( "/base", "file%Q1.tif" );
  CHECK( b.kind != ResourceKind::Invalid );
  CHECK( b.gdalOpenTarget() == "/base/file%Q1.tif" );
  // Traversal stays refused, including encoded and double-encoded forms.
  CHECK( ResourceUri::resolveAgainst( "/base", "../x.tif" ).kind == ResourceKind::Invalid );
  CHECK( ResourceUri::resolveAgainst( "/base", "%2e%2e/x.tif" ).kind == ResourceKind::Invalid );
  CHECK( ResourceUri::resolveAgainst( "/base", "%252e%252e/x.tif" ).kind == ResourceKind::Invalid );
  CHECK( ResourceUri::resolveAgainst( "/base", "a%00b.tif" ).kind == ResourceKind::Invalid );
}

// ---------------------------------------------------------------------------
// Item 7: the B13 uncommitted-journal gate covered RasterReader/VectorReader
// only. Metadata extraction (inspectRaster/inspectVector/inspectMultidim —
// the B06 fact chain), COG validation and the convert inputs opened the same
// possibly mixed-generation group without the gate.
// ---------------------------------------------------------------------------
namespace
{
std::string writeTinyTif( const QString &dir )
{
  GDALAllRegister();
  const std::string path = QDir( dir ).filePath( QStringLiteral( "g.tif" ) ).toStdString();
  GDALDriverH drv = GDALGetDriverByName( "GTiff" );
  REQUIRE( drv != nullptr );
  GDALDatasetH ds = GDALCreate( drv, path.c_str(), 4, 4, 1, GDT_Byte, nullptr );
  REQUIRE( ds != nullptr );
  GDALClose( ds );
  return path;
}
void touch( const std::string &path )
{
  QFile f( QString::fromStdString( path ) );
  REQUIRE( f.open( QIODevice::WriteOnly ) );
  f.write( "{}" );
}
bool refusedAsInterrupted( const std::function<void()> &fn )
{
  try
  {
    fn();
  }
  catch ( const sicnu::geo::GeoError &e )
  {
    return std::string( e.what() ).find( "interrupted mid-publish" ) != std::string::npos;
  }
  return false;
}
} // namespace

TEST_CASE( "Item7: metadata / COG / convert readers refuse an uncommitted publish journal",
           "[r15b][item7][journal]" )
{
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const std::string tif = writeTinyTif( tmp.path() );
  const std::string out = QDir( tmp.path() ).filePath( QStringLiteral( "out.tif" ) ).toStdString();
  touch( tif + ".publish-journal" );
  CHECK( refusedAsInterrupted( [&] { sicnu::geo::inspectRaster( tif ); } ) );
  CHECK( refusedAsInterrupted( [&] { sicnu::geo::inspectAny( tif ); } ) );
  CHECK( refusedAsInterrupted( [&] { sicnu::geo::validateCog( tif ); } ) );
  CHECK( refusedAsInterrupted( [&] { sicnu::geo::translateRaster( tif, out, sicnu::geo::TranslateOptions{} ); } ) );
  CHECK_FALSE( QFile::exists( QString::fromStdString( out ) ) );

  // A committed marker (only backup sweeping interrupted) does not block.
  QFile::remove( QString::fromStdString( tif + ".publish-journal" ) );
  touch( tif + ".publish-journal.committed" );
  CHECK_NOTHROW( sicnu::geo::inspectRaster( tif ) );
  CHECK_NOTHROW( sicnu::geo::validateCog( tif ) );
}
