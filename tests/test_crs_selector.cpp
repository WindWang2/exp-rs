// test_crs_selector.cpp — shared CRS input widget (C5)
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>

#include "app/widgets/crs_selector.h"

namespace
{

void ensureQgisApplication()
{
  if ( QApplication::instance() )
    return;
  static int argc = 1;
  static char applicationName[] = "test_crs_selector";
  static char *argv[] = { applicationName, nullptr };
  static auto *application = new QgsApplication( argc, argv, true );
  ( void ) application;
  QgsApplication::initQgis();
}

} // namespace

TEST_CASE( "CrsSelector parses and reports the CRS string", "[crs_selector]" )
{
  ensureQgisApplication();

  CrsSelector selector;
  auto *edit = selector.lineEdit();
  REQUIRE( edit != nullptr );

  SECTION( "empty is invalid" )
  {
    CHECK_FALSE( selector.isValid() );
    CHECK( selector.crsString().isEmpty() );
  }

  SECTION( "authid round trip" )
  {
    selector.setCrsString( QStringLiteral( "EPSG:32650" ) );
    CHECK( selector.isValid() );
    CHECK( selector.crsString() == QStringLiteral( "EPSG:32650" ) );
    REQUIRE( selector.crs().isValid() );
    CHECK( selector.crs().authid() == QStringLiteral( "EPSG:32650" ) );
  }

  SECTION( "garbage is invalid" )
  {
    selector.setCrsString( QStringLiteral( "not-a-crs" ) );
    CHECK_FALSE( selector.isValid() );
  }

  SECTION( "crsChanged fires on edit" )
  {
    QString last;
    QObject::connect( &selector, &CrsSelector::crsChanged,
                      [&]( const QString &crs ) { last = crs; } );
    edit->setText( QStringLiteral( "EPSG:3857" ) );
    CHECK( last == QStringLiteral( "EPSG:3857" ) );
  }
}
