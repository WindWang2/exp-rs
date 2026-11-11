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
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>

#include <array>
#include <string>
#include <vector>

#include <qgsapplication.h>
#include <qgsprocessingregistry.h>

#include "app/dialogs/batch_processing_dialog.h"
#include "operators/framework/rs_operator_registry.h"
#include "processing/providers/gdal_tools/provider.h"
#include "processing/providers/qgis_algorithms/provider.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

namespace
{

void ensureQgisApplication()
{
  // libsicnu_operators registers rs:* operators into AtomicAlgorithmRegistry
  // via static initializers. Ubuntu's default --as-needed link drops a shared
  // library the binary references no symbol from, so force a reference to keep
  // it in DT_NEEDED (same pattern as test_atomic_algorithm_registry); without
  // it the batch dialog's RS operator combo is empty and rs:* batch items fail
  // with "RS operator not found".
  ( void ) sicnu::operators::RSOperatorRegistry::instance().operatorNames();

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

void settleLayout()
{
  for ( int i = 0; i < 5; ++i )
  {
    // Algorithm switches retire the previous editors with deleteLater().
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    QCoreApplication::processEvents();
  }
}

void useLargeProductFont( QWidget &widget )
{
  // Match main.cpp: offscreen environments may have no system fonts, so an
  // unregistered family otherwise measures every glyph as a full-width box.
  static const int fontId = QFontDatabase::addApplicationFont(
      QDir( QFileInfo( QString::fromUtf8( __FILE__ ) ).absolutePath() )
          .filePath( QStringLiteral( "../resources/fonts/IBMPlexSans.ttf" ) ) );
  REQUIRE( fontId >= 0 );
  REQUIRE( QFontDatabase::applicationFontFamilies( fontId )
               .contains( QStringLiteral( "IBM Plex Sans" ) ) );
  widget.setStyleSheet( QStringLiteral(
      "QWidget { font-family: 'IBM Plex Sans'; font-size: 26px; }" ) );
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
  QVariantMap defaults = dialog.collectParamOverrides();
  CHECK( defaults.value( QStringLiteral( "source" ) ).toString() == QStringLiteral( "auto" ) );
  CHECK( defaults.value( QStringLiteral( "mask" ) ).toString()
         == QStringLiteral( "cloud_and_shadow" ) );

  // Editing the form is reflected in the collected overrides.
  maskCombo->setCurrentIndex( maskCombo->findData( QStringLiteral( "cloud" ) ) );
  sourceCombo->setCurrentIndex(
    sourceCombo->findData( QStringLiteral( "generic_bitmask" ) ) );
  const QVariantMap edited = dialog.collectParamOverrides();
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
  QVariantMap overrides;
  overrides[QStringLiteral( "source" )] = QStringLiteral( "generic_bitmask" );
  overrides[QStringLiteral( "bits" )] = 1;
  overrides[QStringLiteral( "qa_band" )] = 1; // band 1 acts as the QA band
  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:qa_mask" ), input, output, &err, overrides ) );
  REQUIRE( QFile::exists( output ) );

  // An override that tries to hijack the main input is ignored.
  QVariantMap hijack;
  hijack[QStringLiteral( "input" )] = QStringLiteral( "/nonexistent.tif" );
  hijack[QStringLiteral( "source" )] = QStringLiteral( "generic_bitmask" );
  hijack[QStringLiteral( "bits" )] = 1;
  hijack[QStringLiteral( "qa_band" )] = 1;
  const QString out2 = dir.filePath( QStringLiteral( "masked2.tif" ) );
  REQUIRE( dialog.runBatchItem( QStringLiteral( "rs:qa_mask" ), input, out2, &err, hijack ) );
  REQUIRE( QFile::exists( out2 ) );
}

TEST_CASE( "Batch dialog exposes QGIS algorithm parameters via wrappers", "[batch_processing_dialog]" )
{
  ensureQgisApplication();
  if ( !QgsApplication::processingRegistry()->providerById( QStringLiteral( "qgis_algorithms" ) ) )
    QgsApplication::processingRegistry()->addProvider( new QgisAlgorithmsProvider() );

  BatchProcessingDialog dialog;
  // reprojectlayer has a TARGET_CRS parameter beyond INPUT/OUTPUT.
  dialog.setAlgorithmId( QStringLiteral( "qgis_algorithms:reprojectlayer" ) );

  auto *crsWidget = dialog.findChild<QWidget *>( QStringLiteral( "qgisParam_TARGET_CRS" ) );
  REQUIRE( crsWidget != nullptr );

  // The wrapper collects its current value into the override map.
  const QVariantMap overrides = dialog.collectParamOverrides();
  CHECK( overrides.contains( QStringLiteral( "TARGET_CRS" ) ) );

  // INPUT/OUTPUT are not offered (decided by the batch item).
  CHECK_FALSE( overrides.contains( QStringLiteral( "INPUT" ) ) );
  CHECK_FALSE( overrides.contains( QStringLiteral( "OUTPUT" ) ) );
}

TEST_CASE( "Batch algorithm changes keep large Boolean captions usable in narrow windows",
           "[batch_processing_dialog][layout]" )
{
  ensureQgisApplication();
  if ( !QgsApplication::processingRegistry()->providerById( QStringLiteral( "gdal_tools" ) ) )
    QgsApplication::processingRegistry()->addProvider( new GdalToolsProvider() );

  BatchProcessingDialog dialog;
  dialog.setAlgorithmId( QStringLiteral( "rs:qa_mask" ) );
  useLargeProductFont( dialog );
  dialog.show();
  dialog.resize( 400, 600 );
  settleLayout();
  auto *scroll = dialog.findChild<QScrollArea *>( QStringLiteral( "rsToolFormScroll" ) );
  REQUIRE( scroll );
  auto *algorithm = dialog.findChild<QComboBox *>( QStringLiteral( "batchAlgorithmCombo" ) );
  REQUIRE( algorithm );
  REQUIRE( algorithm->currentData().toString() == QStringLiteral( "rs:qa_mask" ) );
  // The static selector also has to fit: its original 14-character minimum
  // exceeded this viewport at 26px before any dynamic parameters were added.
  REQUIRE( scroll->horizontalScrollBar()->maximum() == 0 );
  CHECK( algorithm->minimumSizeHint().width() <= scroll->viewport()->width() );

  // These real provider forms previously grew 34–70 px beyond the viewport.
  for ( const QString &id : { QStringLiteral( "gdal_tools:gdal2xyz" ),
                              QStringLiteral( "gdal_tools:gdal_edit" ),
                              QStringLiteral( "gdal_tools:ogr2ogr" ) } )
  {
    INFO( id.toStdString() );
    dialog.setAlgorithmId( id );
    settleLayout();
    REQUIRE( dialog.width() == 400 );
    REQUIRE( scroll->horizontalScrollBar()->maximum() == 0 );

    int booleanCount = 0;
    for ( auto *check : scroll->widget()->findChildren<QCheckBox *>() )
    {
      if ( !check->objectName().startsWith( QStringLiteral( "qgisParam_" ) ) )
        continue;
      ++booleanCount;
      REQUIRE( check->isVisibleTo( scroll->widget() ) );
      REQUIRE( check->font().pixelSize() == 26 );
      REQUIRE( check->text().isEmpty() );
      const QString caption = check->accessibleName();
      REQUIRE_FALSE( caption.isEmpty() );

      QLabel *captionLabel = nullptr;
      int captionCount = 0;
      for ( auto *label : scroll->widget()->findChildren<QLabel *>() )
      {
        if ( label->text() == caption )
        {
          captionLabel = label;
          ++captionCount;
        }
      }
      REQUIRE( captionCount == 1 );
      REQUIRE( captionLabel->buddy() == check );
      CHECK( captionLabel->wordWrap() );
      CHECK( captionLabel->heightForWidth( captionLabel->width() ) <= captionLabel->height() + 1 );

      const QString parameter = check->objectName().mid( QStringLiteral( "qgisParam_" ).size() );
      const bool initialValue = dialog.collectParamOverrides().value( parameter ).toBool();
      scroll->ensureWidgetVisible( captionLabel );
      QTest::mouseClick( captionLabel, Qt::LeftButton );
      CHECK( dialog.collectParamOverrides().value( parameter ).toBool() == !initialValue );
      check->setFocus();
      QTest::keyClick( check, Qt::Key_Space );
      CHECK( dialog.collectParamOverrides().value( parameter ).toBool() == initialValue );
    }
    REQUIRE( booleanCount > 0 );
  }
}

TEST_CASE( "Batch RS enum rebuilds tolerate long option captions without changing their values",
           "[batch_processing_dialog][layout]" )
{
  ensureQgisApplication();
  BatchProcessingDialog dialog;
  dialog.setAlgorithmId( QStringLiteral( "rs:qa_mask" ) );
  useLargeProductFont( dialog );
  dialog.show();
  dialog.resize( 400, 600 );
  settleLayout();
  QPointer<QComboBox> previous = dialog.findChild<QComboBox *>( QStringLiteral( "rsParam_source" ) );
  REQUIRE( previous );

  dialog.setAlgorithmId( QStringLiteral( "rs:contrast_stretch" ) );
  settleLayout();
  REQUIRE( previous.isNull() );
  dialog.setAlgorithmId( QStringLiteral( "rs:qa_mask" ) );
  auto *source = dialog.findChild<QComboBox *>( QStringLiteral( "rsParam_source" ) );
  REQUIRE( source );
  // Model a verbose/localized choice on the newly generated, real RS editor.
  source->setItemText( 0, QStringLiteral(
    "Automatically determine the cloud mask source from the input product metadata" ) );
  settleLayout();

  auto *scroll = dialog.findChild<QScrollArea *>( QStringLiteral( "rsToolFormScroll" ) );
  REQUIRE( scroll );
  REQUIRE( source->font().pixelSize() == 26 );
  CHECK( dialog.width() == 400 );
  CHECK( scroll->horizontalScrollBar()->maximum() == 0 );
  CHECK( dialog.collectParamOverrides().value( QStringLiteral( "source" ) ).toString()
         == QStringLiteral( "auto" ) );
  REQUIRE( source->count() > 1 );
  const QString nextValue = source->itemData( 1 ).toString();
  scroll->ensureWidgetVisible( source );
  source->setFocus();
  QTest::keyClick( source, Qt::Key_Down );
  CHECK( dialog.collectParamOverrides().value( QStringLiteral( "source" ) ).toString() == nextValue );
}
