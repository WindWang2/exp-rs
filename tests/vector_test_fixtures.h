// tests/vector_test_fixtures.h — runtime-generated vector fixtures.
// Replaces the sample files (test_vectors.geojson, samples/test.shp) that were
// removed from VCS: the six vector-path tests that SKIP'd on the missing
// fixtures execute against these instead (#460).
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>

#include <gdal.h>
#include <ogr_api.h>
#include <ogr_spatialref.h>

#include <cpl_conv.h>

namespace vector_test_fixtures {

/// GeoJSON equivalent of the retired data/test_vectors.geojson: a layer named
/// "test_points" with 3 WGS84 points spanning extent [5..20]^2 — the exact
/// structure the provider tests assert on.
inline QString syntheticGeoJsonPath()
{
    static QTemporaryDir dir;
    static const QString path = dir.filePath( QStringLiteral( "test_vectors.geojson" ) );
    if ( !QFile::exists( path ) )
    {
        QFile f( path );
        if ( !f.open( QIODevice::WriteOnly | QIODevice::Text ) )
            return QString();
        QTextStream s( &f );
        s << QStringLiteral(
            "{\n"
            "  \"name\": \"test_points\",\n"
            "  \"type\": \"FeatureCollection\",\n"
            "  \"features\": [\n"
            "    {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [5.0, 5.0]}, \"properties\": {}},\n"
            "    {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [20.0, 20.0]}, \"properties\": {}},\n"
            "    {\"type\": \"Feature\", \"geometry\": {\"type\": \"Point\", \"coordinates\": [12.0, 15.0]}, \"properties\": {}}\n"
            "  ]\n"
            "}\n" );
        s.flush();
        f.close();
    }
    return path;
}

/// Minimal ESRI Shapefile (one polygon layer with a class field, EPSG:4326)
/// equivalent to the retired samples/test.shp; the driver emits the full
/// .shp/.shx/.dbf/.prj sidecar set. Returns the .shp path.
inline QString syntheticShapefilePath()
{
    static QTemporaryDir dir;
    static const QString path = dir.filePath( QStringLiteral( "test.shp" ) );
    if ( !QFile::exists( path ) )
    {
        GDALAllRegister();
        OGRRegisterAll();
        GDALDriverH drv = GDALGetDriverByName( "ESRI Shapefile" );
        if ( !drv )
            return QString();
        GDALDatasetH ds = GDALCreate( drv, path.toUtf8().constData(), 0, 0, 0,
                                      GDT_Unknown, nullptr );
        if ( !ds )
            return QString();
        OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
        OSRImportFromEPSG( srs, 4326 );
        OSRSetAxisMappingStrategy( srs, OAMS_TRADITIONAL_GIS_ORDER );
        OGRLayerH layer = GDALDatasetCreateLayer( ds, "polygons", srs, wkbPolygon, nullptr );
        OSRDestroySpatialReference( srs );
        if ( !layer )
        {
            GDALClose( ds );
            return QString();
        }
        OGRFieldDefnH field = OGR_Fld_Create( "class", OFTInteger );
        OGR_L_CreateField( layer, field, TRUE );
        OGR_Fld_Destroy( field );

        OGRFeatureDefnH defn = OGR_L_GetLayerDefn( layer );
        for ( const auto &ring : { QVector<double>{ 0, 0, 4, 0, 4, 4, 0, 4, 0, 0 },
                                   QVector<double>{ 6, 6, 9, 6, 9, 9, 6, 9, 6, 6 } } )
        {
            OGRFeatureH feat = OGR_F_Create( defn );
            OGRGeometryH polygon = OGR_G_CreateGeometry( wkbPolygon );
            OGRGeometryH lr = OGR_G_CreateGeometry( wkbLinearRing );
            for ( int i = 0; i < ring.size(); i += 2 )
                OGR_G_AddPoint_2D( lr, ring[i], ring[i + 1] );
            OGR_G_AddGeometryDirectly( polygon, lr );
            OGR_F_SetGeometryDirectly( feat, polygon );
            OGR_F_SetFieldInteger( feat, OGR_F_GetFieldIndex( feat, "class" ), 1 );
            OGR_L_CreateFeature( layer, feat );
            OGR_F_Destroy( feat );
        }
        GDALClose( ds );
    }
    return path;
}

} // namespace vector_test_fixtures
