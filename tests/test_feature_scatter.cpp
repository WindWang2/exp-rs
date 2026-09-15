// tests/test_feature_scatter.cpp — D15 Package G (density scatter).
//
// Ground-truth policy: bin indices for hand-placed points follow from the
// documented binning formula (floor over [min,max] with the y axis flipped
// for screen coordinates); the 40 ms budget is the spec's rendering gate.
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QColor>
#include <QImage>

#include <QTemporaryDir>

#include <chrono>
#include <cstdint>
#include <vector>

#include "app/workbench/classification_studio_widget.h"

using rs::app::FeatureScatterWidget;

namespace
{
  void ensureApplication()
  {
    if ( QApplication::instance() )
      return;
    static int argc = 1;
    static char appName[] = "test_feature_scatter";
    static char *argv[] = { appName, nullptr };
    static QApplication application( argc, argv );
    ( void ) application;
  }

  class Lcg
  {
    public:
      explicit Lcg( uint64_t seed ) : m_state( seed ) {}
      double nextUnit()
      {
        m_state = m_state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>( ( m_state >> 33 ) & 0xFFFFFF ) / 16777216.0;
      }

    private:
      uint64_t m_state;
  };
} // namespace

TEST_CASE( "Density thumbnail bins hand-placed points per the documented formula",
           "[d15][scatter]" )
{
  ensureApplication();
  FeatureScatterWidget w;
  REQUIRE( w.renderDensityThumbnail().isNull() ); // nothing yet

  w.setGridBinning( 4, 4 );
  // One sample at (0.125, 0.125) over the auto-derived [0,1] range:
  // bx = floor(0.125*4) = 0; by = floor((1 - 0.125)*4) = 3 (screen flip).
  const std::vector<float> x = { 0.125f };
  const std::vector<float> y = { 0.125f };
  const std::vector<int> labels = { 0 };
  w.setData( x, y, labels, QStringLiteral( "ndvi" ), QStringLiteral( "brightness" ) );

  const QImage img = w.renderDensityThumbnail();
  REQUIRE( img.size() == QSize( 4, 4 ) );
  const QColor occupied = img.pixelColor( 0, 3 );
  const QColor empty = img.pixelColor( 3, 0 );
  REQUIRE( occupied != empty );
  REQUIRE( empty.rgb() == QColor( 12, 12, 16 ).rgb() ); // documented zero-bin colour

  w.setGridBinning( 200, 200 );
  const QImage fine = w.renderDensityThumbnail();
  REQUIRE( fine.size() == QSize( 200, 200 ) );
}

TEST_CASE( "Density rendering survives degenerate single-valued clouds",
           "[d15][scatter]" )
{
  ensureApplication();
  FeatureScatterWidget w;
  std::vector<float> x( 100, 0.5f );
  std::vector<float> y( 100, 0.5f );
  w.setData( x, y, {}, QStringLiteral( "a" ), QStringLiteral( "b" ) );
  const QImage img = w.renderDensityThumbnail();
  REQUIRE_FALSE( img.isNull() );
  REQUIRE( img.size() == QSize( 200, 200 ) );
}

TEST_CASE( "100k samples bin and render within the 40 ms budget",
           "[d15][scatter]" )
{
  ensureApplication();
  FeatureScatterWidget w;
  Lcg lcg( 20260914ULL );
  std::vector<float> x( 100000 );
  std::vector<float> y( 100000 );
  for ( size_t i = 0; i < x.size(); ++i )
  {
    x[i] = static_cast<float>( lcg.nextUnit() * 2.0 - 1.0 );
    y[i] = static_cast<float>( lcg.nextUnit() * 2.0 - 1.0 );
  }

  const auto start = std::chrono::steady_clock::now();
  w.setData( x, y, {}, QStringLiteral( "f1" ), QStringLiteral( "f2" ) );
  const QImage img = w.renderDensityThumbnail();
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start )
                           .count();
  REQUIRE_FALSE( img.isNull() );
  REQUIRE( elapsedMs < 40 );
}
