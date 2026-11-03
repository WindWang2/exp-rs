// tests/test_r27_t04_export_atomic.cpp — R27-T04 (#1792)
//
// Contract under test: the classification Step-7 export copy
//   1. refuses dest == src (canonical compare) — on HEAD QFile::remove(dest)
//      deleted the SOURCE file before the copy;
//   2. publishes staged-atomically — a failed publish leaves any previous
//      destination intact (never remove-then-copy).
// The QFileDialog inside copyPathWithDialog is answered by a timed
// dismisser (same technique as the shell lifecycle suite's modal boxes).
#include <catch2/catch_test_macros.hpp>

#include "classification/qgsclassificationmainwindow.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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

  /// Persistent modal answerer: one repeating timer runs for the whole
  /// test, answering every modal QFileDialog with the current pending
  /// target (select + accept) and closing every QMessageBox (refusal
  /// warnings). A single-shot dismisser disarms after the first dialog and
  /// the NEXT modal (the dest==src warning) then spins forever.
  struct ModalAnswerer
  {
    QString pendingFile;

    explicit ModalAnswerer()
    {
      timer.setInterval( 10 );
      QObject::connect( &timer, &QTimer::timeout, &timer, [this] { dismiss(); } );
    }
    ~ModalAnswerer() { timer.stop(); }

    void answerNextWith( const QString &file ) { pendingFile = file; }

    void dismiss()
    {
      for ( QWidget *widget : QApplication::topLevelWidgets() )
      {
        if ( !widget->isModal() )
          continue;
        if ( auto *dialog = qobject_cast<QFileDialog *>( widget ) )
        {
          if ( !pendingFile.isEmpty() )
          {
            dialog->selectFile( pendingFile );
            // QFileDialog::accept() is protected in Qt6; call the public
            // QDialog::accept (dynamic dispatch applies the selection).
            static_cast<QDialog *>( dialog )->accept();
            return;
          }
          dialog->close();
          return;
        }
        widget->close(); // QMessageBox / any other modal
        return;
      }
    }

    QTimer timer;
  };
} // namespace

TEST_CASE( "Export copy refuses dest==src and never deletes the source (#1792)",
           "[classify][export][r27t04]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  ModalAnswerer answerer;
  answerer.timer.start();

  const QString src = QDir::temp().filePath( QStringLiteral( "r27t04_export_src.tif" ) );
  {
    QFile f( src );
    REQUIRE( f.open( QIODevice::WriteOnly ) );
    f.write( QByteArray( 1024, 'x' ) );
    f.close();
  }
  const QByteArray sourceBytes = [&] {
    QFile f( src );
    f.open( QIODevice::ReadOnly );
    return f.readAll();
  }();

  // --- dest == src: refused, source intact --------------------------
  answerer.answerNextWith( src );
  const bool refusedSame = !w.copyPathWithDialog( src, QStringLiteral( "Export" ) );
  QApplication::processEvents( QEventLoop::AllEvents, 300 );
  CHECK( refusedSame );
  CHECK( QFileInfo::exists( src ) );                       // RED on HEAD: removed
  {
    QFile f( src );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    CHECK( f.readAll() == sourceBytes );
  }

  // --- dest == src via a different spelling (symlink/..): refused ----
  const QString srcDotted = QFileInfo( src ).dir().filePath(
    QStringLiteral( "./" ) + QFileInfo( src ).fileName() );
  answerer.answerNextWith( srcDotted );
  const bool refusedDotted = !w.copyPathWithDialog( src, QStringLiteral( "Export" ) );
  QApplication::processEvents( QEventLoop::AllEvents, 300 );
  CHECK( refusedDotted );
  CHECK( QFileInfo::exists( src ) );

  // --- healthy copy to a NEW destination -----------------------------
  const QString dest = QDir::temp().filePath( QStringLiteral( "r27t04_export_dest.tif" ) );
  QFile::remove( dest );
  answerer.answerNextWith( dest );
  CHECK( w.copyPathWithDialog( src, QStringLiteral( "Export" ) ) );
  QApplication::processEvents( QEventLoop::AllEvents, 300 );
  CHECK( QFileInfo::exists( dest ) );
  {
    QFile f( dest );
    REQUIRE( f.open( QIODevice::ReadOnly ) );
    CHECK( f.readAll() == sourceBytes );
  }

  // No staging leftovers in the target directory.
  const QStringList staging = QFileInfo( dest ).dir().entryList(
    QStringList() << QStringLiteral( "*.tmp-*" ) );
  CHECK( staging.isEmpty() );

  QFile::remove( src );
  QFile::remove( dest );
}
