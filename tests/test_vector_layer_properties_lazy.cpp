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

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <string>
#endif

namespace {
int fakeArgc = 1;
char fakeArg0[] = "test_vector_layer_properties_lazy";
char *fakeArgv[] = { fakeArg0, nullptr };

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
  QCoreApplication::setOrganizationName( "QGIS" );
  QCoreApplication::setApplicationName( "QGIS" );
  QgsApplication application( fakeArgc, fakeArgv, true );
  QgsApplication::initQgis();

#ifdef _WIN32
  int wargc = 0;
  LPWSTR *wargv = CommandLineToArgvW( GetCommandLineW(), &wargc );
  std::vector<std::string> utf8Args;
  std::vector<char *> utf8Argv;
  if ( wargv )
  {
    utf8Args.resize( wargc );
    utf8Argv.resize( wargc + 1, nullptr );
    for ( int i = 0; i < wargc; ++i )
    {
      int len = WideCharToMultiByte( CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr );
      if ( len > 0 )
      {
        utf8Args[i].resize( len - 1 );
        WideCharToMultiByte( CP_UTF8, 0, wargv[i], -1, &utf8Args[i][0], len, nullptr, nullptr );
      }
      utf8Argv[i] = const_cast<char *>( utf8Args[i].c_str() );
    }
    LocalFree( wargv );
  }
  const int result = Catch::Session().run( utf8Argv.empty() ? argc : static_cast<int>( utf8Argv.size() - 1 ),
                                           utf8Argv.empty() ? argv : utf8Argv.data() );
  _exit( result );
#else
  const int result = Catch::Session().run( argc, argv );
  return result;
#endif
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
