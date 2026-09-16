// tests/lab_grading_fixture_gen.cpp — deterministic grading fixtures for the
// labspec labs (teaching-lab-platform-11).
//
// The committed corpus under tests/fixtures/lab/ follows one tradition
// (docs/labs/GRADING.md): closed-form scenes, NO RNG, NO timestamps — a
// regeneration on any machine is byte-stable, and every grading bound in the
// rules files derives from the same closed forms written here. This tool is
// the osgeo-free regeneration path (the python generator stays for hosts
// with GDAL bindings; the committed tifs are the products).
//
// Scenes (all 256x256, EPSG:4326, origin 102.5/30.5, pixel 0.001):
//   temporal  zones 1..4: evergreen NDVI 0.75 | water -0.10 | cropland
//             0.475 + 0.325*cos(2pi(m-6)/12) | disturbance = cropland
//             curve for epochs 1..8 then 0.12; 12 monthly bands, float32,
//             reflectance quantised to 1/255 (8-bit-scaled, per data-spec).
//   sar       change ground truth = one 40x40 block (1600 px of 65536 ~
//             2.4% prior); masks are Byte {0,1}.
//   hsi       3 pure vertical stripes (zones 1..3) with the SAM label
//             mapping zone -> label = zone-1.
//   carto     a valid MapSpec v5 document, a legend-less variant, and two
//             PNG exports (A4 landscape 2339x1654 px @200 dpi; A5 wrong).
//
// Usage: lab_grading_fixture_gen <out-dir>   (writes the files listed above)
#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_srs_api.h>

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <zlib.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;
constexpr int kEpochs = 12;
constexpr double kQuantum = 1.0 / 255.0; // 8-bit-scaled reflectance grid

/// Zone of a pixel for the temporal scene (deterministic layout):
/// rows 0..89    -> 1 evergreen forest
/// rows 90..179  -> 2 cropland (with an embedded disturbance block)
/// rows 180..255 -> 3 water
/// rows 100..131, cols 100..131 -> 4 disturbance patch (inside cropland)
int temporalZone( int x, int y )
{
    if ( x >= 100 && x <= 131 && y >= 100 && y <= 131 )
        return 4;
    if ( y <= 89 )
        return 1;
    if ( y <= 179 )
        return 2;
    return 3;
}

/// The closed-form class NDVI curves (BEFORE quantisation).
double classNdvi( int zone, int epoch0Based )
{
    const double month = epoch0Based + 1; // 1..12 (Jan..Dec)
    switch ( zone )
    {
        case 1:
            return 0.75;
        case 3:
            return -0.10;
        case 2:
            return 0.475 + 0.325 * std::cos( 2.0 * M_PI * ( month - 6.0 ) / 12.0 );
        case 4:
            return epoch0Based <= 7
                     ? 0.475 + 0.325 * std::cos( 2.0 * M_PI * ( month - 6.0 ) / 12.0 )
                     : 0.12;
    }
    return 0.0;
}

double quantise( double ndvi )
{
    return std::round( ndvi / kQuantum ) * kQuantum;
}

bool writeByteRaster( const QString &path, const std::vector<uint8_t> &cells )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), kWidth, kHeight,
                                       1, GDT_Byte, nullptr );
    if ( !dataset )
        return false;
    double geotransform[6] = { 102.5, 0.001, 0.0, 30.5, 0.0, -0.001 };
    GDALSetGeoTransform( dataset, geotransform );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    if ( !srs || OSRImportFromEPSG( srs, 4326 ) != OGRERR_NONE )
    {
        std::fprintf( stderr,
                      "ERROR: cannot set EPSG:4326 (PROJ database missing? set "
                      "GDAL_DATA/PROJ_LIB)\n" );
        if ( srs )
            OSRDestroySpatialReference( srs );
        GDALClose( dataset );
        return false;
    }
    char *wkt = nullptr;
    OSRExportToWkt( srs, &wkt );
    GDALSetProjection( dataset, wkt );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );
    GDALRasterBandH band = GDALGetRasterBand( dataset, 1 );
    if ( GDALRasterIO( band, GF_Write, 0, 0, kWidth, kHeight,
                       const_cast<uint8_t *>( cells.data() ), kWidth, kHeight, GDT_Byte, 0, 0 )
         != CE_None )
    {
        GDALClose( dataset );
        return false;
    }
    GDALClose( dataset );
    return true;
}

bool writeFloatSeries( const QString &path,
                       const std::vector<std::vector<float>> &bands, // [epoch][pixel]
                       const char *description )
{
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    if ( !driver )
        return false;
    char **options = nullptr;
    options = CSLSetNameValue( options, "COMPRESS", "DEFLATE" );
    GDALDatasetH dataset = GDALCreate( driver, path.toUtf8().constData(), kWidth, kHeight,
                                       static_cast<int>( bands.size() ), GDT_Float32,
                                       options );
    CSLDestroy( options );
    if ( !dataset )
        return false;
    double geotransform[6] = { 102.5, 0.001, 0.0, 30.5, 0.0, -0.001 };
    GDALSetGeoTransform( dataset, geotransform );
    OGRSpatialReferenceH srs = OSRNewSpatialReference( nullptr );
    if ( !srs || OSRImportFromEPSG( srs, 4326 ) != OGRERR_NONE )
    {
        std::fprintf( stderr,
                      "ERROR: cannot set EPSG:4326 (PROJ database missing? set "
                      "GDAL_DATA/PROJ_LIB)\n" );
        if ( srs )
            OSRDestroySpatialReference( srs );
        GDALClose( dataset );
        return false;
    }
    char *wkt = nullptr;
    OSRExportToWkt( srs, &wkt );
    GDALSetProjection( dataset, wkt );
    CPLFree( wkt );
    OSRDestroySpatialReference( srs );
    for ( int epoch = 0; epoch < static_cast<int>( bands.size() ); ++epoch )
    {
        GDALRasterBandH band = GDALGetRasterBand( dataset, epoch + 1 );
        char date[32];
        std::snprintf( date, sizeof( date ), "2024-%02d-15", epoch + 1 );
        GDALSetMetadataItem( band, "SICNU_ACQUISITION_DATE", date, nullptr );
        if ( description && epoch == 0 )
            GDALSetDescription( band, description );
        if ( GDALRasterIO( band, GF_Write, 0, 0, kWidth, kHeight,
                           const_cast<float *>( bands[static_cast<size_t>( epoch )].data() ),
                           kWidth, kHeight, GDT_Float32, 0, 0 ) != CE_None )
        {
            GDALClose( dataset );
            return false;
        }
    }
    GDALClose( dataset );
    return true;
}

// --- temporal ---------------------------------------------------------------

std::vector<uint8_t> temporalZoneCells()
{
    std::vector<uint8_t> cells( static_cast<size_t>( kWidth ) * kHeight );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
            cells[static_cast<size_t>( y ) * kWidth + x] =
              static_cast<uint8_t>( temporalZone( x, y ) );
    return cells;
}

std::vector<std::vector<float>> temporalSeries( bool seasonInverted, bool zonesSwapped )
{
    std::vector<std::vector<float>> bands(
      static_cast<size_t>( kEpochs ),
      std::vector<float>( static_cast<size_t>( kWidth ) * kHeight ) );
    for ( int epoch = 0; epoch < kEpochs; ++epoch )
    {
        for ( int y = 0; y < kHeight; ++y )
        {
            for ( int x = 0; x < kWidth; ++x )
            {
                const int zone = temporalZone( x, y );
                // zonesSwapped: the classic "wrong zone map" error — curves
                // painted for the WRONG class (1<->3, 2<->4).
                const int curveZone = zonesSwapped
                                        ? ( zone == 1 ? 3
                                          : zone == 3 ? 1
                                          : zone == 2 ? 4
                                                      : 2 )
                                        : zone;
                double ndvi = classNdvi( curveZone, epoch );
                if ( seasonInverted )
                {
                    // The classic "sign error in the seasonal term".
                    if ( curveZone == 2 || curveZone == 4 )
                    {
                        const double month = epoch + 1;
                        ndvi = 0.475 - 0.325 * std::cos( 2.0 * M_PI * ( month - 6.0 ) / 12.0 );
                    }
                }
                bands[static_cast<size_t>( epoch )][static_cast<size_t>( y ) * kWidth + x] =
                  static_cast<float>( quantise( ndvi ) );
            }
        }
    }
    return bands;
}

// --- sar / hsi masks ---------------------------------------------------------

std::vector<uint8_t> changeTruthCells( int blockOriginX, int blockOriginY )
{
    std::vector<uint8_t> cells( static_cast<size_t>( kWidth ) * kHeight, 0 );
    for ( int y = blockOriginY; y < blockOriginY + 40; ++y )
        for ( int x = blockOriginX; x < blockOriginX + 40; ++x )
            cells[static_cast<size_t>( y ) * kWidth + x] = 1;
    return cells;
}

std::vector<uint8_t> invertedCells( const std::vector<uint8_t> &src )
{
    std::vector<uint8_t> cells( src.size() );
    for ( size_t i = 0; i < src.size(); ++i )
        cells[i] = src[i] == 1 ? 0 : 1;
    return cells;
}

std::vector<uint8_t> hsiZoneCells()
{
    // Three pure vertical stripes: zones 1 (x<86), 2 (x<171), 3 (rest).
    std::vector<uint8_t> cells( static_cast<size_t>( kWidth ) * kHeight );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
            cells[static_cast<size_t>( y ) * kWidth + x] =
              static_cast<uint8_t>( x < 86 ? 1 : ( x < 171 ? 2 : 3 ) );
    return cells;
}

std::vector<uint8_t> hsiLabelsFor( const std::vector<uint8_t> &zones )
{
    // The platform's SAM label convention: label = zone - 1.
    std::vector<uint8_t> labels( zones.size() );
    for ( size_t i = 0; i < zones.size(); ++i )
        labels[i] = static_cast<uint8_t>( zones[i] - 1 );
    return labels;
}

// --- cartographic (file-mode fixtures) ---------------------------------------

bool writeText( const QString &path, const QByteArray &bytes )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    return file.write( bytes ) == bytes.size();
}

QJsonObject baseMapSpec()
{
    // Structurally identical to the shipped lab11 mapspec (MapSpec v5): page,
    // one frame, main title, legend, scale bar, north arrow, source note.
    QJsonObject spec;
    spec.insert( "schema_version", QStringLiteral( "1.0" ) );
    spec.insert( "kind", QStringLiteral( "map_spec" ) );
    spec.insert( "spec_version", 5 );
    spec.insert( "layout_name", QStringLiteral( "lab11_grading_fixture" ) );
    spec.insert( "description",
                 QStringLiteral( "Grading fixture: compliant thematic map (A4 landscape)." ) );

    QJsonObject page;
    page.insert( "width_mm", 297 );
    page.insert( "height_mm", 210 );
    spec.insert( "page", page );

    QJsonArray frames;
    QJsonObject frame;
    frame.insert( "id", QStringLiteral( "map-1" ) );
    QJsonArray rect;
    rect.append( 15 );
    rect.append( 28 );
    rect.append( 185 );
    rect.append( 160 );
    frame.insert( "rect_mm", rect );
    QJsonArray extent;
    extent.append( 102.5 );
    extent.append( 30.244 );
    extent.append( 102.756 );
    extent.append( 30.5 );
    frame.insert( "extent", extent );
    QJsonArray layers;
    layers.append( QStringLiteral( "lab11_thematic_mask" ) );
    frame.insert( "layers", layers );
    frames.append( frame );
    spec.insert( "map_frames", frames );

    QJsonArray titles;
    QJsonObject title;
    title.insert( "id", QStringLiteral( "title-1" ) );
    title.insert( "text", QStringLiteral( "Grading fixture thematic map" ) );
    title.insert( "semantic_role", QStringLiteral( "title.main" ) );
    QJsonArray titleRect;
    titleRect.append( 15 );
    titleRect.append( 8 );
    titleRect.append( 185 );
    titleRect.append( 16 );
    title.insert( "rect_mm", titleRect );
    titles.append( title );
    spec.insert( "titles", titles );

    QJsonArray legends;
    QJsonObject legend;
    legend.insert( "id", QStringLiteral( "legend-1" ) );
    legend.insert( "map_ref", QStringLiteral( "map-1" ) );
    QJsonArray legendRect;
    legendRect.append( 208 );
    legendRect.append( 30 );
    legendRect.append( 60 );
    legendRect.append( 70 );
    legend.insert( "rect_mm", legendRect );
    legends.append( legend );
    spec.insert( "legends", legends );

    QJsonArray bars;
    QJsonObject bar;
    bar.insert( "id", QStringLiteral( "scale-bar-1" ) );
    bar.insert( "map_ref", QStringLiteral( "map-1" ) );
    bars.append( bar );
    spec.insert( "scale_bars", bars );

    QJsonArray arrows;
    QJsonObject arrow;
    arrow.insert( "id", QStringLiteral( "north-arrow-1" ) );
    arrow.insert( "map_ref", QStringLiteral( "map-1" ) );
    arrows.append( arrow );
    spec.insert( "north_arrows", arrows );

    QJsonArray notes;
    QJsonObject note;
    note.insert( "id", QStringLiteral( "source-note-1" ) );
    note.insert( "text", QStringLiteral( "Data: teaching synthesis (2024); mapping: SICNU GEO RS" ) );
    QJsonArray noteRect;
    noteRect.append( 15 );
    noteRect.append( 194 );
    noteRect.append( 265 );
    noteRect.append( 8 );
    note.insert( "rect_mm", noteRect );
    notes.append( note );
    spec.insert( "source_notes", notes );

    QJsonObject output;
    QJsonArray formats;
    formats.append( QStringLiteral( "png" ) );
    output.insert( "formats", formats );
    output.insert( "dpi", 200 );
    output.insert( "dir", QStringLiteral( "fixtures/cartographic" ) );
    spec.insert( "output", output );

    spec.insert( "annotations", QJsonArray{} );
    spec.insert( "charts", QJsonArray{} );
    spec.insert( "colorbars", QJsonArray{} );
    spec.insert( "constraints", QJsonArray{} );
    spec.insert( "grids", QJsonArray{} );
    spec.insert( "inset_maps", QJsonArray{} );
    spec.insert( "labels", QJsonArray{} );
    spec.insert( "layers", QJsonArray{} );
    spec.insert( "symbols", QJsonArray{} );

    return spec;
}

bool writePng( const QString &path, int width, int height )
{
    // Hand-rolled minimal PNG (GDAL builds without the PNG driver): grayscale
    // 8-bit, filter 0 scanlines, zlib IDAT. Deterministic content.
    auto be32 = []( uint32_t v )
    {
        return std::vector<uint8_t>{ static_cast<uint8_t>( v >> 24 ),
                                     static_cast<uint8_t>( v >> 16 ),
                                     static_cast<uint8_t>( v >> 8 ),
                                     static_cast<uint8_t>( v ) };
    };
    auto chunk = [&be32]( const char *type, const std::vector<uint8_t> &data )
    {
        std::vector<uint8_t> out = be32( static_cast<uint32_t>( data.size() ) );
        out.insert( out.end(), type, type + 4 );
        out.insert( out.end(), data.begin(), data.end() );
        const uint32_t crc =
          static_cast<uint32_t>( ::crc32( 0L, out.data() + 4,
                                          static_cast<uInt>( data.size() + 4 ) ) );
        const std::vector<uint8_t> crcBytes = be32( crc );
        out.insert( out.end(), crcBytes.begin(), crcBytes.end() );
        return out;
    };

    // IHDR: width, height, bit depth 8, color type 0 (grayscale), 0/0/0.
    std::vector<uint8_t> ihdr;
    const auto w = be32( static_cast<uint32_t>( width ) );
    const auto h = be32( static_cast<uint32_t>( height ) );
    ihdr.insert( ihdr.end(), w.begin(), w.end() );
    ihdr.insert( ihdr.end(), h.begin(), h.end() );
    ihdr.push_back( 8 );  // bit depth
    ihdr.push_back( 0 );  // color type: grayscale
    ihdr.push_back( 0 );  // compression
    ihdr.push_back( 0 );  // filter
    ihdr.push_back( 0 );  // interlace

    // Raw scanlines with filter byte 0, zlib-deflated.
    std::vector<uint8_t> raw;
    raw.reserve( static_cast<size_t>( height ) * ( width + 1 ) );
    for ( int y = 0; y < height; ++y )
    {
        raw.push_back( 0 ); // filter: none
        for ( int x = 0; x < width; ++x )
            raw.push_back( static_cast<uint8_t>( ( x / 16 + y / 16 ) % 2 ? 0xAA : 0x55 ) );
    }
    uLongf compressedSize = ::compressBound( static_cast<uLong>( raw.size() ) );
    std::vector<uint8_t> idat( compressedSize );
    if ( ::compress2( idat.data(), &compressedSize, raw.data(),
                      static_cast<uLong>( raw.size() ), Z_BEST_SPEED )
         != Z_OK )
        return false;
    idat.resize( compressedSize );

    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    static const uint8_t kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if ( file.write( reinterpret_cast<const char *>( kSignature ), 8 ) != 8 )
        return false;
    const auto ihdrChunk = chunk( "IHDR", ihdr );
    const auto idatChunk = chunk( "IDAT", idat );
    const auto iendChunk = chunk( "IEND", {} );
    if ( file.write( reinterpret_cast<const char *>( ihdrChunk.data() ),
                     static_cast<qint64>( ihdrChunk.size() ) )
           != static_cast<qint64>( ihdrChunk.size() ) )
        return false;
    if ( file.write( reinterpret_cast<const char *>( idatChunk.data() ),
                     static_cast<qint64>( idatChunk.size() ) )
           != static_cast<qint64>( idatChunk.size() ) )
        return false;
    if ( file.write( reinterpret_cast<const char *>( iendChunk.data() ),
                     static_cast<qint64>( iendChunk.size() ) )
           != static_cast<qint64>( iendChunk.size() ) )
        return false;
    return true;
}

} // namespace

int main( int argc, char **argv )
{
    if ( argc != 2 )
    {
        std::fprintf( stderr, "usage: lab_grading_fixture_gen <out-dir>\n" );
        return 2;
    }
    GDALAllRegister();
    const QDir outDir( QString::fromLocal8Bit( argv[1] ) );
    if ( !outDir.exists() && !QDir().mkpath( outDir.absolutePath() ) )
    {
        std::fprintf( stderr, "cannot create %s\n", argv[1] );
        return 2;
    }

    int failures = 0;
    const auto write = [&failures]( bool ok, const char *what )
    {
        if ( !ok )
        {
            std::fprintf( stderr, "FAILED: %s\n", what );
            ++failures;
        }
        else
        {
            std::printf( "wrote %s\n", what );
        }
    };

    // temporal
    write( writeByteRaster( outDir.filePath( "temporal_zones.tif" ),
                            temporalZoneCells() ),
           "temporal_zones.tif" );
    const auto referenceSeries = temporalSeries( false, false );
    write( writeFloatSeries( outDir.filePath( "temporal_series_reference.tif" ),
                             referenceSeries, "ndvi" ),
           "temporal_series_reference.tif" );
    // wrong: one epoch dropped (band_layout must fire, blocking caps below the
    // pass line).
    auto bandCountWrong = referenceSeries;
    bandCountWrong.pop_back();
    write( writeFloatSeries( outDir.filePath( "temporal_wrong_bandcount.tif" ),
                             bandCountWrong, "ndvi" ),
           "temporal_wrong_bandcount.tif" );
    // wrong: the seasonal term sign-flipped (classic student sign error) —
    // breaks cropland + disturbance bounds in several epochs.
    write( writeFloatSeries( outDir.filePath( "temporal_wrong_season_inverted.tif" ),
                             temporalSeries( true, false ), "ndvi" ),
           "temporal_wrong_season_inverted.tif" );
    // wrong: curves painted for the WRONG classes (zone map swap) — every
    // zone-statistics assertion fires.
    write( writeFloatSeries( outDir.filePath( "temporal_wrong_zones_swapped.tif" ),
                             temporalSeries( false, true ), "ndvi" ),
           "temporal_wrong_zones_swapped.tif" );

    // sar change detection
    write( writeByteRaster( outDir.filePath( "sar_change_groundtruth.tif" ),
                            changeTruthCells( 128, 128 ) ),
           "sar_change_groundtruth.tif" );
    const auto truthCells = changeTruthCells( 128, 128 );
    write( writeByteRaster( outDir.filePath( "sar_change_reference.tif" ), truthCells ),
           "sar_change_reference.tif" );
    write( writeByteRaster( outDir.filePath( "sar_change_wrong_inverted.tif" ),
                            invertedCells( truthCells ) ),
           "sar_change_wrong_inverted.tif" );
    std::vector<uint8_t> allMarked( truthCells.size(), 1 );
    write( writeByteRaster( outDir.filePath( "sar_change_wrong_allmarked.tif" ), allMarked ),
           "sar_change_wrong_allmarked.tif" );
    write( writeByteRaster( outDir.filePath( "sar_change_wrong_shifted.tif" ),
                            changeTruthCells( 158, 128 ) ),
           "sar_change_wrong_shifted.tif" );

    // hyperspectral SAM labels
    const auto hsiZones = hsiZoneCells();
    write( writeByteRaster( outDir.filePath( "hsi_zones.tif" ), hsiZones ),
           "hsi_zones.tif" );
    write( writeByteRaster( outDir.filePath( "hsi_labels_reference.tif" ),
                            hsiLabelsFor( hsiZones ) ),
           "hsi_labels_reference.tif" );
    // wrong: every pixel labelled water (zone 1's label).
    std::vector<uint8_t> allWater( hsiZones.size(), 0 );
    write( writeByteRaster( outDir.filePath( "hsi_labels_wrong_allwater.tif" ), allWater ),
           "hsi_labels_wrong_allwater.tif" );
    // wrong: labels shifted by half a stripe (classification mismatch).
    std::vector<uint8_t> shifted( hsiZones.size() );
    for ( int y = 0; y < kHeight; ++y )
        for ( int x = 0; x < kWidth; ++x )
            shifted[static_cast<size_t>( y ) * kWidth + x] = static_cast<uint8_t>(
              hsiZoneCells()[static_cast<size_t>( y ) * kWidth + ( x + 43 ) % kWidth] - 1 );
    write( writeByteRaster( outDir.filePath( "hsi_labels_wrong_shifted.tif" ), shifted ),
           "hsi_labels_wrong_shifted.tif" );

    // cartographic (file mode): mapspec document + PNG page geometry
    write( writeText( outDir.filePath( "carto_mapspec_reference.mapspec.json" ),
                      QJsonDocument( baseMapSpec() ).toJson( QJsonDocument::Indented ) ),
           "carto_mapspec_reference.mapspec.json" );
    QJsonObject noLegend = baseMapSpec();
    noLegend.remove( "legends" );
    write( writeText( outDir.filePath( "carto_mapspec_wrong_nolegend.mapspec.json" ),
                      QJsonDocument( noLegend ).toJson( QJsonDocument::Indented ) ),
           "carto_mapspec_wrong_nolegend.mapspec.json" );
    write( writePng( outDir.filePath( "carto_export_reference.png" ), 2339, 1654 ),
           "carto_export_reference.png (A4 landscape @200dpi)" );
    write( writePng( outDir.filePath( "carto_export_wrong_a5.png" ), 1654, 1165 ),
           "carto_export_wrong_a5.png (A5 landscape @200dpi)" );

    if ( failures )
        return 1;
    return 0;
}
