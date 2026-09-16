// tests/test_spectral_agent_tools.cpp — D13 agent spectral tool tests
//
// Truths: physics laws (water absorbs in NIR; reflectance bound [0,1];
// vegetation red-edge ratio >= 2) audited against GDAL synthetic rasters
// with deliberately injected anomalies.
#define CATCH_CONFIG_RUNNER
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <json/json.h>

#include <qgsapplication.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <raster/qgsrasterlayer.h>

#include <QTemporaryDir>

#include <gdal_priv.h>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "agent/spatial_tools/spectral_spatial_tools.h"

namespace
{
    QgsApplication *app = nullptr;

    /// Writes a small Float32 GeoTiff whose every pixel carries @p red/@p nir
    /// (plus a blue band), with WAVELENGTH metadata (blue 490 / red 660 /
    /// nir 850 nm).
    QString writeAuditRaster( const QTemporaryDir &dir, const QString &name, double blue, double red,
                              double nir )
    {
        const QString path = dir.filePath( name );
        GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
        GDALDataset *ds = driver->Create( path.toStdString().c_str(), 3, 3, 3, GDT_Float32, nullptr );
        if ( !ds )
            return QString();
        // North-up pixel grid (origin top-left, 1 px cells): the point
        // (0.5, -0.5) used by every test below lands in pixel (0, 0). Without
        // an explicit geotransform the point maps to row -0.5 -> outside.
        double geotransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
        ds->SetGeoTransform( geotransform );
        const double bandValues[3] = { blue, red, nir };
        const double wavelengths[3] = { 490.0, 660.0, 850.0 };
        for ( int b = 1; b <= 3; ++b )
        {
            GDALRasterBand *band = ds->GetRasterBand( b );
            std::vector<float> line( 3, static_cast<float>( bandValues[b - 1] ) );
            for ( int row = 0; row < 3; ++row )
                band->RasterIO( GF_Write, 0, row, 3, 1, line.data(), 3, 1, GDT_Float32, 0, 0 );
            band->SetMetadataItem( "WAVELENGTH", QString::number( wavelengths[b - 1] ).toUtf8().constData() );
        }
        GDALClose( ds );
        return path;
    }

    /// Registers a raster file into the project under a fixed layer id, the
    /// same way an application session would.
    QgsRasterLayer *registerProjectLayer( const QString &path, const QString &layerId )
    {
        auto *layer = new QgsRasterLayer( path, layerId );
        layer->setId( layerId );
        QgsProject::instance()->addMapLayer( layer );
        return layer;
    }

    Json::Value pointParam( double x, double y )
    {
        Json::Value point( Json::arrayValue );
        point.append( x );
        point.append( y );
        return point;
    }
} // namespace

int main( int argc, char *argv[] )
{
    QgsApplication application( argc, argv, false );
    QgsApplication::initQgis();
    app = &application;
    const int result = Catch::Session().run( argc, argv );
    QgsProject::instance()->clear();
    QgsApplication::exitQgis();
    return result;
}

using exp_agent::SpectralInspectTool;
using exp_agent::ValidateBoaPhysicsTool;

TEST_CASE("Agent tool validate BOA physics detects inverted water spectrum",
          "[agent][spectral_tools]")
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A "water" pixel that glows in NIR (rho(NIR) 0.30 > rho(Red) 0.08,
    // NIR > 0.15): the audit must flag INVERTED_WATER_SPECTRUM.
    const QString path = writeAuditRaster( dir, QStringLiteral( "water_anomaly.tif" ), 0.05, 0.08, 0.30 );
    REQUIRE( !path.isEmpty() );
    QgsRasterLayer *layer = registerProjectLayer( path, QStringLiteral( "synthetic_water_layer" ) );
    REQUIRE( layer->isValid() );

    ValidateBoaPhysicsTool tool;
    Json::Value params;
    params["layer_id"] = "synthetic_water_layer";
    params["point"] = pointParam( 0.5, -0.5 );
    auto res = tool.execute( params );

    REQUIRE( res.success );
    REQUIRE( res.output["has_anomaly"].asBool() );
    REQUIRE( res.output["anomaly_code"].asString() == "INVERTED_WATER_SPECTRUM" );
    REQUIRE( res.output["suggestion"].asString().find( "atmospheric_correction" ) != std::string::npos );
    // Band selection is wavelength-driven: NIR = band 3 (850 nm), Red = band 2 (660 nm).
    REQUIRE( res.output["audit"]["nir_band_1based"].asInt() == 3 );
    REQUIRE( res.output["audit"]["red_band_1based"].asInt() == 2 );
}

TEST_CASE("Agent tool validate BOA physics flags unphysical reflectance ranges",
          "[agent][spectral_tools]")
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Reflectance 1.8 is outside the physical bound [0, 1].
    const QString path = writeAuditRaster( dir, QStringLiteral( "range_anomaly.tif" ), 0.10, 1.80, 0.05 );
    REQUIRE( !path.isEmpty() );
    registerProjectLayer( path, QStringLiteral( "range_layer" ) );

    ValidateBoaPhysicsTool tool;
    Json::Value params;
    params["layer_id"] = "range_layer";
    params["point"] = pointParam( 0.5, -0.5 );
    auto res = tool.execute( params );

    REQUIRE( res.success );
    REQUIRE( res.output["has_anomaly"].asBool() );
    REQUIRE( res.output["anomaly_code"].asString() == "UNPHYSICAL_REFLECTANCE_RANGE" );
}

TEST_CASE("Agent tool validate BOA physics accepts healthy physics", "[agent][spectral_tools]")
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Clean water: NIR << Red, all values inside [0, 1] → no anomaly.
    const QString path = writeAuditRaster( dir, QStringLiteral( "clean_water.tif" ), 0.07, 0.09, 0.01 );
    REQUIRE( !path.isEmpty() );
    registerProjectLayer( path, QStringLiteral( "clean_water_layer" ) );

    ValidateBoaPhysicsTool tool;
    Json::Value params;
    params["layer_id"] = "clean_water_layer";
    params["point"] = pointParam( 0.5, -0.5 );
    auto res = tool.execute( params );

    REQUIRE( res.success );
    REQUIRE_FALSE( res.output["has_anomaly"].asBool() );

    // Vegetation audit: healthy canopy NIR/Red = 0.60/0.10 = 6 >= 2 → pass.
    const QString vegPath = writeAuditRaster( dir, QStringLiteral( "veg.tif" ), 0.05, 0.10, 0.60 );
    registerProjectLayer( vegPath, QStringLiteral( "veg_layer" ) );
    Json::Value vegParams;
    vegParams["layer_id"] = "veg_layer";
    vegParams["expected_surface"] = "vegetation";
    vegParams["point"] = pointParam( 0.5, -0.5 );
    auto vegRes = tool.execute( vegParams );
    REQUIRE( vegRes.success );
    REQUIRE_FALSE( vegRes.output["has_anomaly"].asBool() );

    // Senescent canopy: NIR/Red = 0.20/0.30 < 2 → vegetation ratio inversion.
    const QString senescentPath = writeAuditRaster( dir, QStringLiteral( "senescent.tif" ), 0.05, 0.30, 0.20 );
    registerProjectLayer( senescentPath, QStringLiteral( "senescent_layer" ) );
    Json::Value senescentParams;
    senescentParams["layer_id"] = "senescent_layer";
    senescentParams["expected_surface"] = "vegetation";
    senescentParams["point"] = pointParam( 0.5, -0.5 );
    auto senescentRes = tool.execute( senescentParams );
    REQUIRE( senescentRes.success );
    REQUIRE( senescentRes.output["has_anomaly"].asBool() );
    REQUIRE( senescentRes.output["anomaly_code"].asString() == "INVERTED_VEGETATION_RATIO" );
}

TEST_CASE("Agent tool refuses to guess bands without wavelength metadata", "[agent][spectral_tools]")
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Same raster but NO WAVELENGTH metadata: the tool must fail closed.
    const QString path = writeAuditRaster( dir, QStringLiteral( "bare.tif" ), 0.05, 0.08, 0.30 );
    auto *layer = new QgsRasterLayer( path, QStringLiteral( "bare" ) );
    layer->setId( QStringLiteral( "bare_layer" ) );
    QgsProject::instance()->addMapLayer( layer );
    // Strip the wavelength metadata straight out of the file.
    {
        GDALDatasetUniquePtr ds( GDALDataset::Open( path.toStdString().c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE ) );
        REQUIRE( ds );
        for ( int b = 1; b <= 3; ++b )
            ds->GetRasterBand( b )->SetMetadataItem( "WAVELENGTH", nullptr );
    }

    ValidateBoaPhysicsTool tool;
    Json::Value params;
    params["layer_id"] = "bare_layer";
    params["point"] = pointParam( 0.5, -0.5 );
    auto res = tool.execute( params );

    REQUIRE_FALSE( res.success );
    REQUIRE( res.errorCode == "MISSING_WAVELENGTH_METADATA" );
}

TEST_CASE("Agent spectral inspect extracts spectrum, absorption and library match",
          "[agent][spectral_tools]")
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // A Gaussian-ish dip on a ramp: minimum at band 2 (660 nm).
    const QString path = writeAuditRaster( dir, QStringLiteral( "inspect.tif" ), 0.50, 0.20, 0.55 );
    registerProjectLayer( path, QStringLiteral( "inspect_layer" ) );

    // A tiny reference library in the legacy Library JSON shape.
    const QString libraryPath = dir.filePath( QStringLiteral( "mini_library.json" ) );
    {
        Json::Value root( Json::objectValue );
        root["id"] = "d13_mini";
        Json::Value entries( Json::arrayValue );
        Json::Value bright( Json::objectValue );
        bright["name"] = "bright soil";
        bright["material"] = "soil";
        bright["source"] = "D13 fixture";
        Json::Value brightSpectrum( Json::arrayValue );
        brightSpectrum.append( 0.50 );
        brightSpectrum.append( 0.55 );
        brightSpectrum.append( 0.60 );
        bright["spectrum"] = brightSpectrum;
        entries.append( bright );
        Json::Value dark( Json::objectValue );
        dark["name"] = "dark water";
        dark["material"] = "water";
        dark["source"] = "D13 fixture";
        Json::Value darkSpectrum( Json::arrayValue );
        darkSpectrum.append( 0.05 );
        darkSpectrum.append( 0.04 );
        darkSpectrum.append( 0.01 );
        dark["spectrum"] = darkSpectrum;
        entries.append( dark );
        root["entries"] = entries;
        Json::StreamWriterBuilder builder;
        std::ofstream out( libraryPath.toStdString() );
        out << Json::writeString( builder, root );
    }

    SpectralInspectTool tool;
    Json::Value params;
    params["layer_id"] = "inspect_layer";
    params["point"] = pointParam( 0.5, -0.5 );
    params["match_library"] = true;
    params["library_path"] = libraryPath.toStdString();
    auto res = tool.execute( params );

    REQUIRE( res.success );
    REQUIRE( res.output["reflectance"].size() == 3 );
    REQUIRE( res.output["wavelengths"].size() == 3 );
    REQUIRE( res.output["reflectance"][1].asDouble() == Catch::Approx(0.20).margin(1e-6) );
    REQUIRE( res.output["wavelengths"][1].asDouble() == Catch::Approx(660.0).margin(1e-6) );
    // Absorption sits at the 660 nm minimum relative to the end-point continuum.
    REQUIRE( res.output["absorption_wavelength_nm"].asDouble() == 660.0 );
    REQUIRE( res.output["absorption_depth"].asDouble() > 0.2 );
    REQUIRE( res.output["matched_material"].asString() == "soil" );
    // SAM angle vs the soil reference is acos(0.69/(|q||r|)) ≈ 0.394 rad
    // (22.6°), so the linear confidence 1 - deg/28.648 lands ≈ 0.21.
    REQUIRE( res.output["confidence"].asDouble() > 0.15 );
    REQUIRE( res.output["confidence"].asDouble() < 0.35 );
}

TEST_CASE("Agent tools validate their parameter schemas", "[agent][spectral_tools]")
{
    SpectralInspectTool inspect;
    REQUIRE( inspect.name() == "spatial:spectral_inspect" );
    REQUIRE( !inspect.inputSchema()["required"].empty() );
    REQUIRE( inspect.inputSchema()["type"].asString() == "object" );

    ValidateBoaPhysicsTool audit;
    REQUIRE( audit.name() == "spatial:validate_boa_physics" );

    // Neither layer_id nor path → validation failure with machine code.
    auto missing = audit.execute( Json::Value( Json::objectValue ) );
    REQUIRE_FALSE( missing.success );
    REQUIRE( missing.errorCode == "INVALID_PARAMETER" );

    // Unknown layer id → validation failure.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString path = writeAuditRaster( dir, QStringLiteral( "ghost.tif" ), 0.1, 0.1, 0.1 );
    registerProjectLayer( path, QStringLiteral( "ghost" ) );
    Json::Value bad;
    bad["layer_id"] = "does_not_exist";
    auto unknown = audit.execute( bad );
    REQUIRE_FALSE( unknown.success );
    REQUIRE( unknown.errorCode == "INVALID_PARAMETER" );
}
