// tests/test_r27_t04_export_atomic.cpp — R27-T04 (#1792)
//
// Contract under test: the classification Step-7 export copy
//   1. refuses dest == src (canonical compare) — on HEAD QFile::remove(dest)
//      deleted the SOURCE file before the copy;
//   2. publishes staged-atomically — the copy goes to a staged temp first
//      and renames over the destination (never remove-then-copy), leaving
//      no staging residue.
// The tests drive the dialog-free seam (copyPathTo) directly; the only
// modal is the dest==src warning box, closed by a timed dismisser.
#include <catch2/catch_test_macros.hpp>

#include "classification/qgsclassificationmainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QtTest>

namespace
{
  int fake_argc = 1;
  char fake_argv0[] = "test_r27_t04_export_atomic";
  char *fake_argv[] = { fake_argv0, nullptr };

  QApplication *ensureApp()
  {
    if ( !QCoreApplication::instance() )
    {
      qputenv( "QT_QPA_PLATFORM", "offscreen" );
      return new QApplication( fake_argc, fake_argv );
    }
    return static_cast<QApplication *>( QCoreApplication::instance() );
  }

  /// Close any modal widget (the dest==src warning box) so its exec() ends.
  void armModalCloser( int budget = 200 )
  {
    if ( budget <= 0 )
      return;
    QTimer::singleShot( 10, ensureApp(), [budget] {
      if ( QWidget *modal = QApplication::activeModalWidget() )
        modal->close();
      armModalCloser( budget - 1 );
    } );
  }

  void writeFile( const QString &path, const QByteArray &bytes )
  {
    QFile f( path );
    REQUIRE( f.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( f.write( bytes ) == bytes.size() );
    f.close();
  }

  QByteArray readFile( const QString &path )
  {
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    return f.readAll();
  }
} // namespace

TEST_CASE( "Export copy refuses dest==src and never deletes the source (#1792)",
           "[classify][export][r27t04]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );

  const QString src = QDir::temp().filePath( QStringLiteral( "r27t04_export_src.tif" ) );
  const QByteArray sourceBytes( 1024, 'x' );
  writeFile( src, sourceBytes );

  // --- dest == src: refused, source intact --------------------------
  armModalCloser();
  CHECK_FALSE( w.copyPathTo( src, src ) );
  QApplication::processEvents( QEventLoop::AllEvents, 200 );
  CHECK( QFileInfo::exists( src ) );                        // RED on HEAD: removed
  CHECK( readFile( src ) == sourceBytes );

  // --- dest == src via a different spelling (./name): refused --------
  const QString srcDotted = QFileInfo( src ).dir().filePath(
    QStringLiteral( "./" ) + QFileInfo( src ).fileName() );
  armModalCloser();
  CHECK_FALSE( w.copyPathTo( src, srcDotted ) );
  QApplication::processEvents( QEventLoop::AllEvents, 200 );
  CHECK( QFileInfo::exists( src ) );
  CHECK( readFile( src ) == sourceBytes );

  // --- healthy copy to a NEW destination -----------------------------
  const QString dest = QDir::temp().filePath( QStringLiteral( "r27t04_export_dest.tif" ) );
  QFile::remove( dest );
  CHECK( w.copyPathTo( src, dest ) );
  CHECK( QFileInfo::exists( dest ) );
  CHECK( readFile( dest ) == sourceBytes );

  // --- overwrite of an EXISTING destination is atomic ---------------
  // (a failed publish must leave the previous destination intact; here a
  // successful publish replaces it wholesale.)
  const QByteArray updated( 2048, 'y' );
  writeFile( src, updated );
  CHECK( w.copyPathTo( src, dest ) );
  CHECK( readFile( dest ) == updated );

  // No staging leftovers for OUR exports in the target directory (scoped
  // to the dest file's own name pattern — the temp dir is shared).
  const QStringList staging = QFileInfo( dest ).dir().entryList(
    QStringList() << QFileInfo( dest ).fileName() + QStringLiteral( ".tmp-*" ) );
  CHECK( staging.isEmpty() );

  QFile::remove( src );
  QFile::remove( dest );
}
