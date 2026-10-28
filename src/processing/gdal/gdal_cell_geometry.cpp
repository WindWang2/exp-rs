// gdal_cell_geometry.cpp — see gdal_cell_geometry.h
#include "gdal_cell_geometry.h"

#include "gdal_dataset_wrapper.h"
#include "processing/algorithms/math_utils.h"

#include <ogr_spatialref.h>

#include <cmath>

namespace sicnu::processing::gdal_util
{

bool isGeographicCrs( const QString &wkt )
{
    if ( wkt.isEmpty() )
        return false;
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    if ( !srs )
        return false;
    const QByteArray wktBytes = wkt.toUtf8();
    char *wktPtr = const_cast<char *>( wktBytes.constData() );
    const bool geographic =
        OSRImportFromWkt( srs, &wktPtr ) == OGRERR_NONE && OSRIsGeographic( srs ) != 0;
    OSRDestroySpatialReference( srs );
    return geographic;
}

void cellSizesMetres( const GdalDatasetWrapper &ds, double *csx, double *csy )
{
    const std::array<double, 6> gt = ds.geoTransform();
    double x = std::abs( gt[1] );
    double y = std::abs( gt[5] );
    if ( x <= 1e-7 )
        x = 30.0;
    if ( y <= 1e-7 )
        y = x;

    if ( isGeographicCrs( ds.projection() ) )
    {
        const double phiDeg = MathUtils::sceneCentreLatitudeDeg( gt, ds.height() );
        const MathUtils::Wgs84ArcMeters arc = MathUtils::wgs84ArcAtLatitudeDeg( phiDeg );
        x = std::abs( gt[1] ) * arc.perDegLon;
        y = ( std::abs( gt[5] ) > 1e-7 ? std::abs( gt[5] ) : std::abs( gt[1] ) ) * arc.perDegLat;
    }
    *csx = x;
    *csy = y;
}

std::optional<double> sceneLatitudeDeg( const GdalDatasetWrapper &ds )
{
    const QString wkt = ds.projection();
    if ( wkt.isEmpty() )
        return std::nullopt;

    const std::array<double, 6> gt = ds.geoTransform();
    if ( std::abs( gt[1] ) <= 0.0 || std::abs( gt[5] ) <= 0.0 )
        return std::nullopt;
    // The exact identity geotransform is GDAL's "no georeferencing" marker
    // (a CRS without a sane affine would silently place the scene at
    // lat = rows/2 in the geographic branch) — fail closed instead (R1-14).
    if ( gt[0] == 0.0 && gt[1] == 1.0 && gt[2] == 0.0 && gt[3] == 0.0
         && gt[4] == 0.0 && gt[5] == 1.0 )
        return std::nullopt;

    double latitude = 0.0;
    if ( isGeographicCrs( wkt ) )
    {
        // Angular CRS: the centre row's Y IS the latitude.
        latitude = MathUtils::sceneCentreLatitudeDeg( gt, ds.height() );
    }
    else
    {
        // Projected CRS: transform the raster centre to WGS84. Same
        // import/transform/release discipline as MosaicPlanner's footprint
        // envelope (src/processing/algorithms/mosaic_plan.cpp).
        //
        // The centre is the SIGNED affine midpoint (R1-2): with north-up
        // rasters gt[5] < 0, so an abs() walked half a scene ABOVE the top
        // edge and derived a latitude off by the full scene height. The
        // full form also carries the rotation terms gt[2]/gt[4].
        OGRSpatialReference src;
        const QByteArray wktBytes = wkt.toUtf8();
        if ( src.importFromWkt( wktBytes.constData() ) != OGRERR_NONE )
            return std::nullopt;
        OGRSpatialReference dst;
        if ( dst.SetFromUserInput( "EPSG:4326" ) != OGRERR_NONE )
            return std::nullopt;
        OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation( &src, &dst );
        if ( !ct )
            return std::nullopt;
        const double halfW = static_cast<double>( ds.width() ) / 2.0;
        const double halfH = static_cast<double>( ds.height() ) / 2.0;
        double x = gt[0] + gt[1] * halfW + gt[2] * halfH;
        double y = gt[3] + gt[4] * halfW + gt[5] * halfH;
        const bool ok = ct->Transform( 1, &x, &y ) != 0;
        OGRCoordinateTransformation::DestroyCT( ct );
        if ( !ok || !std::isfinite( y ) )
            return std::nullopt;
        latitude = y;
    }

    if ( !std::isfinite( latitude ) || latitude < -90.0 || latitude > 90.0 )
        return std::nullopt;
    return latitude;
}

} // namespace sicnu::processing::gdal_util
