// study_spatial.cpp — buffer summary + GDAL adapter.
#include "study/study_spatial.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

#include <cmath>
#include <limits>

#include "gdal.h"
#include "gdal_priv.h"
#include "ogr_spatialref.h"

namespace sicnu::study
{
namespace
{

Diagnostic spatialError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

} // namespace

QJsonObject SpatialDifferenceSummary::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSpatialSummarySchemaVersion );
    json.insert( QStringLiteral( "baseline_path" ), baselinePath );
    json.insert( QStringLiteral( "run_path" ), runPath );
    json.insert( QStringLiteral( "total_pixels" ), static_cast<double>( totalPixels ) );
    json.insert( QStringLiteral( "valid_pixels" ), static_cast<double>( validPixels ) );
    json.insert( QStringLiteral( "changed_pixels" ), static_cast<double>( changedPixels ) );
    json.insert( QStringLiteral( "changed_percent" ), changedPercent );
    json.insert( QStringLiteral( "mean_abs_diff" ), meanAbsDiff );
    json.insert( QStringLiteral( "max_abs_diff" ), maxAbsDiff );
    json.insert( QStringLiteral( "rms_diff" ), rmsDiff );
    return json;
}

Result<SpatialDifferenceSummary> SpatialDifferenceSummary::fromJson( const QJsonObject &json )
{
    if ( json.value( QStringLiteral( "schema_version" ) ).toInt()
         != kSpatialSummarySchemaVersion )
        return Result<SpatialDifferenceSummary>::failure( spatialError(
            QStringLiteral( "study.spatial_unsupported_version" ),
            QStringLiteral( "spatial summary requires schema_version %1" )
                .arg( kSpatialSummarySchemaVersion ) ) );
    SpatialDifferenceSummary summary;
    summary.baselinePath = json.value( QStringLiteral( "baseline_path" ) ).toString();
    summary.runPath = json.value( QStringLiteral( "run_path" ) ).toString();
    summary.totalPixels =
        static_cast<qint64>( json.value( QStringLiteral( "total_pixels" ) ).toDouble() );
    summary.validPixels =
        static_cast<qint64>( json.value( QStringLiteral( "valid_pixels" ) ).toDouble() );
    summary.changedPixels =
        static_cast<qint64>( json.value( QStringLiteral( "changed_pixels" ) ).toDouble() );
    summary.changedPercent = json.value( QStringLiteral( "changed_percent" ) ).toDouble();
    summary.meanAbsDiff = json.value( QStringLiteral( "mean_abs_diff" ) ).toDouble();
    summary.maxAbsDiff = json.value( QStringLiteral( "max_abs_diff" ) ).toDouble();
    summary.rmsDiff = json.value( QStringLiteral( "rms_diff" ) ).toDouble();
    return Result<SpatialDifferenceSummary>::success( summary );
}

SpatialDifferenceSummary summarizeBufferDifference( const QString &baselinePath,
                                                    const QString &runPath,
                                                    const float *baseline,
                                                    const float *run, qint64 count,
                                                    double epsilon )
{
    SpatialDifferenceSummary summary;
    summary.baselinePath = baselinePath;
    summary.runPath = runPath;
    summary.totalPixels = count;

    double absSum = 0.0;
    double squaredSum = 0.0;
    for ( qint64 i = 0; i < count; ++i )
    {
        const float a = baseline[i];
        const float b = run[i];
        // NaN marks invalid — the pixel carries no evidence in either raster.
        if ( !std::isfinite( a ) || !std::isfinite( b ) )
            continue;
        ++summary.validPixels;
        const double diff = std::fabs( static_cast<double>( a ) - static_cast<double>( b ) );
        absSum += diff;
        squaredSum += diff * diff;
        if ( diff > summary.maxAbsDiff )
            summary.maxAbsDiff = diff;
        if ( diff > epsilon )
            ++summary.changedPixels;
    }
    if ( summary.validPixels > 0 )
    {
        const double n = static_cast<double>( summary.validPixels );
        summary.meanAbsDiff = absSum / n;
        summary.rmsDiff = std::sqrt( squaredSum / n );
        summary.changedPercent =
            100.0 * static_cast<double>( summary.changedPixels ) / n;
    }
    return summary;
}

GdalRasterDifferenceSummarizer::GdalRasterDifferenceSummarizer( double epsilon )
    : m_epsilon( epsilon )
{
}

Result<SpatialDifferenceSummary> GdalRasterDifferenceSummarizer::summarize(
    const QString &baselinePath, const QString &runPath ) const
{
    GDALAllRegister();

    // RAII close guard (the house GDAL pattern uses raw datasets).
    struct DatasetGuard
    {
        GDALDataset *dataset = nullptr;
        ~DatasetGuard()
        {
            if ( dataset )
                GDALClose( dataset );
        }
    };

    DatasetGuard baselineGuard;
    baselineGuard.dataset = GDALDataset::Open( baselinePath.toUtf8().constData(),
                                               GDAL_OF_READONLY | GDAL_OF_RASTER );
    if ( !baselineGuard.dataset )
        return Result<SpatialDifferenceSummary>::failure( spatialError(
            QStringLiteral( "study.spatial_unreadable" ),
            QStringLiteral( "cannot open baseline raster %1" ).arg( baselinePath ) ) );
    DatasetGuard runGuard;
    runGuard.dataset = GDALDataset::Open( runPath.toUtf8().constData(),
                                          GDAL_OF_READONLY | GDAL_OF_RASTER );
    if ( !runGuard.dataset )
        return Result<SpatialDifferenceSummary>::failure( spatialError(
            QStringLiteral( "study.spatial_unreadable" ),
            QStringLiteral( "cannot open run raster %1" ).arg( runPath ) ) );
    GDALDataset *baseline = baselineGuard.dataset;
    GDALDataset *run = runGuard.dataset;

    // Grid equality: dimensions, geotransform and CRS. A study compares the
    // SAME frame under DIFFERENT parameters — anything else is a registration
    // problem and is refused, not papered over.
    if ( baseline->GetRasterXSize() != run->GetRasterXSize()
         || baseline->GetRasterYSize() != run->GetRasterYSize() )
        return Result<SpatialDifferenceSummary>::failure( spatialError(
            QStringLiteral( "study.spatial_mismatch" ),
            QStringLiteral( "raster dimensions differ (%1x%2 vs %3x%4)" )
                .arg( baseline->GetRasterXSize() )
                .arg( baseline->GetRasterYSize() )
                .arg( run->GetRasterXSize() )
                .arg( run->GetRasterYSize() ) ) );

    double baselineGeotransform[6] = {};
    double runGeotransform[6] = {};
    const bool baselineHasGeotransform =
        baseline->GetGeoTransform( baselineGeotransform ) == CE_None;
    const bool runHasGeotransform = run->GetGeoTransform( runGeotransform ) == CE_None;
    if ( baselineHasGeotransform != runHasGeotransform
         || ( baselineHasGeotransform
              && !std::equal( baselineGeotransform, baselineGeotransform + 6,
                              runGeotransform ) ) )
        return Result<SpatialDifferenceSummary>::failure(
            spatialError( QStringLiteral( "study.spatial_mismatch" ),
                          QStringLiteral( "raster geotransforms differ" ) ) );

    const OGRSpatialReference *baselineCrs = baseline->GetSpatialRef();
    const OGRSpatialReference *runCrs = run->GetSpatialRef();
    const bool bothCrs = baselineCrs && runCrs;
    const bool noCrs = !baselineCrs && !runCrs;
    if ( !noCrs
         && ( !bothCrs || !baselineCrs->IsSame( const_cast<OGRSpatialReference *>( runCrs ) ) ) )
        return Result<SpatialDifferenceSummary>::failure(
            spatialError( QStringLiteral( "study.spatial_mismatch" ),
                          QStringLiteral( "raster CRS differ" ) ) );

    GDALRasterBand *baselineBand = baseline->GetRasterBand( 1 );
    GDALRasterBand *runBand = run->GetRasterBand( 1 );
    if ( !baselineBand || !runBand )
        return Result<SpatialDifferenceSummary>::failure(
            spatialError( QStringLiteral( "study.spatial_unreadable" ),
                          QStringLiteral( "rasters must have at least one band" ) ) );

    const int width = baseline->GetRasterXSize();
    const int height = baseline->GetRasterYSize();
    const qint64 count = static_cast<qint64>( width ) * height;
    if ( count > kMaxSpatialComparePixels )
        return Result<SpatialDifferenceSummary>::failure(
            spatialError( QStringLiteral( "study.spatial_too_large" ),
                          QStringLiteral( "raster holds %1 pixels; whole-raster"
                                          " comparison caps at %2 — tile the input"
                                          " instead" )
                              .arg( count )
                              .arg( kMaxSpatialComparePixels ) ) );

    QVector<float> baselineBuffer( count );
    QVector<float> runBuffer( count );
    if ( baselineBand->RasterIO( GF_Read, 0, 0, width, height, baselineBuffer.data(), width,
                                 height, GDT_Float32, 0, 0, nullptr )
             != CE_None
         || runBand->RasterIO( GF_Read, 0, 0, width, height, runBuffer.data(), width, height,
                               GDT_Float32, 0, 0, nullptr )
             != CE_None )
        return Result<SpatialDifferenceSummary>::failure(
            spatialError( QStringLiteral( "study.spatial_unreadable" ),
                          QStringLiteral( "cannot read raster pixels" ) ) );

    // Nodata is no evidence: mask it before summarizing.
    int baselineHasNodata = 0;
    int runHasNodata = 0;
    const double baselineNodata = baselineBand->GetNoDataValue( &baselineHasNodata );
    const double runNodata = runBand->GetNoDataValue( &runHasNodata );
    for ( qint64 i = 0; i < count; ++i )
    {
        if ( baselineHasNodata && baselineBuffer.at( i ) == baselineNodata )
            baselineBuffer[i] = std::numeric_limits<float>::quiet_NaN();
        if ( runHasNodata && runBuffer.at( i ) == runNodata )
            runBuffer[i] = std::numeric_limits<float>::quiet_NaN();
    }

    return Result<SpatialDifferenceSummary>::success( summarizeBufferDifference(
        baselinePath, runPath, baselineBuffer.constData(), runBuffer.constData(), count,
        m_epsilon ) );
}

} // namespace sicnu::study
