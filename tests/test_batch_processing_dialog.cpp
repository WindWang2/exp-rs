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

TEST_CASE( "Batch dialog RS parameter form exposes overridable defaults", "[batch_processing_dialog]" )
{
  ensureQgisApplication();
  BatchProcessingDialog dialog;
  dialog.setAlgorithmId( QStringLiteral( "rs:qa_mask" ) );

  // qa_mask exposes source (enum) and mask (enum) overrides.
  auto *sourceCombo = dialog.findChild<QComboBox *>( QStringLiteral( "rsParam_source" ) );
  auto *maskCombo = dialog.findChild<QComboBox *>( QStringLiteral( "rsParam_mask" ) );
  REQUIRE( sourceCombo != nullptr );
  REQUIRE( maskCombo != nullptr );

  // Defaults from the operator schema surface in the form.
  QJsonObject defaults = dialog.collectParamOverrides();
  CHECK( defaults.value( QStringLiteral( "source" ) ).toString() == QStringLiteral( "auto" ) );
  CHECK( defaults.value( QStringLiteral( "mask" ) ).toString()
         == QStringLiteral( "cloud_and_shadow" ) );

  // Editing the form is reflected in the collected overrides.
  maskCombo->setCurrentIndex( maskCombo->findData( QStringLiteral( "cloud" ) ) );
  sourceCombo->setCurrentIndex(
    sourceCombo->findData( QStringLiteral( "generic_bitmask" ) ) );
  const QJsonObject edited = dialog.collectParamOverrides();
  CHECK( edited.value( QStringLiteral( "source" ) ).toString()
         == QStringLiteral( "generic_bitmask" ) );
  CHECK( edited.value( QStringLiteral( "mask" ) ).toString() == QStringLiteral( "cloud" ) );
}

TEST_CASE( "Batch run applies parameter overrides but keeps input/output fixed", "[batch_processing_dialog]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  REQUIRE( dir.isValid() );

  // 4-band raster with a QA band (band role scene_classification on band 1).
  const QString input = dir.filePath( QStringLiteral( "qa_product.tif" ) );
  const QString output = dir.filePath( QStringLiteral( "masked.tif" ) );
  std::vector<std::vector<float>> bands( 4, std::vector<float>( 4, 100.0f ) );
  std::array<double, 6> gt = { 0, 1, 0, 0, 0, -1 };
  QString err;
  REQUIRE( writeGdalOutput( input, 2, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

  BatchProcessingDialog dialog;

  // Overrides reach the operator (generic_bitmask + bit flag 1 runs and
  // produces the mask); main input / output stay fixed by the batch item.
  QJsonObject overrides;
  overrides[QStringLiteral( "source" )] = QStringLiteral( "generic_bitmask" );
  overrides[QStringLiteral( "bits" )] = 1;
  overrides[QStringLiteral( "qa_band" )] = 1; // band 1 acts as the QA band
  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:qa_mask" ), input, output, &err, overrides ) );
  REQUIRE( QFile::exists( output ) );

  // An override that tries to hijack the main input is ignored.
  QJsonObject hijack;
  hijack[QStringLiteral( "input" )] = QStringLiteral( "/nonexistent.tif" );
  hijack[QStringLiteral( "source" )] = QStringLiteral( "generic_bitmask" );
  hijack[QStringLiteral( "bits" )] = 1;
  hijack[QStringLiteral( "qa_band" )] = 1;
  const QString out2 = dir.filePath( QStringLiteral( "masked2.tif" ) );
  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:qa_mask" ), input, out2, &err, hijack ) );
  REQUIRE( QFile::exists( out2 ) );
}
