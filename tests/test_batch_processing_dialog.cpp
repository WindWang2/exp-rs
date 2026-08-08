// test_batch_processing_dialog.cpp — batch dialog RS-operator support
//
// The batch dialog runs a single algorithm over many files. Since version 52
// it also lists single-input RS operators (declared defaults only) and
// executes them through the AtomicAlgorithmRegistry adapter. This test pins:
//   - which operators are offered (batchable ones only),
//   - that a real batch item actually produces the output file,
//   - that non-batchable operators fail cleanly.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>

#include <array>
#include <string>
#include <vector>

#include <qgsapplication.h>

#include "app/dialogs/batch_processing_dialog.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_batch_processing_dialog";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

QStringList comboIds( const QComboBox *combo )
{
  QStringList ids;
  for ( int i = 0; i < combo->count(); ++i )
    ids << combo->itemData( i ).toString();
  return ids;
}

} // namespace

TEST_CASE( "Batch dialog lists only batchable RS operators", "[batch_processing_dialog]" )
{
  ensureQgisApplication();
  BatchProcessingDialog dialog;

  auto *combo = dialog.findChild<QComboBox *>( QStringLiteral( "batchAlgorithmCombo" ) );
  REQUIRE( combo != nullptr );
  REQUIRE( combo->count() > 0 );

  const QStringList ids = comboIds( combo );

  // Single-input operators whose remaining parameters have defaults.
  CHECK( ids.contains( QStringLiteral( "rs:rx_anomaly" ) ) );
  CHECK( ids.contains( QStringLiteral( "rs:qa_mask" ) ) );

  // Multi-input or required-parameter operators must not be offered.
  CHECK_FALSE( ids.contains( QStringLiteral( "rs:change_detection" ) ) );
  CHECK_FALSE( ids.contains( QStringLiteral( "rs:apply_mask" ) ) );
  CHECK_FALSE( ids.contains( QStringLiteral( "rs:spectral_resample" ) ) );
  CHECK_FALSE( ids.contains( QStringLiteral( "rs:spectral_unmixing" ) ) );
}

TEST_CASE( "Batch run executes an RS operator per file", "[batch_processing_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // Two small 4-band rasters with varying pixel values (RX needs a
  // non-singular covariance, so constant data would fail).
  QStringList inputs;
  QStringList outputs;
  for ( int i = 0; i < 2; ++i )
  {
    const QString in = dir.filePath( QStringLiteral( "scene%1.tif" ).arg( i ) );
    const QString out = dir.filePath( QStringLiteral( "scene%1_processed.tif" ).arg( i ) );
    std::vector<std::vector<float>> bands( 4, std::vector<float>( 4 ) );
    float v = 1.0f;
    for ( auto &band : bands )
      for ( float &pixel : band )
        pixel = v++;
    std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
    QString err;
    REQUIRE( writeGdalOutput( in, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
    inputs << in;
    outputs << out;
  }

  BatchProcessingDialog dialog;
  QString err;

  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:rx_anomaly" ), inputs[0], outputs[0], &err ) );
  REQUIRE( QFile::exists( outputs[0] ) );

  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:rx_anomaly" ), inputs[1], outputs[1], &err ) );
  REQUIRE( QFile::exists( outputs[1] ) );

  // Unknown id and multi-input operators fail cleanly with a message.
  err.clear();
  CHECK_FALSE( dialog.runBatchItem( QStringLiteral( "rs:does_not_exist" ),
                                    inputs[0], outputs[0], &err ) );
  CHECK_FALSE( err.isEmpty() );

  err.clear();
  CHECK_FALSE( dialog.runBatchItem( QStringLiteral( "rs:change_detection" ),
                                    inputs[0], outputs[0], &err ) );
  CHECK_FALSE( err.isEmpty() );
}
