// R20-05 R1-P2 driver: elongated raster (200x40, axis 32) has long-axis gaps
// -> must be stamped sampled even though sampled(9216) >= total(8000).
#include "verify/verify_engine.h"
#include "verify_adapters/gdal_grid_probe.h"
#include "verify_adapters/fs_artifact_probe.h"
#include <gdal.h>
#include <cpl_conv.h>
#include <iostream>
#include <vector>
using namespace sicnu::verify;
int main()
{
    const std::string path = "/tmp/r2005_red/sv2_200x40.tif";
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    GDALDatasetH ds = GDALCreate( driver, path.c_str(), 200, 40, 1, GDT_Float32, nullptr );
    double gt[6] = { 0.0, 10.0, 0.0, 0.0, 0.0, -10.0 };
    GDALSetGeoTransform( ds, gt );
    GDALRasterBandH band = GDALGetRasterBand( ds, 1 );
    std::vector<float> px( 200 * 40, 1.0f );
    GDALRasterIO( band, GF_Write, 0, 0, 200, 40, px.data(), 200, 40, GDT_Float32, 0, 0 );
    GDALClose( ds );
    sicnu::verify_adapters::GdalGridProbe probe;
    sicnu::verify_adapters::FsArtifactProbe artifactProbe;
    VerificationCheckSpec grid;
    grid.checkId = "g"; grid.kind = "artifact.grid";
    grid.params["path"] = path;
    grid.params["maxNodataFraction"] = 0.5;
    VerificationSpec spec; spec.specId = "s"; spec.scope = "node"; spec.checks.push_back( grid );
    const VerificationContext context{ &artifactProbe, &probe, nullptr, nullptr, nullptr };
    const VerificationReport report = evaluate( spec, context );
    if ( report.overall != VerificationStatus::Pass || !report.checks[0].evidence.has_value() )
    { std::cout << "engine not pass\n"; return 2; }
    const bool stamped = report.checks[0].evidence->hasSampling
                         && report.checks[0].evidence->sampledPoints == std::size_t( 9 ) * 32 * 32;
    std::cout << ( stamped ? "ELONGATED-STAMPED\n" : "ELONGATED-UNMARKED (bad)\n" );
    return stamped ? 0 : 1;
}
