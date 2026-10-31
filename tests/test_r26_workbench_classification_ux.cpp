// test_r26_workbench_classification_ux.cpp — R26-02 workbench: classification
// studio interaction honesty.
//   WP1: class table color edit must not read a rebuilt (freed) table item.
//   WP2: merge classes removes source class defs and relabels samples.
//   WP3: image switch with samples asks; close with 0 ROIs cannot deadlock.
//   WP4: precise band errors reach the status bar; invalid classifier kinds
//        fall back instead of leaving no backend.
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <gdal_priv.h>

#include "processing/framework/task_center.h"
#include "qgsclassificationmainwindow.h"
#include "rs_class_table_widget.h"
#include "rs_classifier_setup_bar.h"
#include "rs_merge_classes_dialog.h"
#include "rs_roi_collection.h"

#include <gdal.h>
#include <qgsvectorlayer.h>
#include <qgsfeature.h>
#include <qgsfeaturerequest.h>
#include <qgsgeometry.h>
#include <qgspointxy.h>

#include <memory>
#include <stdexcept>
#include <vector>
#include "support/qt_lifecycle.h"

CATCH_REGISTER_LISTENER( sicnu::test::qtlifecycle::TeardownListener )

namespace
{

int fake_argc = 1;
char fake_argv0[] = "test";
char *fake_argv[] = { fake_argv0, nullptr };

QApplication *ensureApp()
{
  if ( !QCoreApplication::instance() )
  {
    return sicnu::test::qtlifecycle::heapQApplication( fake_argc, fake_argv );
  }
  return static_cast<QApplication *>( QCoreApplication::instance() );
}

sicnu::AlgorithmTaskInfo waitForTerminalTask( long taskId )
{
  sicnu::AlgorithmTaskInfo info;
  for ( int attempt = 0; attempt < 1000; ++attempt )
  {
    // Pump events while polling: the TaskCenter status flips on a worker
    // thread, while the window's success callback (which records the output
    // path this test depends on) is delivered on the GUI thread.
    QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
    info = sicnu::TaskCenter::instance().getTaskInfo( taskId );
    if ( info.status == sicnu::TaskStatus::Completed
         || info.status == sicnu::TaskStatus::Failed
         || info.status == sicnu::TaskStatus::Canceled )
      return info;
    QThread::msleep( 5 );
  }
  return info;
}

/// 32x32 Int32 class-ID raster the post-process task accepts.
QString makeLabelRaster( const QString &path )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return QStringLiteral( "GTiff driver unavailable" );
  GDALDataset *dataset = driver->Create( path.toUtf8().constData(),
                                         32, 32, 1, GDT_Int32, nullptr );
  if ( !dataset )
    return QStringLiteral( "could not create raster" );
  std::vector<int> labels( 32 * 32, 1 );
  if ( dataset->GetRasterBand( 1 )->RasterIO(
         GF_Write, 0, 0, 32, 32, labels.data(), 32, 32, GDT_Int32, 0, 0 )
       != CE_None )
  {
    GDALClose( dataset );
    return QStringLiteral( "could not write raster" );
  }
  GDALClose( dataset );
  return {};
}

/// Real post-process task config (labels 1 everywhere, no op enabled — the
/// task center copy pass is enough to set m_lastPostRasterPath on success).
RsPostProcessConfig mergeablePostProcessConfig( const QTemporaryDir &tmp )
{
  RsPostProcessConfig config;
  config.inputPath = tmp.path() + QStringLiteral( "/labels.tif" );
  config.outputRasterPath = tmp.path() + QStringLiteral( "/post.tif" );
  config.runSieve = false;
  config.runMajority = false;
  config.runClump = false;
  config.runRecode = false;
  config.runPolygonize = false;
  config.creationOptions.clear();
  return config;
}

QgsVectorLayer *sampleLayerOf( QgsClassificationMainWindow &w )
{
  return w.findChild<QgsVectorLayer *>( QStringLiteral( "rsClassifySampleLayer" ) );
}

/// Append one square sample polygon tagged with cls_id (size × size pixels).
void addSample( QgsVectorLayer *layer, int classId, double x0, double y0,
                double size = 5.0 )
{
  if ( !layer )
    return;
  if ( !layer->isEditable() )
    layer->startEditing();
  QgsFeature f( layer->fields() );
  QgsPolygonXY poly;
  QgsPolylineXY ring = { QgsPointXY( x0, y0 ), QgsPointXY( x0 + size, y0 ),
                         QgsPointXY( x0 + size, y0 + size ),
                         QgsPointXY( x0, y0 + size ), QgsPointXY( x0, y0 ) };
  poly << ring;
  f.setGeometry( QgsGeometry::fromPolygonXY( poly ) );
  f.setAttribute( QStringLiteral( "cls_id" ), classId );
  layer->addFeature( f );
  layer->commitChanges();
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
}

/// 32x32, 3-band Float32 unit-geotransform raster for classification.
QString makeThreeBandRaster( const QString &path )
{
  GDALAllRegister();
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return QStringLiteral( "GTiff driver unavailable" );
  GDALDataset *dataset = driver->Create( path.toUtf8().constData(),
                                         32, 32, 3, GDT_Float32, nullptr );
  if ( !dataset )
    return QStringLiteral( "could not create raster" );
  double gt[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
  dataset->SetGeoTransform( gt );
  std::vector<float> data( 32 * 32, 0.0f );
  for ( int b = 1; b <= 3; ++b )
  {
    for ( int i = 0; i < 32 * 32; ++i )
      data[i] = 10.0f * b + ( i % 32 ) * 0.5f;
    if ( dataset->GetRasterBand( b )->RasterIO(
           GF_Write, 0, 0, 32, 32, data.data(), 32, 32, GDT_Float32, 0, 0 )
         != CE_None )
    {
      GDALClose( dataset );
      return QStringLiteral( "could not write band %1" ).arg( b );
    }
  }
  GDALClose( dataset );
  return {};
}

}  // namespace

TEST_CASE( "ClassificationStudio: class table color edit keeps the class name stable (UAF)",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  auto *table = w.findChild<RsClassTableWidget *>();
  REQUIRE( table != nullptr );
  const int rowsBefore = table->findChild<QTableWidget *>() != nullptr
                           ? table->findChild<QTableWidget *>()->rowCount()
                           : 0;
  REQUIRE( rowsBefore >= 1 );

  const QString nameBefore =
    table->findChild<QTableWidget *>()->item( 0, 1 )->text();

  QSignalSpy editedSpy( table, &RsClassTableWidget::classDefEdited );

  // Intercept QColorDialog::getColor's modal loop: pick a color, accept.
  QTimer pickColor;
  pickColor.setInterval( 0 );
  const QColor chosen( 12, 34, 56 );
  QObject::connect( &pickColor, &QTimer::timeout, [&chosen] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *dialog = qobject_cast<QColorDialog *>( widget ) )
      {
        dialog->setCurrentColor( chosen );
        dialog->accept();
      }
    }
  } );
  pickColor.start();

  // Row 0, column 0 = the color cell. Must not crash and must emit the
  // ORIGINAL class name — before the fix this read a QTableWidgetItem that
  // rebuild() had already destroyed (heap-use-after-free).
  QMetaObject::invokeMethod( table, "onCellDoubleClicked", Q_ARG( int, 0 ),
                             Q_ARG( int, 0 ) );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  pickColor.stop();

  REQUIRE( editedSpy.size() == 1 );
  const QString emittedName = editedSpy.at( 0 ).at( 1 ).toString();
  CHECK( emittedName == nameBefore );
}

TEST_CASE( "ClassificationStudio: class table color edit copies the name before the model update (source contract)",
           "[classify][ux][r26-workbench]" )
{
  // The behavioral UAF above can read intact-but-freed bytes on some
  // allocators; this source-scan pins the structural guarantee the fix
  // introduces (same pattern as test_classification_window's method scans):
  // onCellDoubleClicked must snapshot the name BEFORE setClassDef triggers
  // the rebuild that deletes the item, and must not touch nameItem after it.
  QFile source( QStringLiteral( SICNU_SOURCE_DIR "/src/app/classification/rs_class_table_widget.cpp" ) );
  REQUIRE( source.open( QIODevice::ReadOnly | QIODevice::Text ) );
  const QString text = QString::fromUtf8( source.readAll() );

  const int begin = text.indexOf( QStringLiteral( "void RsClassTableWidget::onCellDoubleClicked" ) );
  REQUIRE( begin >= 0 );
  const int end = text.indexOf( QStringLiteral( "void RsClassTableWidget::onItemChanged" ), begin );
  REQUIRE( end >= 0 );
  const QString body = text.mid( begin, end - begin );

  const int copyPos = body.indexOf( QStringLiteral( "nameBefore" ) );
  const int modelUpdatePos = body.indexOf( QStringLiteral( "setClassDef" ) );
  REQUIRE( copyPos >= 0 );  // RED pre-fix: no snapshot existed
  REQUIRE( modelUpdatePos >= 0 );
  CHECK( copyPos < modelUpdatePos );
  // The post-model-update emit must use the snapshot, not the freed item.
  const int emitPos = body.lastIndexOf( QStringLiteral( "emit classDefEdited" ) );
  REQUIRE( emitPos > modelUpdatePos );
  const QString emitLine = body.mid( emitPos, body.indexOf( QLatin1Char( '\n' ), emitPos ) - emitPos );
  CHECK( emitLine.contains( QStringLiteral( "nameBefore" ) ) );
  CHECK_FALSE( emitLine.contains( QStringLiteral( "nameItem" ) ) );
}

TEST_CASE( "ClassificationStudio: merging classes closes the loop on defs and samples",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  REQUIRE( makeLabelRaster( tmp.path() + QStringLiteral( "/labels.tif" ) ).isEmpty() );

  QgsClassificationMainWindow w( nullptr );
  auto *table = w.findChild<RsClassTableWidget *>();
  auto *tableWidget = table ? table->findChild<QTableWidget *>() : nullptr;
  REQUIRE( tableWidget != nullptr );
  const int rowsBefore = tableWidget->rowCount();
  REQUIRE( rowsBefore >= 2 );
  // Rows are sorted by id: the merge target is always the SMALLEST id.
  const int targetId = tableWidget->item( 0, 0 )->data( Qt::UserRole ).toInt();
  const int absorbedId = tableWidget->item( 1, 0 )->data( Qt::UserRole ).toInt();
  REQUIRE( targetId != absorbedId );

  auto *sampleLayer = sampleLayerOf( w );
  REQUIRE( sampleLayer != nullptr );
  addSample( sampleLayer, absorbedId, 0.0, 0.0 );
  REQUIRE( sampleLayer->featureCount() == 1 );

  // A finished post-process run gives runMergeClasses its input raster.
  RsPostProcessConfig config = mergeablePostProcessConfig( tmp );
  const long taskId = w.startPostProcessTask(
    config, /*loadToLayers=*/false, QStringLiteral( "merge setup" ),
    QStringLiteral( "module:classify:postprocess" ) );
  REQUIRE( taskId > 0 );
  REQUIRE( waitForTerminalTask( taskId ).status == sicnu::TaskStatus::Completed );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );

  // Trigger the private slot (opens RsMergeClassesDialog) and accept it —
  // the dialog always targets the smallest source id (targetId).
  QTimer acceptDialog;
  acceptDialog.setInterval( 0 );
  QObject::connect( &acceptDialog, &QTimer::timeout, [] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *dialog = qobject_cast<RsMergeClassesDialog *>( widget ) )
        dialog->accept();
    }
  } );
  acceptDialog.start();
  const QList<int> mergeSources{ targetId, absorbedId };
  const bool invoked = QMetaObject::invokeMethod(
    &w, "onMergeClassesRequested",
    Q_ARG( QList<int>, mergeSources ),
    Q_ARG( int, targetId ),
    Q_ARG( QString, QStringLiteral( "Merged" ) ),
    Q_ARG( QColor, QColor( 10, 20, 30 ) ) );
  REQUIRE( invoked );
  QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );
  acceptDialog.stop();
  QCoreApplication::processEvents( QEventLoop::AllEvents, 200 );

  // Absorbed class definition is gone from the (rebuilt) table.
  bool absorbedStillListed = false;
  const int rowsAfter = tableWidget->rowCount();
  for ( int row = 0; row < rowsAfter; ++row )
  {
    if ( tableWidget->item( row, 0 )->data( Qt::UserRole ).toInt() == absorbedId )
      absorbedStillListed = true;
  }
  CHECK_FALSE( absorbedStillListed );
  CHECK( rowsAfter == rowsBefore - 1 );

  // The sample feature was relabeled to the target class.
  int relabeled = 0;
  {
    QgsFeatureIterator it = sampleLayer->getFeatures();
    QgsFeature f;
    while ( it.nextFeature( f ) )
    {
      if ( f.attribute( QStringLiteral( "cls_id" ) ).toInt() == targetId )
        ++relabeled;
    }
  }
  CHECK( relabeled == 1 );
}

TEST_CASE( "ClassificationStudio: switching images with samples asks first",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  auto *sampleLayer = sampleLayerOf( w );
  REQUIRE( sampleLayer != nullptr );
  addSample( sampleLayer, 1, 0.0, 0.0 );
  REQUIRE( sampleLayer->featureCount() == 1 );

  // Answer "No" to the confirmation: the samples must survive.
  QTimer answerNo;
  answerNo.setInterval( 0 );
  int questionsSeen = 0;
  QObject::connect( &answerNo, &QTimer::timeout, [&questionsSeen] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *box = qobject_cast<QMessageBox *>( widget ) )
      {
        ++questionsSeen;
        box->button( QMessageBox::No )->click();
      }
    }
  } );
  answerNo.start();
  // A hard watchdog keeps a pre-fix hang (no confirmation → modal file
  // dialog waits forever) from stalling the suite: the test itself must
  // finish and report its RED failure.
  QTimer killSwitch;
  killSwitch.setSingleShot( true );
  killSwitch.start( 20000 );
  QObject::connect( &killSwitch, &QTimer::timeout, [] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *dialog = qobject_cast<QDialog *>( widget ) )
        dialog->reject();
    }
  } );

  w.openSourceRaster();  // declines inside the modal loop, before any dialog
  QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
  answerNo.stop();
  killSwitch.stop();

  CHECK( questionsSeen == 1 );  // RED pre-fix: 0 questions, samples lost
  CHECK( sampleLayer->featureCount() == 1 );
}

TEST_CASE( "ClassificationStudio: dirty close with zero ROIs offers discard, not a failing save",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  QgsClassificationMainWindow w( nullptr );
  w.show();
  REQUIRE( w.isVisible() );

  // Make the session dirty without leaving any ROI: add then delete a sample.
  auto *sampleLayer = sampleLayerOf( w );
  REQUIRE( sampleLayer != nullptr );
  addSample( sampleLayer, 1, 0.0, 0.0 );
  REQUIRE( sampleLayer->featureCount() == 1 );
  if ( !sampleLayer->isEditable() )
    sampleLayer->startEditing();
  QgsFeatureIds allIds;
  QgsFeatureIterator it = sampleLayer->getFeatures( QgsFeatureRequest().setNoAttributes() );
  QgsFeature f;
  while ( it.nextFeature( f ) )
    allIds.insert( f.id() );
  sampleLayer->deleteFeatures( allIds );
  sampleLayer->commitChanges();
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  REQUIRE( sampleLayer->featureCount() == 0 );
  REQUIRE( w.isSessionDirty() );

  // Prefer "Save": pre-fix, Save with zero ROIs always fails ("No ROIs to
  // export") and wedges the window open — exactly the deadlock under test.
  // Post-fix there is no Save button at all (discard-only question), so the
  // interceptor falls through to Discard and the window closes.
  QTimer clickSaveThenDiscard;
  clickSaveThenDiscard.setInterval( 0 );
  QObject::connect( &clickSaveThenDiscard, &QTimer::timeout, [] {
    for ( QWidget *widget : QApplication::topLevelWidgets() )
    {
      if ( auto *box = qobject_cast<QMessageBox *>( widget ) )
      {
        if ( auto *btn = box->button( QMessageBox::Save ) )
          btn->click();
        else if ( auto *btn2 = box->button( QMessageBox::Discard ) )
          btn2->click();
        else
          box->accept();
      }
    }
  } );
  clickSaveThenDiscard.start();
  w.close();
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );
  clickSaveThenDiscard.stop();

  CHECK_FALSE( w.isVisible() );
}

TEST_CASE( "ClassificationStudio: out-of-range band surfaces the real extraction error",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  QTemporaryDir tmp;
  REQUIRE( tmp.isValid() );
  const QString rasterPath = tmp.filePath( QStringLiteral( "threeband.tif" ) );
  REQUIRE( makeThreeBandRaster( rasterPath ).isEmpty() );

  QgsClassificationMainWindow w( nullptr );
  REQUIRE( w.openSourceRaster( rasterPath ) );

  auto *sampleLayer = sampleLayerOf( w );
  REQUIRE( sampleLayer != nullptr );
  addSample( sampleLayer, 1, 0.0, 0.0 );  // 25 px ≥ the 10-px minimum
  addSample( sampleLayer, 2, 10.0, 10.0 );

  auto *bandsEdit = w.findChild<QLineEdit *>( QStringLiteral( "rsClassifierBands" ) );
  REQUIRE( bandsEdit != nullptr );
  bandsEdit->setText( QStringLiteral( "9" ) );  // raster only has 1..3

  w.applyPreview();
  QCoreApplication::processEvents( QEventLoop::AllEvents, 100 );

  // The real cause is the band number, and the status bar must say so —
  // not the misleading "draw more ROIs" hint from the swallowed error.
  const QString message = w.statusBar()->currentMessage();
  CHECK( message.contains( QStringLiteral( "Band" ) ) );
  CHECK( message.contains( QStringLiteral( "9" ) ) );
  CHECK_FALSE( message.contains( QStringLiteral( "training samples" ) ) );
}

TEST_CASE( "ClassifierSetupBar: restored unknown classifier kind falls back",
           "[classify][ux][r26-workbench]" )
{
  ensureApp();
  RsClassifierSetupBar bar;
  bar.setCurrentKind( static_cast<RsClassifierKind>( 99 ) );
  CHECK( bar.currentKind() == RsClassifierKind::NormalBayes );
  const auto buttons = bar.findChildren<QAbstractButton *>();
  int checked = 0;
  for ( auto *btn : buttons )
  {
    if ( btn->isCheckable() && btn->isChecked() )
      ++checked;
  }
  CHECK( checked >= 1 );  // exactly one algorithm button owns the selection
}
