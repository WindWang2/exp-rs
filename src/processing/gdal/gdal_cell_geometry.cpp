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

} // namespace sicnu::processing::gdal_util
