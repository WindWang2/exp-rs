// tests/test_vector_layer_properties_lazy.cpp
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QScrollArea>
#include <QStackedWidget>

#include "gui/vector/qgsvectorlayerproperties.h"

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsgeometry.h>
#include <qgsmapcanvas.h>
#include <qgsmessagebar.h>
#include <qgsproject.h>
#include <qgsvectorlayer.h>

#include <memory>

namespace {

class TestableVectorLayerProperties : public QgsVectorLayerProperties {
public:
  TestableVectorLayerProperties( QgsMapCanvas *canvas, QgsMessageBar *messageBar, QgsVectorLayer *lyr, QWidget *parent = nullptr )
    : QgsVectorLayerProperties( canvas, messageBar, lyr, parent ) {}

  QStackedWidget *stackedWidget() const { return mOptStackedWidget; }
  void selectTab( int index ) { optionsStackedWidget_CurrentChanged( index ); }
};

} // namespace

int main( int argc, char *argv[] )
{
  QgsApplication application( argc, argv, true );
  QgsApplication::initQgis();
  const int result = Catch::Session().run( argc, argv );
  QgsProject::instance()->clear();
  QgsApplication::exitQgis();
  return result;
}

TEST_CASE( "QgsVectorLayerProperties defers statistics computation (#324)", "[gui][vector][properties]" )
{
  auto canvas = std::make_unique<QgsMapCanvas>();
  auto messageBar = std::make_unique<QgsMessageBar>();
  auto layer = std::make_unique<QgsVectorLayer>( "Polygon?crs=EPSG:4326&field=id:int&field=val:double", "test_poly_layer", "memory" );
  REQUIRE( layer->isValid() );

  // Add a test feature
  QgsFeature f( layer->fields() );
  f.setAttribute( "id", 1 );
  f.setAttribute( "val", 42.0 );
  f.setGeometry( QgsGeometry::fromWkt( "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))" ) );
  layer->dataProvider()->addFeature( f );

  {
    TestableVectorLayerProperties props( canvas.get(), messageBar.get(), layer.get() );

    // Constructor must NOT populate statistics eagerly
    CHECK_FALSE( props.isStatisticsFilled() );

    // Find statistics tab index
    int statsIndex = -1;
    for ( int i = 0; i < props.stackedWidget()->count(); ++i )
    {
      QWidget *w = props.stackedWidget()->widget( i );
      if ( w == props.statisticsPage() )
      {
        statsIndex = i;
        break;
      }
      if ( auto *sa = qobject_cast<QScrollArea *>( w ) )
      {
        if ( sa->widget() == props.statisticsPage() )
        {
          statsIndex = i;
          break;
        }
      }
    }

    REQUIRE( statsIndex >= 0 );

    props.selectTab( statsIndex );

    // Now statistics must be populated
    CHECK( props.isStatisticsFilled() );
  }
}
