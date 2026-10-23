// R20-05 #1600 RED driver: a raster larger than the sampling lattice must
// produce grid evidence stamped hasSampling=true + sampledPoints>0.
#include "verify/verify_engine.h"
#include "verify_adapters/gdal_grid_probe.h"
#include "verify_adapters/fs_artifact_probe.h"
#include <gdal.h>
#include <cpl_conv.h>
#include <ogr_srs_api.h>
#include <iostream>
#include <string>
#include <vector>

using namespace sicnu::verify;

int main()
{
    const std::string path = "/tmp/r2005_red/sv_100.tif";
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver ) { std::cout << "no GTiff driver\n"; return 2; }
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), 100, 100, 1, GDT_Float32, nullptr );
    if ( !ds ) { std::cout << "create failed\n"; return 2; }
    double gt[6] = { 0.0, 10.0, 0.0, 0.0, 0.0, -10.0 };
    GDALSetGeoTransform( ds, gt );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    char *wkt = nullptr;
    OSRSetFromUserInput( srs, "EPSG:4326" );
    OSRExportToWkt( srs, &wkt );
    GDALSetProjection( ds, wkt );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> px( 100 * 100, 1.0f );
    GDALRasterIO( band, GF_Write, 0, 0, 100, 100, px.data(), 100, 100, GDT_Float32, 0, 0 );
    GDALClose( ds );

    sicnu::verify_adapters::GdalGridProbe probe;
    sicnu::verify_adapters::FsArtifactProbe artifactProbe;

    VerificationCheckSpec grid;
    grid.checkId = "g";
    grid.kind = "artifact.grid";
    grid.params["path"] = path;
    grid.params["maxNodataFraction"] = 0.5;
    VerificationSpec spec;
    spec.specId = "s";
    spec.scope = "node";
    spec.checks.push_back( grid );
    const VerificationContext context{ &artifactProbe, &probe, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, context );
    if ( report.overall != VerificationStatus::Pass ) { std::cout << "engine not pass\n"; return 2; }
    if ( !report.checks[0].evidence.has_value() ) { std::cout << "no evidence\n"; return 2; }
    const Json::Value canonical = report.checks[0].evidence->toCanonicalJson();
    const bool stamped = canonical.isMember( "hasSampling" ) && canonical["hasSampling"].asBool()
                         && canonical.isMember( "sampledPoints" )
                         && canonical["sampledPoints"].asUInt64() == std::size_t( 9 ) * 32 * 32;
    std::cout << ( stamped ? "SAMPLING-STAMPED\n" : "NOT-STAMPED (RED on this tree)\n" );
    return stamped ? 0 : 1;
}
