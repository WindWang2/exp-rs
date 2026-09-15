// tests/test_classification_studio_widget.cpp — D15 Package G (studio shell).
//
// Signal wiring, palette table and QPointer-guarded layer binding are
// exercised headlessly through public Qt surfaces (findChild / QSignalSpy).
#include <catch2/catch_test_macros.hpp>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <QComboBox>
#include <QImage>
#include <QPoint>
#include <QPolygonF>
#include <QSignalSpy>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "app/workbench/classification_studio_widget.h"
#include "synthetic_raster_builder.h"

using rs::app::ClassificationStudioWidget;

namespace
{
  void ensureQgisApplication()
  {
    if ( QApplication::instance() )
      return;
    static int argc = 1;
    static char appName[] = "test_classification_studio_widget";
    static char *argv[] = { appName, nullptr };
    static auto *application = new QgsApplication( argc, argv, true );
    ( void ) application;
    QgsApplication::initQgis();
  }

  std::unique_ptr<QgsRasterLayer> makeDiskLayer( const QTemporaryDir &dir )
  {
    const QString path = dir.filePath( QStringLiteral( "studio_disk.tif" ) );
    if ( sicnu::testing::RsSyntheticRasterBuilder( 100, 100, 1, GDT_Float32 )
           .withCircle( 1, 50, 50, 20, 100.0f, 0.0f )
           .writeToDisk( path )
           .isEmpty() )
      return nullptr;
    auto layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "studio" ) );
    if ( !layer->isValid() )
      return nullptr;
    return layer;
  }
} // namespace

TEST_CASE( "Studio palette table reflects the class definition", "[d15][studio]" )
{
  ensureQgisApplication();
  ClassificationStudioWidget w;
  w.setClassPalette( { 1, 2, 3 },
                     { QStringLiteral( "water" ), QStringLiteral( "vegetation" ), QStringLiteral( "built" ) },
                     { 0xFF1133AAu, 0xFF22CC44u, 0xFFDD6622u } );

  auto *table = w.findChild<QTableWidget *>();
  REQUIRE( table != nullptr );
  REQUIRE( table->rowCount() == 3 );
  REQUIRE( table->item( 1, 1 )->text() == QStringLiteral( "vegetation" ) );
  REQUIRE( table->item( 0, 0 )->text().toInt() == 1 );
}

TEST_CASE( "Swipe slider and algorithm combo emit typed signals", "[d15][studio]" )
{
  ensureQgisApplication();
  ClassificationStudioWidget w;

  QSignalSpy swipeSpy( &w, &ClassificationStudioWidget::swipeOffsetChanged );
  auto *slider = w.findChild<QSlider *>();
  REQUIRE( slider != nullptr );
  slider->setValue( 50 );
  REQUIRE( swipeSpy.count() == 1 );
  REQUIRE( swipeSpy.takeFirst().at( 0 ).value<float>() == 0.5f );

  QSignalSpy algoSpy( &w, &ClassificationStudioWidget::classificationRequested );
  auto *combo = w.findChild<QComboBox *>();
  REQUIRE( combo != nullptr );
  combo->setCurrentIndex( 2 );
  REQUIRE( algoSpy.count() == 1 );
  REQUIRE( algoSpy.takeFirst().at( 0 ).toInt() == 2 );
}

TEST_CASE( "Studio magic wand emits the extracted ROI with the selected class",
           "[d15][studio]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  auto layer = makeDiskLayer( dir );
  REQUIRE( layer != nullptr );

  ClassificationStudioWidget w;
  w.bindInputLayer( layer.get() );
  w.setClassPalette( { 7 },
                     { QStringLiteral( "water" ) },
                     { 0xFF1133AAu } );

  QSignalSpy roiSpy( &w, &ClassificationStudioWidget::roiExtracted );
  w.runMagicWandAt( QPoint( 50, 50 ), 5.0 );
  REQUIRE( roiSpy.count() == 1 );
  const auto args = roiSpy.takeFirst();
  REQUIRE( args.at( 0 ).toInt() == 7 );
  REQUIRE( args.at( 1 ).value<QPolygonF>().size() >= 4 );
}

TEST_CASE( "Studio wand is QPointer-safe against destroyed layers",
           "[d15][studio]" )
{
  ensureQgisApplication();
  QTemporaryDir dir;
  ClassificationStudioWidget w;

  // Never-bound and null-bound layers: no crash, no signal.
  QSignalSpy roiSpy( &w, &ClassificationStudioWidget::roiExtracted );
  w.runMagicWandAt( QPoint( 50, 50 ), 5.0 );
  REQUIRE( roiSpy.count() == 0 );

  auto layer = makeDiskLayer( dir );
  REQUIRE( layer != nullptr );
  w.bindInputLayer( layer.get() );
  layer.reset(); // destroy the raster under the widget's QPointer

  w.runMagicWandAt( QPoint( 50, 50 ), 5.0 );
  REQUIRE( roiSpy.count() == 0 );
}
