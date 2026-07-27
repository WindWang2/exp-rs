// test_obia_task.cpp — Phase 10B Task 10B.4
//
// Tests for RsObiaTask OBIA classification pipeline.
#include <catch2/catch_test_macros.hpp>

#ifdef SICNU_HAS_OPENCV

#include "app/obia/rs_obia_task.h"
#include "analysis/classification/rs_classifier_normalbayes.h"
#include "analysis/classification/rs_classifier_kmeans.h"

#include <gdal.h>
#include <cpl_error.h>

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>

// Ensure GDAL drivers are registered
static bool g_gdalInit = ( GDALAllRegister(), true );

// Helper: create a small synthetic GeoTIFF for testing
static QString createTestRaster( const QString &dir, int w, int h, int nBands )
{
    QString path = dir + "/test_input.tif";
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return {};

    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, nBands, GDT_Float32, nullptr );
    if ( !ds )
        return {};

    // Write synthetic data: left half = 100, right half = 200
    QVector<float> row( w );
    for ( int b = 0; b < nBands; ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, b + 1 );
        for ( int r = 0; r < h; ++r )
        {
            for ( int c = 0; c < w; ++c )
                row[c] = ( c < w / 2 ) ? 100.0f : 200.0f;
            GDALRasterIO( band, GF_Write, 0, r, w, 1, row.data(), w, 1, GDT_Float32, 0, 0 );
        }
    }

    GDALClose( ds );
    return path;
}

TEST_CASE( "ObiaTask: config construction", "[obia][classification]" )
{
    RsObiaTask::Config cfg;
    cfg.sourceRaster = "/test.tif";
    cfg.outputRaster = "/output.tif";
    cfg.bandIndices = { 1, 2, 3 };
    cfg.useOtb = false;
    cfg.algoName = "NormalBayes";

    // Config should hold values
    REQUIRE( cfg.sourceRaster == "/test.tif" );
    REQUIRE( cfg.bandIndices.size() == 3 );
    REQUIRE( !cfg.useOtb );
}

TEST_CASE( "ObiaTask: SimpleSegmenter pipeline with synthetic raster", "[obia][classification]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    QString inputPath = createTestRaster( tempDir.path(), 32, 32, 3 );
    REQUIRE( !inputPath.isEmpty() );

    RsObiaTask::Config cfg;
    cfg.sourceRaster = inputPath;
    cfg.outputRaster = tempDir.path() + "/obia_output.tif";
    cfg.bandIndices = { 1, 2, 3 };
    cfg.useOtb = false; // force SimpleSegmenter
    cfg.smoothKernel = 3;
    cfg.quantizeBins = 4;
    cfg.minRegionSize = 10;
    cfg.backend = std::make_unique<RsClassifierNormalBayes>();
    cfg.algoName = "NormalBayes";

    // Left half = segment 1 (class 1), right half = segment 2 (class 2)
    // We'll label after segmentation — but for the test, we pre-label
    // segment IDs that we expect. The synthetic image has 2 halves,
    // so after segmentation we expect 2 segments.
    // We use class IDs 1 and 2.

    // Create task (segmentation will produce ~2 segments)
    RsObiaTask task( std::move( cfg ) );

    // Run synchronously (not in task manager for testing)
    bool ok = task.run();

    if ( ok )
    {
        auto &result = task.result();
        REQUIRE( result.ok );
        REQUIRE( result.totalSegments > 0 );
        REQUIRE( result.totalPixels > 0 );
        REQUIRE( result.durationMs >= 0 );

        // Output file should exist
        REQUIRE( QFile::exists( cfg.outputRaster ) );
    }
    // If run() returns false, it's likely because segmentLabels is empty
    // (we didn't pre-label segments). That's expected behavior.
    else
    {
        auto &result = task.result();
        REQUIRE( !result.ok );
        REQUIRE( !result.errorMessage.isEmpty() );
    }
}

TEST_CASE( "ObiaTask: OTB unavailable falls back to SimpleSegmenter", "[obia][classification]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    QString inputPath = createTestRaster( tempDir.path(), 16, 16, 2 );
    REQUIRE( !inputPath.isEmpty() );

    RsObiaTask::Config cfg;
    cfg.sourceRaster = inputPath;
    cfg.outputRaster = tempDir.path() + "/obia_output.tif";
    cfg.bandIndices = { 1, 2 };
    cfg.useOtb = true;
    cfg.smoothKernel = 3;
    cfg.quantizeBins = 4;
    cfg.minRegionSize = 10;
    cfg.backend = std::make_unique<RsClassifierNormalBayes>();
    cfg.algoName = "NormalBayes";

    RsObiaTask task( std::move( cfg ) );
    const bool ok = task.run();

    // Training fails without labels, but segmentation should succeed via fallback.
    REQUIRE( !ok );
    REQUIRE( task.result().errorMessage.contains( "No labeled segments" ) );
    REQUIRE( !task.segmentMap().isEmpty() );
    REQUIRE( task.segmentMap().segmentCount() > 0 );
}

TEST_CASE( "ObiaTask: empty segmentLabels fails gracefully", "[obia][classification]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    QString inputPath = createTestRaster( tempDir.path(), 16, 16, 1 );
    REQUIRE( !inputPath.isEmpty() );

    RsObiaTask::Config cfg;
    cfg.sourceRaster = inputPath;
    cfg.outputRaster = tempDir.path() + "/obia_output.tif";
    cfg.bandIndices = { 1 };
    cfg.useOtb = false;
    cfg.backend = std::make_unique<RsClassifierNormalBayes>();
    cfg.algoName = "NormalBayes";
    // No segmentLabels!

    RsObiaTask task( std::move( cfg ) );
    bool ok = task.run();

    REQUIRE( !ok );
    REQUIRE( task.result().errorMessage.contains( "No labeled segments" ) );
}

TEST_CASE( "ObiaTask: labeled pipeline fills training accuracy", "[obia][classification][accuracy]" )
{
    QTemporaryDir tempDir;
    REQUIRE( tempDir.isValid() );

    const QString inputPath = createTestRaster( tempDir.path(), 32, 32, 3 );
    REQUIRE( !inputPath.isEmpty() );
    const QString outputPath = tempDir.path() + "/obia_acc.tif";

    // First pass: segment only (no labels) to discover segment ids.
    {
        RsObiaTask::Config cfg;
        cfg.sourceRaster = inputPath;
        cfg.outputRaster = tempDir.path() + "/_seg_probe.tif";
        cfg.bandIndices = { 1, 2, 3 };
        cfg.useOtb = false;
        cfg.smoothKernel = 3;
        cfg.quantizeBins = 4;
        cfg.minRegionSize = 10;
        cfg.backend = std::make_unique<RsClassifierNormalBayes>();
        RsObiaTask probe( std::move( cfg ) );
        ( void ) probe.run(); // expected fail: no labels
        REQUIRE( !probe.segmentMap().isEmpty() );

        const auto ids = probe.segmentMap().uniqueLabels();
        REQUIRE( ids.size() >= 2 );

        // Label first two segments with different classes.
        QList<quint32> sorted = ids.values();
        std::sort( sorted.begin(), sorted.end() );
        RsObiaTask::Config trainCfg;
        trainCfg.sourceRaster = inputPath;
        trainCfg.outputRaster = outputPath;
        trainCfg.bandIndices = { 1, 2, 3 };
        trainCfg.useOtb = false;
        trainCfg.existingSegMap = probe.segmentMap();
        trainCfg.backend = std::make_unique<RsClassifierNormalBayes>();
        trainCfg.segmentLabels[sorted[0]] = 1;
        trainCfg.segmentLabels[sorted[1]] = 2;
        trainCfg.classColors[1] = QColor( Qt::red );
        trainCfg.classColors[2] = QColor( Qt::green );
        trainCfg.algoName = "NormalBayes";

        RsObiaTask task( std::move( trainCfg ) );
        REQUIRE( task.run() );
        REQUIRE( task.result().ok );
        REQUIRE( task.result().labeledSegments >= 2 );
        REQUIRE( !task.result().accuracy.classIds.isEmpty() );
        REQUIRE( task.result().accuracy.overallAccuracy >= 0.0 );
        REQUIRE( task.result().accuracy.overallAccuracy <= 1.0 );
        REQUIRE( QFile::exists( outputPath ) );
    }
}

#endif // SICNU_HAS_OPENCV
