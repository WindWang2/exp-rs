/***************************************************************************
  tests/test_r18_e_batch_cli.cpp — R18-E E03 CLI batch discovery lane.

  T301 (D2): `lab --batch` discovery still used a type-filtered listing
  (QDir::Files), which FOLLOWS symlinks — a linked file grades content from
  outside the submissions root and publishes its digest — and silently
  drops dangling links. The GUI batch path (teaching_admin) already refuses
  links with a typed, visible skip; the CLI twin must hold the same line:
  links are refused visibly in the summary, never graded, never vanished.
 ***************************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "cli/lab_batch_runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <unistd.h>

#include <filesystem>

using namespace sicnu::cli;

namespace
{

std::filesystem::path tempRoot()
{
  auto dir = std::filesystem::temp_directory_path()
    / ( "r18e_batch_cli_" + std::to_string( ::getpid() ) );
  std::filesystem::remove_all( dir );
  std::filesystem::create_directories( dir );
  return dir;
}

void writeFile( const std::filesystem::path &p, const QByteArray &bytes )
{
  QFile f( QString::fromStdString( p.string() ) );
  REQUIRE( f.open( QIODevice::WriteOnly ) );
  f.write( bytes );
  f.close();
}

bool makeLink( const std::filesystem::path &target, const std::filesystem::path &link )
{
  QFile::remove( QString::fromStdString( link.string() ) );
  return QFile::link( QString::fromStdString( target.string() ),
                      QString::fromStdString( link.string() ) );
}

sicnu::agent::OutputVerifier::LabGradeResult passingGrade()
{
  sicnu::agent::OutputVerifier::LabGradeResult result;
  result.graded = true;
  result.verdict = QStringLiteral( "pass" );
  result.score = 90.0;
  return result;
}

} // namespace

TEST_CASE( "r18e T301: symlinked submissions are refused visibly, never graded",
           "[cli][batch][discovery][r18_e]" )
{
  const auto root = tempRoot();
  const QString submissionsDir = QString::fromStdString( ( root / "subs" ).string() );
  const QString csvPath = QString::fromStdString( ( root / "grades.csv" ).string() );
  std::filesystem::create_directories( root / "subs" );

  // A real submission, a live symlink pointing INSIDE the tree, a live
  // symlink pointing OUTSIDE the tree, and a dangling link.
  writeFile( root / "subs" / "s01.tif", QByteArray( "real submission bytes" ) );
  writeFile( root / "outside.tif", QByteArray( "outside the submissions root" ) );
  REQUIRE( makeLink( root / "subs" / "s01.tif", root / "subs" / "inside.tif" ) );
  REQUIRE( makeLink( root / "outside.tif", root / "subs" / "outside.tif" ) );
  REQUIRE( makeLink( root / "does_not_exist.tif", root / "subs" / "dangling.tif" ) );

  int gradedCalls = 0;
  QStringList gradedPaths;
  const LabBatchSummary summary = LabBatchRunner::run(
    submissionsDir, QStringLiteral( "labX" ), csvPath,
    [ & ]( const QString &artifactPath )
    {
      ++gradedCalls;
      gradedPaths.append( artifactPath );
      return passingGrade();
    } );

  // Only the real file is graded; neither link is followed.
  CHECK( summary.total == 1 );
  REQUIRE( gradedCalls == 1 );
  CHECK( QFileInfo( gradedPaths.first() ).fileName() == QStringLiteral( "s01.tif" ) );

  // Every link is accounted for by name in the summary — nothing vanishes.
  REQUIRE( summary.refusedEntries.size() == 3 );
  QStringList refusedNames;
  for ( const auto &entry : summary.refusedEntries )
    refusedNames.append( entry.first );
  for ( const char *expected : { "inside.tif", "outside.tif", "dangling.tif" } )
  {
    CHECK( refusedNames.contains( QString::fromLatin1( expected ) ) );
  }

  std::filesystem::remove_all( root );
}

TEST_CASE( "r18e T301b: a clean directory behaves exactly as before",
           "[cli][batch][discovery][r18_e]" )
{
  const auto root = tempRoot();
  const QString submissionsDir = QString::fromStdString( ( root / "subs" ).string() );
  const QString csvPath = QString::fromStdString( ( root / "grades.csv" ).string() );
  std::filesystem::create_directories( root / "subs" );
  writeFile( root / "subs" / "s01.tif", QByteArray( "a" ) );
  writeFile( root / "subs" / "s02.tif", QByteArray( "b" ) );
  // Hidden files and a nested directory keep their legacy invisibility.
  writeFile( root / "subs" / ".hidden.tif", QByteArray( "h" ) );
  std::filesystem::create_directories( root / "subs" / "nested" );

  const LabBatchSummary summary = LabBatchRunner::run(
    submissionsDir, QStringLiteral( "labX" ), csvPath,
    []( const QString & ) { return passingGrade(); } );

  CHECK( summary.total == 2 );
  CHECK( summary.graded == 2 );
  CHECK( summary.refusedEntries.isEmpty() );

  std::filesystem::remove_all( root );
}
