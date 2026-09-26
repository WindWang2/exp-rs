// tests/support/r4_operator_fixtures.h — Track 7 (R4) shared operator-test
// fixtures. Header-only; helpers are inline so unused ones stay warning-free
// in suites that include only part of the surface. Truth values live in the
// TEST_CASEs, not here — this file only writes/reads fixtures and runs
// operators through the registry seam.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include "operators/framework/rs_operator.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_registry.h"

#include <json/json.h>

#include <QFile>
#include <QString>
#include <QTextStream>

#include <gdal.h>
#include <gdal_priv.h>

#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace r4fixtures
{
inline constexpr double kSentinel = -9999.0;

inline const char *wgs84Wkt()
{
    return "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563,"
           "AUTHORITY[\"EPSG\",\"7030\"]],AUTHORITY[\"EPSG\",\"6326\"]],PRIMEM[\"Greenwich\",0,"
           "AUTHORITY[\"EPSG\",\"8901\"]],UNIT[\"degree\",0.0174532925199433,"
           "AUTHORITY[\"EPSG\",\"9122\"]],AUTHORITY[\"EPSG\",\"4326\"]]";
}

inline void setGrid( GDALDatasetH ds, int h )
{
    const double gt[6] = { 0.0, 1.0, 0.0, static_cast<double>( h ), 0.0, -1.0 };
    REQUIRE( GDALSetGeoTransform( ds, gt ) == CE_None );
    REQUIRE( GDALSetProjection( ds, wgs84Wkt() ) == CE_None );
}

/// Float32 GeoTIFF, one band per value row; declares @p nodata when @p hasNoData.
inline QString writeFloatRaster( const QString &path, int w, int h,
                                 const std::vector<std::vector<float>> &bands,
                                 bool hasNoData, double nodata )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h,
                                  static_cast<int>( bands.size() ), GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    setGrid( ds, h );
    for ( size_t b = 0; b < bands.size(); ++b )
    {
        GDALRasterBandH band = GDALGetRasterBand( ds, static_cast<int>( b ) + 1 );
        if ( hasNoData )
            GDALSetRasterNoDataValue( band, nodata );
        REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                               const_cast<float *>( bands[b].data() ), w, h, GDT_Float32,
                               0, 0 ) == CE_None );
    }
    GDALClose( ds );
    return path;
}

/// UInt16 variant (QA words, SCL).
inline QString writeU16Raster( const QString &path, int w, int h,
                               const std::vector<uint16_t> &values, bool hasNoData,
                               double nodata )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_UInt16,
                                  nullptr );
    REQUIRE( ds != nullptr );
    setGrid( ds, h );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    if ( hasNoData )
        GDALSetRasterNoDataValue( band, nodata );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                           const_cast<uint16_t *>( values.data() ), w, h, GDT_UInt16,
                           0, 0 ) == CE_None );
    GDALClose( ds );
    return path;
}

/// Byte mask (band 1, >0 = masked) for rs:apply_mask.
inline QString writeByteMask( const QString &path, int w, int h,
                              const std::vector<uint8_t> &values )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), w, h, 1, GDT_Byte,
                                  nullptr );
    REQUIRE( ds != nullptr );
    setGrid( ds, h );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    REQUIRE( GDALRasterIO( band, GF_Write, 0, 0, w, h,
                           const_cast<uint8_t *>( values.data() ), w, h, GDT_Byte, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return path;
}

/// Zone polygon covering the whole grid; zone key from property @p key.
inline QString writeZoneGeoJson( const QString &path, const std::string &key, int extent = 10 )
{
    std::ofstream out( path.toStdString() );
    REQUIRE( out.is_open() );
    out << "{\"type\":\"FeatureCollection\",\"features\":[{"
        << "\"type\":\"Feature\",\"properties\":{\"" << key << "\":\"A\"},"
        << "\"geometry\":{\"type\":\"Polygon\",\"coordinates\":["
        << "[[0,0],[" << extent << ",0],[" << extent << "," << extent << "],[0," << extent
        << "],[0,0]]]}}]}";
    return path;
}

/// Runs a registered operator through the public run() seam.
inline Json::Value runOperator( const std::string &id, const Json::Value &params,
                                const std::string &workDir )
{
    auto op = sicnu::operators::RSOperatorRegistry::instance().create( id );
    REQUIRE( op != nullptr );
    sicnu::operators::RSOperatorContext ctx( workDir );
    return op->run( params, ctx );
}

inline std::vector<float> readBand( const QString &path, int band1Based,
                                    bool *hasNoData = nullptr, double *noData = nullptr )
{
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, band1Based );
    REQUIRE( band != nullptr );
    if ( hasNoData || noData )
    {
        int has = 0;
        const double nd = GDALGetRasterNoDataValue( band, &has );
        if ( hasNoData )
            *hasNoData = has != 0;
        if ( noData )
            *noData = nd;
    }
    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    std::vector<float> values( static_cast<size_t>( w ) * h );
    REQUIRE( GDALRasterIO( band, GF_Read, 0, 0, w, h, values.data(), w, h, GDT_Float32,
                           0, 0 ) == CE_None );
    GDALClose( ds );
    return values;
}

inline std::vector<uint8_t> readByteBand( const QString &path, int band1Based )
{
    GDALAllRegister();
    GDALDatasetH ds = GDALOpen( path.toUtf8().constData(), GA_ReadOnly );
    REQUIRE( ds != nullptr );
    GDALRasterBandH band = GDALGetRasterBand( ds, band1Based );
    REQUIRE( band != nullptr );
    const int w = GDALGetRasterXSize( ds );
    const int h = GDALGetRasterYSize( ds );
    std::vector<uint8_t> values( static_cast<size_t>( w ) * h );
    REQUIRE( GDALRasterIO( band, GF_Read, 0, 0, w, h, values.data(), w, h, GDT_Byte, 0, 0 )
             == CE_None );
    GDALClose( ds );
    return values;
}

/// Relative closeness with an absolute floor of 1.
inline bool nearRel( double got, double want, double rel = 1e-9 )
{
    return std::fabs( got - want ) <= rel * std::max( 1.0, std::fabs( want ) );
}

/// Naive CSV split for the zonal/segment statistics products.
inline std::vector<std::vector<std::string>> readCsv( const QString &path )
{
    QFile f( path );
    REQUIRE( f.open( QIODevice::ReadOnly | QIODevice::Text ) );
    std::vector<std::vector<std::string>> rows;
    QTextStream ts( &f );
    while ( !ts.atEnd() )
    {
        const QString line = ts.readLine();
        if ( line.isEmpty() )
            continue;
        std::vector<std::string> cols;
        int start = 0;
        for ( int i = 0; i <= line.size(); ++i )
        {
            if ( i == line.size() || line[i] == ',' )
            {
                cols.push_back( line.mid( start, i - start ).toStdString() );
                start = i + 1;
            }
        }
        rows.push_back( std::move( cols ) );
    }
    return rows;
}

} // namespace r4fixtures
