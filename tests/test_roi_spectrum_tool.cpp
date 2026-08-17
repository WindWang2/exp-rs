// tests/test_roi_spectrum_tool.cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include "app/map_tools/rs_roi_spectrum_tool.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <qgsapplication.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsmapcanvas.h>
#include <qgsmapmouseevent.h>
#include <qgsproject.h>
#include <qgsrasterlayer.h>

#include <array>
#include <memory>
#include <vector>

using Catch::Approx;

namespace {

struct TestAppFixture {
  TestAppFixture() {
    if ( !QCoreApplication::instance() ) {
      static int argc = 1;
      static char appName[] = "test_roi_spectrum_tool";
      static char *argv[] = { appName, nullptr };
      s_app = new QApplication( argc, argv );
      QgsApplication::initQgis();
    }
  }

  static QApplication *s_app;
};
QApplication *TestAppFixture::s_app = nullptr;

class TestableRsRoiSpectrumTool : public RsRoiSpectrumTool {
public:
  using RsRoiSpectrumTool::RsRoiSpectrumTool;
  using RsRoiSpectrumTool::canvasPressEvent;
  using RsRoiSpectrumTool::canvasDoubleClickEvent;
};

QString makeRoiRaster( const QString &path )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 8, 0.0f ) );
    for ( int row = 0; row < 2; ++row )
    {
        for ( int col = 0; col < 4; ++col )
        {
            bands[0][static_cast<size_t>( row * 4 + col )] = static_cast<float>( row * 10 + col );
            bands[1][static_cast<size_t>( row * 4 + col )] = 100.0f + static_cast<float>( row * 10 + col );
        }
    }
    QString err;
    REQUIRE( writeGdalOutput( path, 4, 2, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );
    return {};
}

} // namespace

TEST_CASE( "RsRoiSpectrumTool computes ROI spectrum asynchronously (#322)", "[app][map_tools][spectral_roi]" )
{
    TestAppFixture fixture;

    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString rasterPath = tmp.filePath( QStringLiteral( "roi_test.tif" ) );
    REQUIRE( makeRoiRaster( rasterPath ).isEmpty() );

    auto rasterLayer = std::make_unique<QgsRasterLayer>( rasterPath, QStringLiteral( "roi_test" ) );
    REQUIRE( rasterLayer->isValid() );

    auto canvas = std::make_unique<QgsMapCanvas>();
    canvas->setDestinationCrs( rasterLayer->crs() );
    canvas->setExtent( rasterLayer->extent() );

    bool callbackFired = false;
    QVector<double> resultValues;
    QVector<double> resultWavelengths;
    QVector<QString> resultLabels;
    QString resultLayerName;

    QEventLoop loop;

    auto tool = std::make_unique<TestableRsRoiSpectrumTool>(
        canvas.get(), rasterLayer.get(),
        [&]( const QVector<double> &values, const QVector<double> &wavelengths,
             const QVector<QString> &labels, const QString &layerName ) {
            callbackFired = true;
            resultValues = values;
            resultWavelengths = wavelengths;
            resultLabels = labels;
            resultLayerName = layerName;
            loop.quit();
        } );

    // Draw 4 points covering left half: (0,0) -> (2,0) -> (2,-2) -> (0,-2)
    QPoint pt0 = canvas->mapSettings().mapToPixel().transform( QgsPointXY( 0.1, -0.1 ) ).toQPointF().toPoint();
    QPoint pt1 = canvas->mapSettings().mapToPixel().transform( QgsPointXY( 1.9, -0.1 ) ).toQPointF().toPoint();
    QPoint pt2 = canvas->mapSettings().mapToPixel().transform( QgsPointXY( 1.9, -1.9 ) ).toQPointF().toPoint();
    QPoint pt3 = canvas->mapSettings().mapToPixel().transform( QgsPointXY( 0.1, -1.9 ) ).toQPointF().toPoint();

    QgsMapMouseEvent e0( canvas.get(), QEvent::MouseButtonPress, pt0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    tool->canvasPressEvent( &e0 );

    QgsMapMouseEvent e1( canvas.get(), QEvent::MouseButtonPress, pt1, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    tool->canvasPressEvent( &e1 );

    QgsMapMouseEvent e2( canvas.get(), QEvent::MouseButtonPress, pt2, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    tool->canvasPressEvent( &e2 );

    QgsMapMouseEvent e3( canvas.get(), QEvent::MouseButtonPress, pt3, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    tool->canvasPressEvent( &e3 );

    // Finish with double click
    QgsMapMouseEvent eDbl( canvas.get(), QEvent::MouseButtonDblClick, pt3, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    tool->canvasDoubleClickEvent( &eDbl );

    // Timeout safety
    QTimer::singleShot( 5000, &loop, &QEventLoop::quit );
    loop.exec();

    REQUIRE( callbackFired );
    REQUIRE( resultLayerName == "roi_test" );
    REQUIRE( resultValues.size() == 2 );
    CHECK( resultValues[0] == Approx( 5.5 ).margin( 0.1 ) );
    CHECK( resultValues[1] == Approx( 105.5 ).margin( 0.1 ) );
    REQUIRE( resultLabels.size() == 2 );
}
