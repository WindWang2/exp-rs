// test_r26_workbench_roi_histogram.cpp — R26-02 workbench: ROI statistics
// refresh-state recovery, all-NoData honesty, and histogram stretch spinbox
// re-anchoring after the async scan lands.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QThread>

#include <qgsapplication.h>
#include <qgsrasterlayer.h>

#include <gdal.h>

#include "app/widgets/roi_statistics_widget.h"
#include "app/widgets/histogram_stretch_widget.h"
#include "app/widgets/histogram_widget.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Approx;

namespace
{

struct RoiHistoFixture
{
    RoiHistoFixture()
    {
        if ( !QCoreApplication::instance() )
        {
            static int argc = 1;
            static char appName[] = "test_r26_workbench_roi_histogram";
            static char *argv[] = { appName, nullptr };
            s_app = new QApplication( argc, argv );
            QgsApplication::initQgis();
        }
    }
    static QApplication *s_app;
};
QApplication *RoiHistoFixture::s_app = nullptr;

void pumpEvents( int iterations = 400 )
{
    for ( int i = 0; i < iterations; ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
}

/// 4x4 two-band raster: band1 row*10+col, band2 100+row*10+col, with the
/// given pixels masked by sentinel -999 (dataset NoData).
QString makeRaster( const QString &path, const std::vector<int> &nodataPixels = {} )
{
    ensureGdalInit();
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 2, std::vector<float>( 16, 0.0f ) );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
        {
            bands[0][static_cast<size_t>( row * 4 + col )] = static_cast<float>( row * 10 + col );
            bands[1][static_cast<size_t>( row * 4 + col )] = 100.0f + static_cast<float>( row * 10 + col );
        }
    for ( int idx : nodataPixels )
    {
        bands[0][static_cast<size_t>( idx )] = -999.0f;
        bands[1][static_cast<size_t>( idx )] = -999.0f;
    }
    QString err;
    if ( !writeGdalOutput( path, 4, 4, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) )
        return err;
    if ( !nodataPixels.empty() )
    {
        GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_Update );
        if ( !ds )
            return QStringLiteral( "nodata update open failed" );
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 1 ), -999.0 );
        GDALSetRasterNoDataValue( GDALGetRasterBand( ds, 2 ), -999.0 );
        GDALClose( ds );
    }
    return {};
}

}  // namespace

TEST_CASE( "RoiStatisticsWidget recovers the Refresh button after a layer switch",
           "[app][widgets][roi_stats][r26-workbench]" )
{
    RoiHistoFixture fixture;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString pathA = tmp.filePath( QStringLiteral( "a.tif" ) );
    const QString pathB = tmp.filePath( QStringLiteral( "b.tif" ) );
    REQUIRE( makeRaster( pathA ).isEmpty() );
    REQUIRE( makeRaster( pathB, { 0 } ).isEmpty() );

    auto layerA = std::make_unique<QgsRasterLayer>( pathA, QStringLiteral( "a" ) );
    auto layerB = std::make_unique<QgsRasterLayer>( pathB, QStringLiteral( "b" ) );
    REQUIRE( layerA->isValid() );
    REQUIRE( layerB->isValid() );

    RoiStatisticsWidget widget;
    widget.setRasterLayer( layerA.get() );
    widget.computeStatistics();
    // Switch layers while the scan is in flight: the landing must not leave
    // the Refresh button permanently disabled.
    widget.setRasterLayer( layerB.get() );
    pumpEvents();

    auto *refresh = widget.findChild<QPushButton *>( QStringLiteral( "rsRoiStatsRefreshBtn" ) );
    REQUIRE( refresh != nullptr );
    CHECK( refresh->isEnabled() );
    CHECK( widget.statistics().isEmpty() );  // stale stats dropped, none claimed
}

TEST_CASE( "RoiStatisticsWidget reports all-NoData bands as no-data, not zeros",
           "[app][widgets][roi_stats][r26-workbench]" )
{
    RoiHistoFixture fixture;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    // Every pixel is the sentinel: there is no valid measurement at all.
    std::vector<int> all;
    for ( int i = 0; i < 16; ++i )
        all.push_back( i );
    const QString path = tmp.filePath( QStringLiteral( "allnodata.tif" ) );
    REQUIRE( makeRaster( path, all ).isEmpty() );

    auto layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "allnodata" ) );
    REQUIRE( layer->isValid() );

    RoiStatisticsWidget widget;
    widget.setRasterLayer( layer.get() );
    widget.setRoiLayer( nullptr );
    widget.computeStatistics();
    pumpEvents();

    const auto stats = widget.statistics();
    REQUIRE( stats.size() == 2 );
    CHECK( stats[0].pixelCount == 0 );
    CHECK( std::isnan( stats[0].min ) );
    CHECK( std::isnan( stats[0].mean ) );
}

TEST_CASE( "HistogramStretchWidget re-anchors min/max spins when the scan lands",
           "[app][widgets][histogram][r26-workbench]" )
{
    RoiHistoFixture fixture;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );

    // Float32 raster whose real range [1000, 4000] is far away from the
    // 0..255 placeholder the widget anchors to before the scan lands.
    ensureGdalInit();
    const QString path = tmp.filePath( QStringLiteral( "wide.tif" ) );
    std::array<double, 6> gt = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
    std::vector<std::vector<float>> bands( 1, std::vector<float>( 256, 0.0f ) );
    for ( int i = 0; i < 256; ++i )
        bands[0][static_cast<size_t>( i )] = 1000.0f + 3000.0f * i / 255.0f;
    QString err;
    REQUIRE( writeGdalOutput( path, 16, 16, bands, gt, QStringLiteral( "EPSG:32648" ), &err ) );

    auto layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "wide" ) );
    REQUIRE( layer->isValid() );

    HistogramStretchWidget widget;
    widget.setRasterLayer( layer.get() );

    auto *histogram = widget.findChild<HistogramWidget *>();
    REQUIRE( histogram != nullptr );
    // Wait until the async scan lands the real physical range.
    for ( int i = 0; i < 400 && histogram->realDataMin() < 999.0; ++i )
    {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 10 );
        QThread::msleep( 5 );
    }
    REQUIRE( histogram->realDataMin() == Approx( 1000.0 ).margin( 1.0 ) );

    const auto spins = widget.findChildren<QDoubleSpinBox *>();
    REQUIRE( spins.size() >= 3 );
    bool hasMin = false, hasMax = false, hasPlaceholderZero = false;
    for ( auto *spin : spins )
    {
        if ( spin->value() == Approx( 1000.0 ).margin( 0.5 ) )
            hasMin = true;
        if ( spin->value() == Approx( 4000.0 ).margin( 0.5 ) )
            hasMax = true;
        if ( spin->value() == 0.0 )
            hasPlaceholderZero = true;
    }
    CHECK( hasMin );
    CHECK( hasMax );
    CHECK( !hasPlaceholderZero );

    SECTION( "a manual adjustment survives later bandDataReady landings" )
    {
        // Switch to the Levels algorithm first: it consumes the spin values
        // in its spec, so the widget's own snap-back reflects the user's
        // numbers (under the default piecewise algorithm applyCurrentSpec
        // legitimately rewrites the spins from the resolved stretch).
        const auto combos = widget.findChildren<QComboBox *>();
        REQUIRE( combos.size() >= 3 );
        combos[2]->setCurrentIndex( 1 );  // Photoshop Levels Adjustment
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );

        auto *minSpin = spins.first();
        const double keep = minSpin->value();
        minSpin->setValue( keep + 25.0 );  // user intent
        // Simulate a late landing (e.g. the RGB scan finishing) WITHOUT a
        // re-anchoring trigger like setBand: the user's cutoffs must win.
        QMetaObject::invokeMethod( histogram, "bandDataReady" );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
        CHECK( minSpin->value() == Approx( keep + 25.0 ).margin( 0.01 ) );
    }
}

TEST_CASE( "HistogramWidget rejects out-of-range band numbers",
           "[app][widgets][histogram][r26-workbench]" )
{
    RoiHistoFixture fixture;
    QTemporaryDir tmp;
    REQUIRE( tmp.isValid() );
    const QString path = tmp.filePath( QStringLiteral( "bands.tif" ) );
    REQUIRE( makeRaster( path ).isEmpty() );

    auto layer = std::make_unique<QgsRasterLayer>( path, QStringLiteral( "bands" ) );
    REQUIRE( layer->isValid() );

    HistogramWidget widget;
    widget.setRasterLayer( layer.get() );
    REQUIRE( widget.band() == 1 );
    widget.setBand( 2 );
    CHECK( widget.band() == 2 );
    // A band past bandCount must be refused, not accepted while the chart
    // keeps showing band 2's data under a lying title.
    widget.setBand( 99 );
    CHECK( widget.band() == 2 );
    CHECK( widget.lastError().contains( QStringLiteral( "outside" ) ) );
}
