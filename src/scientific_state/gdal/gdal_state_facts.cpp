/***************************************************************************
  scientific_state/gdal_state_facts.cpp
  RS14-01 Scientific Data Passport — GDAL facts collector.
 ***************************************************************************/

#include "scientific_state/gdal/gdal_state_facts.h"

#include <cpl_error.h>
#include <ogr_srs_api.h>

#include <algorithm>

namespace sicnu::state
{

namespace
{

void collectMetadataItems( CSLConstList metadata, const std::string &sourcePrefix,
                           MetadataItems &out )
{
    if ( !metadata )
        return;
    // Scan every entry: the cap is MetadataItems' contract (add() drops
    // beyond it and counts), not this loop's. Bounding iterations here
    // truncated >cap files silently while dropped() stayed 0 — the
    // "truncation is never silent" rule broke exactly for the files large
    // enough to matter.
    for ( CSLConstList entry = metadata; *entry; ++entry )
    {
        const std::string item = *entry;
        const std::size_t equals = item.find( '=' );
        if ( equals == std::string::npos || equals == 0 )
            continue;
        out.add( item.substr( 0, equals ), item.substr( equals + 1 ),
                 sourcePrefix + item.substr( 0, equals ) );
    }
}

std::string bandDataTypeName( GDALRasterBandH band )
{
    return GDALGetDataTypeName( GDALGetRasterDataType( band ) );
}

} // namespace

std::optional<DatasetFacts> collectDatasetFacts( GDALDatasetH dataset,
                                                 const std::string &sourcePath,
                                                 std::string &error )
{
    if ( !dataset )
    {
        error = "null dataset";
        return std::nullopt;
    }

    DatasetFacts facts;
    facts.sourcePath = sourcePath;
    GDALDriver *driver = GDALDataset::FromHandle( dataset )->GetDriver();
    facts.driverName = driver ? driver->GetDescription() : std::string();

    facts.metadata.setCap( kMaxCollectedMetadataItems );
    collectMetadataItems( GDALDataset::FromHandle( dataset )->GetMetadata( nullptr ),
                          "gdal:", facts.metadata );
    facts.droppedMetadataItems += facts.metadata.dropped();

    facts.bandCount = GDALDataset::FromHandle( dataset )->GetRasterCount();
    for ( int index = 1; index <= facts.bandCount; ++index )
    {
        GDALRasterBand *band = GDALDataset::FromHandle( dataset )->GetRasterBand( index );
        if ( !band )
            continue;
        BandFacts bandFacts;
        bandFacts.metadata.setCap( kMaxCollectedMetadataItems );
        bandFacts.index = index;
        const char *description = band->GetDescription();
        if ( description && *description )
            bandFacts.name = description;
        bandFacts.dataType = bandDataTypeName( band );
        collectMetadataItems( band->GetMetadata( nullptr ), "gdal:", bandFacts.metadata );

        // Native GDAL nodata declaration, projected under the GDAL term the
        // resolver keys on (see Slice C: band item "NO_DATA_VALUE"). Added
        // BEFORE the drop count is harvested so a full cap can never
        // silently swallow this item.
        int hasNoData = FALSE;
        const double noDataValue = band->GetNoDataValue( &hasNoData );
        if ( hasNoData && !bandFacts.metadata.contains( "NO_DATA_VALUE" ) )
        {
            bandFacts.metadata.add( "NO_DATA_VALUE", std::to_string( noDataValue ),
                                    "gdal:NO_DATA_VALUE" );
        }
        facts.droppedMetadataItems += bandFacts.metadata.dropped();

        facts.bands.push_back( bandFacts );
    }

    // Geometry: CRS + geotransform + raster size.
    const OGRSpatialReference *srs = GDALDataset::FromHandle( dataset )->GetSpatialRef();
    if ( srs )
    {
        facts.geometry.hasCrs = true;
        const std::string wkt = srs->exportToWkt();
        if ( !wkt.empty() )
            facts.geometry.crsWkt = wkt;
        const char *authorityName = srs->GetAuthorityName( nullptr );
        const char *authorityCode = srs->GetAuthorityCode( nullptr );
        if ( authorityName != nullptr && authorityCode != nullptr )
            facts.geometry.crsAuthid =
                std::string( authorityName ) + ":" + std::string( authorityCode );
        facts.geometry.crsGeographic = srs->IsGeographic();
        facts.geometry.crsProjected = srs->IsProjected();
    }

    double geotransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, 1.0 };
    if ( GDALDataset::FromHandle( dataset )->GetGeoTransform( geotransform ) == CE_None )
    {
        facts.geometry.hasGeoTransform = true;
        std::copy( geotransform, geotransform + 6, facts.geometry.geoTransform.begin() );
    }

    facts.geometry.width = GDALDataset::FromHandle( dataset )->GetRasterXSize();
    facts.geometry.height = GDALDataset::FromHandle( dataset )->GetRasterYSize();

    return facts;
}

std::optional<DatasetFacts> collectDatasetFacts( const std::string &path, std::string &error )
{
    GDALAllRegister();

    // Metadata-only inspection: quiet GDAL's error noise for expected
    // failures (missing files, non-raster drivers) — the typed error carries
    // the outcome.
    CPLPushErrorHandler( CPLQuietErrorHandler );
    GDALDatasetH dataset =
        GDALOpenEx( path.c_str(), GDAL_OF_RASTER | GDAL_OF_VERBOSE_ERROR, nullptr, nullptr,
                    nullptr );
    CPLPopErrorHandler();

    if ( !dataset )
    {
        error = "cannot open raster dataset: " + path;
        return std::nullopt;
    }

    std::optional<DatasetFacts> facts = collectDatasetFacts( dataset, path, error );
    GDALClose( dataset );
    if ( !facts )
        error = "cannot collect facts: " + path;
    return facts;
}

} // namespace sicnu::state
