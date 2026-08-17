// test_spectral_library_dialog.cpp — spectral library matching dialog
//
// Drives the dialog headlessly: feeds a synthetic profile spectrum and a
// temp spectral library, then asserts the SAM-ranked results table and the
// save-current-spectrum round trip.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>

#include <qgsapplication.h>

#include "app/dialogs/spectral_library_dialog.h"
#include "processing/algorithms/spectral_library.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_spectral_library_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "SpectralLibraryDialog matches the profile against the library", "[spectral_library_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Library with two entries; the test spectrum is exactly entry "target".
  SpectralLibrary::Library library;
  SpectralLibrary::Entry target;
  target.name = QStringLiteral( "target" );
  target.material = QStringLiteral( "vegetation" );
  target.spectrum = { 0.1f, 0.2f, 0.3f, 0.4f };
  SpectralLibrary::Entry other;
  other.name = QStringLiteral( "other" );
  other.spectrum = { 0.4f, 0.3f, 0.2f, 0.1f };
  library.entries.append( target );
  library.entries.append( other );

  const QString libraryPath = dir.filePath( QStringLiteral( "lib.json" ) );
  QString err;
  REQUIRE( library.save( libraryPath, &err ) );

  SpectralLibraryDialog dialog;
  dialog.setSpectrum( { 0.1, 0.2, 0.3, 0.4 } );

  SECTION( "match rows are ranked by ascending SAM angle" )
  {
    QString loadError;
    REQUIRE( dialog.loadAndMatch( libraryPath, &loadError ) );
    CHECK( dialog.matchRowCount() == 2 );

    auto *table = dialog.findChild<QTableWidget *>( QStringLiteral( "spectralMatchTable" ) );
    REQUIRE( table != nullptr );
    REQUIRE( table->rowCount() == 2 );
    CHECK( table->item( 0, 1 )->text() == QStringLiteral( "target" ) );
    CHECK( table->item( 0, 2 )->text() == QStringLiteral( "vegetation" ) );
    // Identical direction -> SAM angle ~0 degrees.
    CHECK( table->item( 0, 3 )->text().toDouble() == Catch::Approx( 0.0 ).margin( 1e-3 ) );
    CHECK( table->item( 1, 1 )->text() == QStringLiteral( "other" ) );
    CHECK( table->item( 1, 3 )->text().toDouble() > 10.0 );
  }

  SECTION( "a malformed library path reports failure" )
  {
    QString loadError;
    CHECK_FALSE( dialog.loadAndMatch( dir.filePath( QStringLiteral( "missing.json" ) ), &loadError ) );
    CHECK_FALSE( loadError.isEmpty() );
  }

  SECTION( "saving the current spectrum appends an entry and persists" )
  {
    QString loadError;
    REQUIRE( dialog.loadAndMatch( libraryPath, &loadError ) );

    auto *pathEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "spectralLibPathEdit" ) );
    REQUIRE( pathEdit != nullptr );
    auto *saveButton = dialog.findChild<QPushButton *>( QStringLiteral( "spectralSaveBtn" ) );
    REQUIRE( saveButton != nullptr );

    // The dialog saves to the loaded path; the button emits a direct signal.
    saveButton->click();

    SpectralLibrary::Library reloaded;
    REQUIRE( SpectralLibrary::Library::load( libraryPath, &reloaded, &loadError ) );
    REQUIRE( reloaded.entries.size() == 3 );
    CHECK( reloaded.entries.last().name.startsWith( QStringLiteral( "profile_" ) ) );
    REQUIRE( reloaded.entries.last().spectrum.size() == 4 );
    CHECK( reloaded.entries.last().spectrum[3] == Catch::Approx( 0.4f ) );
  }

  SECTION( "switching library path reloads the new library on match (#340)" )
  {
    QString loadError;
    REQUIRE( dialog.loadAndMatch( libraryPath, &loadError ) );
    CHECK( dialog.matchRowCount() == 2 );

    // Create a second library with a different single entry
    SpectralLibrary::Library lib2;
    SpectralLibrary::Entry mineral;
    mineral.name = QStringLiteral( "mineral" );
    mineral.spectrum = { 0.1f, 0.2f, 0.3f, 0.4f };
    lib2.entries.append( mineral );

    const QString lib2Path = dir.filePath( QStringLiteral( "lib2.json" ) );
    REQUIRE( lib2.save( lib2Path, &loadError ) );

    auto *pathEdit = dialog.findChild<QLineEdit *>( QStringLiteral( "spectralLibPathEdit" ) );
    REQUIRE( pathEdit != nullptr );
    auto *matchBtn = dialog.findChild<QPushButton *>( QStringLiteral( "spectralMatchBtn" ) );
    REQUIRE( matchBtn != nullptr );

    // Change path in edit box and click match
    pathEdit->setText( lib2Path );
    matchBtn->click();

    CHECK( dialog.matchRowCount() == 1 );
    auto *table = dialog.findChild<QTableWidget *>( QStringLiteral( "spectralMatchTable" ) );
    REQUIRE( table != nullptr );
    REQUIRE( table->rowCount() == 1 );
    CHECK( table->item( 0, 1 )->text() == QStringLiteral( "mineral" ) );
  }
}
