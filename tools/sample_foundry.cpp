// sample_foundry.cpp — implementation of the deterministic foundry (D1).
//
// Layout of this TU:
//   1. deterministic kernels (class map, DEM surface, change model, noise)
//   2. small FS/hash helpers
//   3. GDAL raster emission (GTiff: DEFLATE, tiled, PAM off)
//   4. ROI derivation from the class map + shapefile emission
//   5. manifest write/verify (ADR 0134 semantics: SHA-256 over the payload
//      with the self-reference field removed)
//   6. typed spec intake
//   7. generate()/verifyDirectory() orchestration
//
// Ban list honored in every emit path: no std::rand, no std::distributions,
// no unordered containers, no wall clock, no locale-sensitive formatting
// (wavelengths are integers in nm), no transcendentals except atan/atan2 for
// the slope/aspect truth. Noise draw order is selection-independent: the
// optical stream always consumes its full pixel/band sequence, and the change
// stream always draws before-noise before after-noise, so one product's bytes
// never depend on which other products were requested.

#include "sample_foundry.h"

#include "sha256.h"

#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_string.h>
#include <ogr_geometry.h>
#include <ogr_feature.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace sicnu::foundry
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// 1. deterministic kernels
// ---------------------------------------------------------------------------

uint8_t classifyPixelImpl( double nx, double ny )
{
  if ( ny > 0.85 )
    return 1; // water
  if ( nx < 0.4 && ny < 0.3 )
    return 3; // urban
  const double dx = nx - 0.7;
  const double dy = ny - 0.5;
  if ( dx * dx + dy * dy < 0.04 )
    return 4; // bare soil
  if ( nx > 0.5 && ny > 0.3 && ny < 0.7 )
    return 5; // forest
  const double sx = nx - 0.3;
  const double sy = ny - 0.6;
  if ( sx * sx + sy * sy < 0.01 )
    return 6; // shadow
  return 2;   // vegetation
}

// Smooth compact polynomial bump: A * t^3 with t = 1 - q/R2 when q < R2, else
// 0. Only +,-,*,/ — bit-identical under IEEE 754 with FP contraction disabled,
// so the closed-form gradient below differentiates exactly this surface.
struct Bump
{
  double x0, y0, amplitude, r2;
  double value( double nx, double ny ) const
  {
    const double q = ( nx - x0 ) * ( nx - x0 ) + ( ny - y0 ) * ( ny - y0 );
    if ( q >= r2 )
      return 0.0;
    const double t = 1.0 - q / r2;
    return amplitude * t * t * t;
  }
  double dnx( double nx, double ny ) const
  {
    const double dx = nx - x0;
    const double q = dx * dx + ( ny - y0 ) * ( ny - y0 );
    if ( q >= r2 )
      return 0.0;
    const double t = 1.0 - q / r2;
    return -6.0 * amplitude * dx * t * t / r2;
  }
  double dny( double nx, double ny ) const
  {
    const double dy = ny - y0;
    const double q = ( nx - x0 ) * ( nx - x0 ) + dy * dy;
    if ( q >= r2 )
      return 0.0;
    const double t = 1.0 - q / r2;
    return -6.0 * amplitude * dy * t * t / r2;
  }
};

const Bump kBumps[] = {
  { 0.3, 0.4, 150.0, 0.0225 }, // western hill
  { 0.7, 0.6, 100.0, 0.04 },   // southern hill
  { 0.5, 0.5, -80.0, 0.0144 }, // central valley
};

double demElevationImpl( double nx, double ny )
{
  double z = 100.0 + 200.0 * ny + 8.0 * ( nx - 0.5 ) * ( ny - 0.5 );
  for ( const Bump &b : kBumps )
    z += b.value( nx, ny );
  return z;
}

void demGradientImpl( double nx, double ny, double *dz_dnx, double *dz_dny )
{
  double gx = 8.0 * ( ny - 0.5 );
  double gy = 200.0 + 8.0 * ( nx - 0.5 );
  for ( const Bump &b : kBumps )
  {
    gx += b.dnx( nx, ny );
    gy += b.dny( nx, ny );
  }
  *dz_dnx = gx;
  *dz_dny = gy;
}

bool changePixelImpl( double nx, double ny )
{
  const double dx = nx - 0.4;
  const double dy = ny - 0.5;
  return dx * dx + dy * dy < 0.03;
}

double changeBeforeImpl( double nx, double ny )
{
  return 0.40 + 0.40 * ( nx - 0.5 ) * ( ny - 0.5 );
}

double changeAfterImpl( double nx, double ny, bool changed )
{
  if ( changed )
    return 0.15 + 0.03 * ( nx - 0.4 );
  return changeBeforeImpl( nx, ny ) + 0.01 * ( 2.0 * nx - 1.0 );
}

/// Portable noise transform of one mt19937 raw draw into [-sigma, sigma).
double uniformNoiseValue( uint32_t raw, double sigma )
{
  return ( static_cast<double>( raw ) - 2147483648.0 ) * ( 1.0 / 2147483648.0 ) * sigma;
}

/// Deterministic mt19937 stream + portable transforms. raw() is standardized
/// across platforms; everything above it is done by hand (DECISIONS D-003).
class DetRng
{
  public:
    explicit DetRng( uint32_t seed ) : engine_( seed ) {}
    uint32_t raw() { return engine_(); }
    double noise( double sigma ) { return uniformNoiseValue( engine_(), sigma ); }

  private:
    std::mt19937 engine_;
};

void clamp01( double *v )
{
  if ( *v < 0.0 )
    *v = 0.0;
  else if ( *v > 1.0 )
    *v = 1.0;
}

void slopeAspectDegreesImpl( double dz_dnx, double dz_dny, const GridSpec &grid,
                             double *slope_deg, double *aspect_deg )
{
  // ny grows southward, so the northward gradient flips sign. sqrt (not
  // hypot) keeps the magnitude exactly-rounded IEEE. Results are quantized to
  // 0.01 degrees so the truth layers carry a stable, gradeable grid.
  const double g_east = dz_dnx / ( grid.width * grid.pixel_size_m );
  const double g_north = -dz_dny / ( grid.height * grid.pixel_size_m );
  const double slope = std::atan( std::sqrt( g_east * g_east + g_north * g_north ) ) *
                       ( 180.0 / kPi );
  *slope_deg = static_cast<double>( std::llround( slope * 100.0 ) ) / 100.0;
  if ( g_east == 0.0 && g_north == 0.0 )
  {
    *aspect_deg = kAspectNoData;
    return;
  }
  double aspect = std::atan2( -g_east, -g_north ) * ( 180.0 / kPi );
  if ( aspect < 0.0 )
    aspect += 360.0;
  *aspect_deg = static_cast<double>( std::llround( aspect * 100.0 ) ) / 100.0;
}

// ---------------------------------------------------------------------------
// 2. outcome / FS / hash helpers
// ---------------------------------------------------------------------------

Outcome okOut() { return Outcome{ true, {} }; }

Outcome fail( const char *category, const std::string &message )
{
  return Outcome{ false, Error{ category, message } };
}

Outcome gdalFail( const std::string &what )
{
  const char *last = CPLGetLastErrorMsg();
  return fail( "gdal", what + ( last && *last ? ( ": " + std::string( last ) ) : std::string() ) );
}

bool readFileBytes( const fs::path &path, std::string *out, std::string *err )
{
  std::ifstream in( path, std::ios::binary );
  if ( !in )
  {
    *err = "cannot open " + path.string() + " for reading";
    return false;
  }
  // Heap buffer: a 1 MiB stack frame would blow MSVC's default 1 MiB stack
  // reserve on the first hashed file (P0 from the adversarial review).
  std::vector<char> buf( 1 << 16 );
  while ( in )
  {
    in.read( buf.data(), static_cast<std::streamsize>( buf.size() ) );
    out->append( buf.data(), static_cast<std::size_t>( in.gcount() ) );
  }
  if ( in.bad() )
  {
    *err = "read failed on " + path.string();
    return false;
  }
  return true;
}

bool hashFile( const fs::path &path, uint64_t *bytes, std::string *hex, std::string *err )
{
  std::string content;
  if ( !readFileBytes( path, &content, err ) )
    return false;
  *bytes = content.size();
  *hex = sha256Hex( content.data(), content.size() );
  return true;
}

template <typename T>
bool contains( const std::vector<T> &v, T value )
{
  return std::find( v.begin(), v.end(), value ) != v.end();
}

// ---------------------------------------------------------------------------
// 3. GDAL raster emission
// ---------------------------------------------------------------------------

struct RasterSpec
{
  int bands = 1;
  bool byte_type = false;
  double nodata = 0.0;
  bool set_nodata = true;
  const char *class_names_csv = nullptr; // SICNU_CLASS_NAMES on band 1
  int class_count = 0;                   // SICNU_CLASS_COUNT on band 1
};

struct BandMeta
{
  const char *role = nullptr;     // SICNU_BAND_ROLE (optical only)
  const char *wavelength_nm = nullptr;
  const char *fwhm_nm = nullptr;
};

Outcome writeRaster( const GridSpec &grid, const std::string &out_dir,
                     const char *product, uint32_t seed, Profile profile,
                     const RasterSpec &spec, const std::vector<BandMeta> &bands,
                     const std::vector<float> &float_bands,
                     const std::vector<uint8_t> &byte_bands )
{
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
  if ( !driver )
    return fail( "gdal", "GTiff driver unavailable" );

  char **options = CSLSetNameValue( nullptr, "COMPRESS", "DEFLATE" );
  options = CSLSetNameValue( options, "TILED", "YES" );
  options = CSLSetNameValue( options, "BLOCKXSIZE", "256" );
  options = CSLSetNameValue( options, "BLOCKYSIZE", "256" );
  // ADR 0164 determinism: single-threaded DEFLATE keeps the emitted bytes
  // independent of the host (NUM_THREADS is also pinned in generate() so a
  // stray GDAL_NUM_THREADS cannot change them). GDAL_PAM_ENABLED=NO keeps
  // .aux.xml sidecars away.
  options = CSLSetNameValue( options, "NUM_THREADS", "1" );
  // COG-friendly layout (epic default #3); PREDICTOR 2 for Byte, 3 for
  // Float32.
  options = CSLSetNameValue( options, "PREDICTOR", spec.byte_type ? "2" : "3" );

  const std::string path = ( fs::path( out_dir ) / product ).string() + ".tif";
  GDALDataset *ds = driver->Create( path.c_str(), grid.width, grid.height, spec.bands,
                                    spec.byte_type ? GDT_Byte : GDT_Float32, options );
  CSLDestroy( options );
  if ( !ds )
    return gdalFail( "cannot create " + path );

  const double geotransform[6] = {
    grid.origin_easting, grid.pixel_size_m, 0.0,
    grid.origin_northing, 0.0, -grid.pixel_size_m,
  };
  if ( ds->SetGeoTransform( const_cast<double *>( geotransform ) ) != CE_None )
  {
    GDALClose( ds );
    return gdalFail( "cannot set geotransform on " + path );
  }

  OGRSpatialReference srs;
  if ( srs.importFromEPSG( 32648 ) != OGRERR_NONE )
  {
    GDALClose( ds );
    return gdalFail( "cannot import EPSG:32648 (PROJ database unavailable?)" );
  }
  char *wkt = nullptr;
  if ( srs.exportToWkt( &wkt ) != OGRERR_NONE || !wkt )
  {
    GDALClose( ds );
    return gdalFail( "cannot export EPSG:32648 WKT" );
  }
  const bool projected = ds->SetProjection( wkt ) == CE_None;
  CPLFree( wkt );
  if ( !projected )
  {
    GDALClose( ds );
    return gdalFail( "cannot set projection on " + path );
  }

  ds->SetMetadataItem( "SICNU_GENERATOR", kGeneratorName );
  ds->SetMetadataItem( "SICNU_GENERATOR_VERSION", kGeneratorVersion );
  ds->SetMetadataItem( "SICNU_SEED", std::to_string( seed ).c_str() );
  ds->SetMetadataItem( "SICNU_PROFILE", profileName( profile ) );
  ds->SetMetadataItem( "SICNU_PRODUCT", product );

  const std::size_t plane = static_cast<std::size_t>( grid.width ) * grid.height;
  for ( int b = 0; b < spec.bands; ++b )
  {
    GDALRasterBand *band = ds->GetRasterBand( b + 1 );
    if ( spec.set_nodata )
      band->SetNoDataValue( spec.nodata );
    if ( spec.class_names_csv && b == 0 )
    {
      band->SetMetadataItem( "SICNU_CLASS_NAMES", spec.class_names_csv );
      band->SetMetadataItem( "SICNU_CLASS_COUNT", std::to_string( spec.class_count ).c_str() );
    }
    if ( !bands.empty() )
    {
      const BandMeta &meta = bands[static_cast<std::size_t>( b )];
      if ( meta.role )
        band->SetMetadataItem( "SICNU_BAND_ROLE", meta.role );
      if ( meta.wavelength_nm )
      {
        band->SetMetadataItem( "WAVELENGTH", meta.wavelength_nm );
        band->SetMetadataItem( "WAVELENGTH_UNITS", "nm" );
      }
      if ( meta.fwhm_nm )
        band->SetMetadataItem( "FWHM", meta.fwhm_nm );
    }
    const void *src = spec.byte_type
                        ? static_cast<const void *>( byte_bands.data() + b * plane )
                        : static_cast<const void *>( float_bands.data() + b * plane );
    if ( band->RasterIO( GF_Write, 0, 0, grid.width, grid.height,
                         const_cast<void *>( src ), grid.width, grid.height,
                         spec.byte_type ? GDT_Byte : GDT_Float32, 0, 0 ) != CE_None )
    {
      GDALClose( ds );
      return gdalFail( std::string( "band write failed for " ) + path );
    }
  }
  // A failed flush at close (disk full, quota) would otherwise bless a
  // truncated TIFF with a self-consistent manifest.
  if ( GDALClose( ds ) != CE_None )
    return gdalFail( "close failed (flush?) on " + path );
  return okOut();
}

// ---------------------------------------------------------------------------
// 4. ROI derivation + shapefile
// ---------------------------------------------------------------------------

struct RoiRect
{
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // inclusive pixel bounds
};

/// Summed-area table over the indicator of one class: O(1) all-class
/// rectangle queries for the ROI scan.
class ClassSummedArea
{
  public:
    ClassSummedArea( const std::vector<uint8_t> &cls, int width, int height, uint8_t klass )
      : width_( width )
    {
      sum_.assign( static_cast<std::size_t>( width + 1 ) * ( height + 1 ), 0 );
      for ( int y = 0; y < height; ++y )
      {
        uint32_t row_acc = 0;
        for ( int x = 0; x < width; ++x )
        {
          row_acc += cls[static_cast<std::size_t>( y ) * width + x] == klass ? 1u : 0u;
          sum_[idx( y + 1, x + 1 )] = sum_[idx( y, x + 1 )] + row_acc;
        }
      }
    }
    /// All pixels in the w x h rectangle at (x, y) belong to the class?
    bool rectIsClass( int x, int y, int w, int h ) const
    {
      const uint32_t area = sum_[idx( y + h, x + w )] - sum_[idx( y, x + w )] -
                            sum_[idx( y + h, x )] + sum_[idx( y, x )];
      return area == static_cast<uint32_t>( w ) * static_cast<uint32_t>( h );
    }

  private:
    std::size_t idx( int y, int x ) const
    {
      return static_cast<std::size_t>( y ) * ( width_ + 1 ) + x;
    }
    int width_;
    std::vector<uint32_t> sum_;
};

/// First (row-major scan order) all-class rectangle with both sides >=
/// min_side, grown greedily, then shrunk by a 2 px inset so ROI edges sit
/// strictly inside the class region. Deterministic by construction.
bool deriveClassRoi( const std::vector<uint8_t> &cls, const GridSpec &grid,
                     uint8_t klass, int min_side, RoiRect *out )
{
  if ( grid.width < min_side + 4 || grid.height < min_side + 4 )
    return false;
  ClassSummedArea sat( cls, grid.width, grid.height, klass );
  for ( int y = 0; y + min_side <= grid.height; ++y )
  {
    for ( int x = 0; x + min_side <= grid.width; ++x )
    {
      if ( !sat.rectIsClass( x, y, min_side, min_side ) )
        continue;
      int w = min_side;
      while ( x + w + 1 <= grid.width && sat.rectIsClass( x, y, w + 1, min_side ) )
        ++w;
      int h = min_side;
      while ( y + h + 1 <= grid.height && sat.rectIsClass( x, y, w, h + 1 ) )
        ++h;
      *out = RoiRect{ x + 2, y + 2, x + w - 3, y + h - 3 };
      return true;
    }
  }
  return false;
}

Outcome writeTrainingShapefile( const std::vector<uint8_t> &cls, const GridSpec &grid,
                                const std::string &out_dir,
                                std::vector<std::string> *emitted )
{
  const int min_side = std::max( 12, grid.width / 16 );

  // Class-id ascending scan order keeps the feature order deterministic.
  std::map<uint8_t, RoiRect> rois;
  for ( uint8_t klass = 1; klass <= 6; ++klass )
  {
    RoiRect rect;
    if ( !deriveClassRoi( cls, grid, klass, min_side, &rect ) )
      return fail( "io", std::string( "no coherent ROI region found for class " ) +
                           std::to_string( klass ) );
    rois[klass] = rect;
  }

  const std::string shp_path = ( fs::path( out_dir ) / "training_samples.shp" ).string();
  // A second generate into a non-empty out_dir must REPLACE the shapefile:
  // the driver refuses to create over an existing dataset, so every
  // foundry-owned sidecar (including a stale .cpg, which SHAPE_ENCODING could
  // otherwise have produced) is removed first. Nothing else is touched.
  std::error_code rm_ec;
  for ( const char *ext : { ".shp", ".shx", ".dbf", ".prj", ".cpg" } )
    fs::remove( fs::path( out_dir ) / ( "training_samples" + std::string( ext ) ), rm_ec );
  GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "ESRI Shapefile" );
  if ( !driver )
    return fail( "gdal", "ESRI Shapefile driver unavailable" );
  GDALDataset *ds = driver->Create( shp_path.c_str(), 0, 0, 0, GDT_Unknown, nullptr );
  if ( !ds )
    return gdalFail( "cannot create " + shp_path );

  OGRSpatialReference srs;
  srs.importFromEPSG( 32648 );
  // The Shapefile driver derives the layer name from the file base name; the
  // argument below is ignored but kept consistent with it.
  OGRLayer *layer = ds->CreateLayer( "training_samples", &srs, wkbPolygon, nullptr );
  if ( !layer )
  {
    GDALClose( ds );
    return gdalFail( "cannot create training_samples layer" );
  }
  OGRFieldDefn name_field( "class_name", OFTString );
  name_field.SetWidth( 24 );
  if ( layer->CreateField( &name_field ) != OGRERR_NONE )
  {
    GDALClose( ds );
    return gdalFail( "cannot create class_name field" );
  }
  OGRFieldDefn id_field( "class_id", OFTInteger );
  if ( layer->CreateField( &id_field ) != OGRERR_NONE )
  {
    GDALClose( ds );
    return gdalFail( "cannot create class_id field" );
  }

  static const char *kNames[7] = {
    "", "water", "vegetation", "urban", "bare_soil", "forest", "shadow",
  };

  for ( const auto &entry : rois )
  {
    const uint8_t klass = entry.first;
    const RoiRect &rect = entry.second;
    const double min_east = grid.origin_easting + rect.x0 * grid.pixel_size_m;
    const double max_east = grid.origin_easting + ( rect.x1 + 1 ) * grid.pixel_size_m;
    const double max_north = grid.origin_northing - rect.y0 * grid.pixel_size_m;
    const double min_north = grid.origin_northing - ( rect.y1 + 1 ) * grid.pixel_size_m;

    OGRPolygon polygon;
    OGRLinearRing ring;
    ring.addPoint( min_east, max_north );
    ring.addPoint( max_east, max_north );
    ring.addPoint( max_east, min_north );
    ring.addPoint( min_east, min_north );
    ring.addPoint( min_east, max_north );
    polygon.addRing( &ring );

    OGRFeature *feature = OGRFeature::CreateFeature( layer->GetLayerDefn() );
    feature->SetField( "class_name", kNames[klass] );
    feature->SetField( "class_id", static_cast<int>( klass ) );
    feature->SetGeometry( &polygon );
    const bool created = layer->CreateFeature( feature ) == OGRERR_NONE;
    OGRFeature::DestroyFeature( feature );
    if ( !created )
    {
      GDALClose( ds );
      return gdalFail( "cannot write training feature" );
    }
  }
  if ( GDALClose( ds ) != CE_None )
    return gdalFail( "close failed (flush?) on " + shp_path );

  // ESRI Shapefile DBF headers embed the creation date, which would make the
  // output time-dependent; pin the 3 date bytes to 2000-01-01 (D-003).
  const std::string dbf_path = ( fs::path( out_dir ) / "training_samples.dbf" ).string();
  {
    std::fstream dbf( dbf_path, std::ios::in | std::ios::out | std::ios::binary );
    if ( !dbf )
      return fail( "io", "cannot reopen " + dbf_path + " to pin the DBF date" );
    char date[3] = {};
    dbf.seekg( 1 );
    dbf.read( date, 3 );
    if ( dbf.gcount() != 3 )
      return fail( "io", "short DBF header on " + dbf_path );
    const char pinned[3] = { static_cast<char>( 100 ), 1, 1 }; // 2000-01-01
    if ( std::memcmp( date, pinned, 3 ) != 0 )
    {
      dbf.seekp( 1 );
      dbf.write( pinned, 3 );
      if ( !dbf )
        return fail( "io", "cannot pin DBF date on " + dbf_path );
    }
  }

  for ( const char *ext : { ".shp", ".shx", ".dbf", ".prj" } )
  {
    const std::string name = "training_samples" + std::string( ext );
    if ( !fs::exists( fs::path( out_dir ) / name ) )
      return fail( "io", "shapefile sidecar missing after write: " + name );
    emitted->push_back( name );
  }
  // No .cpg: SHAPE_ENCODING is cleared in generate(), so the driver can never
  // emit an encoding file whose presence would vary with the host environment.
  return okOut();
}

// ---------------------------------------------------------------------------
// 5. manifest
// ---------------------------------------------------------------------------

std::string jsonDumps( const Json::Value &value )
{
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  builder["commentStyle"] = "None";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  std::ostringstream out;
  writer->write( value, &out );
  return out.str();
}

Outcome parseJsonStrict( const std::string &bytes, const std::string &label, Json::Value *root )
{
  Json::CharReaderBuilder builder;
  builder["collectComments"] = false;
  builder["failIfExtra"] = true;
  builder["rejectDupKeys"] = true;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
  std::string errs;
  if ( !reader->parse( bytes.data(), bytes.data() + bytes.size(), root, &errs ) )
    return fail( "spec", label + ": JSON parse error: " + errs );
  return okOut();
}

Json::Value manifestFilesJson( const std::vector<EmittedFile> &files )
{
  Json::Value arr( Json::arrayValue );
  for ( const EmittedFile &f : files )
  {
    Json::Value entry( Json::objectValue );
    entry["name"] = f.name;
    entry["sha256"] = f.sha256;
    entry["bytes"] = Json::UInt64( f.bytes );
    entry["product"] = f.product;
    arr.append( entry );
  }
  return arr;
}

Json::Value manifestRoot( const Options &options, const std::vector<Product> &selection,
                          const std::vector<EmittedFile> &files )
{
  const GridSpec grid = gridForProfile( options.profile );
  Json::Value root( Json::objectValue );
  root["manifest_version"] = kManifestVersion;
  root["generator"] = kGeneratorName;
  root["generator_version"] = kGeneratorVersion;
  root["seed"] = options.seed;
  root["profile"] = profileName( options.profile );
  Json::Value grid_json( Json::objectValue );
  grid_json["width"] = grid.width;
  grid_json["height"] = grid.height;
  grid_json["pixel_size_m"] = static_cast<Json::Int>( grid.pixel_size_m );
  grid_json["origin_easting"] = static_cast<Json::Int64>( grid.origin_easting );
  grid_json["origin_northing"] = static_cast<Json::Int64>( grid.origin_northing );
  grid_json["crs"] = kCrsEpsg;
  root["grid"] = grid_json;
  root["gdal_version"] = GDALVersionInfo( "--version" );
  Json::Value products( Json::arrayValue );
  for ( Product p : selection )
    products.append( productName( p ) );
  root["products"] = products;
  root["files"] = manifestFilesJson( files );
  return root;
}

/// ADR 0134 semantics: hash the serialized payload with the self-reference
/// field removed. jsoncpp serializes object members in sorted key order with
/// the fixed indent above, so the digest only depends on content.
std::string selfFingerprint( const Json::Value &root )
{
  Json::Value payload = root;
  payload.removeMember( "self_fingerprint" );
  const std::string canonical = jsonDumps( payload );
  return sha256Hex( canonical.data(), canonical.size() );
}

bool isDataFileExtension( const std::string &name )
{
  static const char *kExts[] = { ".tif", ".tiff", ".shp", ".dbf", ".shx", ".prj", ".cpg" };
  for ( const char *ext : kExts )
  {
    const std::size_t n = std::strlen( ext );
    if ( name.size() >= n && name.compare( name.size() - n, n, ext ) == 0 )
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 6. spec intake
// ---------------------------------------------------------------------------

Outcome parseSpecBytes( const std::string &path, const std::string &label,
                        SpecRequest *request )
{
  std::string bytes;
  std::string err;
  if ( !readFileBytes( fs::path( path ), &bytes, &err ) )
    return fail( "spec", label + ": " + err );
  Json::Value root;
  Outcome parsed = parseJsonStrict( bytes, label, &root );
  if ( !parsed.ok )
    return parsed;
  if ( !root.isObject() )
    return fail( "spec", label + ": top-level JSON object expected" );
  // Typed strictness (D-008): unknown keys are refused, never silently
  // ignored — failIfExtra only catches trailing garbage, not extra members.
  {
    static const char *kAllowed[] = { "experiment", "products" };
    for ( const std::string &member : root.getMemberNames() )
    {
      bool allowed = false;
      for ( const char *key : kAllowed )
        allowed = allowed || member == key;
      if ( !allowed )
        return fail( "spec", label + ": unknown spec key '" + member +
                               "' (allowed: experiment, products)" );
    }
  }
  const Json::Value &experiment = root["experiment"];
  if ( !experiment.isString() || experiment.asString().empty() )
    return fail( "spec", label + ": 'experiment' must be a non-empty string" );
  const Json::Value &products = root["products"];
  if ( !products.isArray() || products.empty() )
    return fail( "spec", label + ": 'products' must be a non-empty array" );

  std::string catalog;
  for ( Product p : productCatalog() )
    catalog += ( catalog.empty() ? "" : ", " ) + std::string( productName( p ) );

  for ( const Json::Value &item : products )
  {
    if ( !item.isString() )
      return fail( "spec", label + ": 'products' entries must be strings" );
    const std::string id = item.asString();
    const std::optional<Product> product = productByName( id );
    if ( !product )
      return fail( "spec", label + ": unknown product '" + id + "' (catalog: " + catalog + ")" );
    if ( contains( request->products, *product ) )
      return fail( "spec", label + ": duplicate product '" + id + "'" );
    request->products.push_back( *product );
  }
  if ( request->experiment.empty() )
    request->experiment = experiment.asString();
  else if ( request->experiment != experiment.asString() )
    return fail( "spec", "experiment mismatch between specs: '" + request->experiment +
                           "' vs '" + experiment.asString() + "' (" + label + ")" );
  return okOut();
}

// ---------------------------------------------------------------------------
// 7. generate() group emitters
// ---------------------------------------------------------------------------

struct FileSink
{
  std::string out_dir;
  std::vector<EmittedFile> files;

  Outcome record( const std::string &name, Product product )
  {
    EmittedFile entry;
    entry.name = name;
    entry.product = productName( product );
    std::string err;
    if ( !hashFile( fs::path( out_dir ) / name, &entry.bytes, &entry.sha256, &err ) )
      return fail( "io", err );
    files.push_back( std::move( entry ) );
    return okOut();
  }
};

Outcome emitOpticalGroup( const Options &options, const std::vector<Product> &selection,
                          const std::vector<uint8_t> &cls, const GridSpec &grid,
                          FileSink *sink )
{
  const bool want_sample = contains( selection, Product::LandsatSample );
  const bool want_truth = contains( selection, Product::LandsatTruth );
  if ( !want_sample && !want_truth )
    return okOut();

  const int w = grid.width;
  const int h = grid.height;
  const std::size_t plane = static_cast<std::size_t>( w ) * h;

  // Reflectance signatures per class id 1..6 (preserved from the original
  // generator; they encode the docs/labs spectral behaviour table).
  static const double kSignatures[6][7] = {
    { 0.05, 0.06, 0.08, 0.04, 0.02, 0.01, 0.005 }, // water
    { 0.04, 0.05, 0.08, 0.04, 0.45, 0.15, 0.08 },  // vegetation
    { 0.12, 0.13, 0.15, 0.17, 0.20, 0.25, 0.22 },  // urban
    { 0.15, 0.18, 0.22, 0.30, 0.35, 0.40, 0.38 },  // bare soil
    { 0.03, 0.04, 0.06, 0.03, 0.50, 0.12, 0.06 },  // forest
    { 0.01, 0.01, 0.02, 0.01, 0.01, 0.01, 0.005 }, // shadow
  };

  DetRng rng( options.seed );
  std::vector<float> optical( 7 * plane ); // allocated even if only truth is
  std::vector<uint8_t> truth;              // requested: the draw sequence must
  if ( want_truth )                        // not depend on the selection set.
    truth.assign( plane, 0 );

  for ( int y = 0; y < h; ++y )
  {
    for ( int x = 0; x < w; ++x )
    {
      const std::size_t base = static_cast<std::size_t>( y ) * w + x;
      const uint8_t klass = cls[base];
      if ( want_truth )
        truth[base] = klass;
      const double *sig = kSignatures[klass - 1];
      for ( int b = 0; b < 7; ++b ) // canonical draw order: bands innermost
      {
        double v = sig[b] + rng.noise( 0.02 );
        clamp01( &v );
        optical[b * plane + base] = static_cast<float>( v );
      }
    }
  }

  if ( want_truth )
  {
    Outcome wrote = writeRaster( grid, options.out_dir, productName( Product::LandsatTruth ),
                                 options.seed, options.profile,
                                 RasterSpec{ 1, true, 0.0, true, classNamesCsv(), 6 }, {}, {},
                                 truth );
    if ( !wrote.ok )
      return wrote;
    Outcome recorded = sink->record( std::string( productName( Product::LandsatTruth ) ) + ".tif",
                                     Product::LandsatTruth );
    if ( !recorded.ok )
      return recorded;
  }

  if ( !want_sample )
    return okOut();

  static const char *kRoles[7] = { "coastal", "blue", "green", "red", "nir", "swir1", "swir2" };
  static const char *kWaveNm[7] = { "440", "480", "560", "660", "870", "1610", "2200" };
  static const char *kFwhmNm[7] = { "20", "60", "60", "30", "30", "80", "180" };
  std::vector<BandMeta> meta( 7 );
  for ( int b = 0; b < 7; ++b )
    meta[static_cast<std::size_t>( b )] = BandMeta{ kRoles[b], kWaveNm[b], kFwhmNm[b] };

  Outcome wrote = writeRaster( grid, options.out_dir, productName( Product::LandsatSample ),
                               options.seed, options.profile,
                               RasterSpec{ 7, false, 0.0, false }, meta, optical, {} );
  if ( !wrote.ok )
    return wrote;
  return sink->record( std::string( productName( Product::LandsatSample ) ) + ".tif",
                       Product::LandsatSample );
}

Outcome emitDemGroup( const Options &options, const std::vector<Product> &selection,
                      const GridSpec &grid, FileSink *sink )
{
  const bool want_dem = contains( selection, Product::DemSample );
  const bool want_slope = contains( selection, Product::DemSlopeTruth );
  const bool want_aspect = contains( selection, Product::DemAspectTruth );
  if ( !want_dem && !want_slope && !want_aspect )
    return okOut();

  const int w = grid.width;
  const int h = grid.height;
  const std::size_t plane = static_cast<std::size_t>( w ) * h;

  std::vector<float> dem( want_dem ? plane : 0 );
  std::vector<float> slope( want_slope ? plane : 0 );
  std::vector<float> aspect( want_aspect ? plane : 0 );

  for ( int y = 0; y < h; ++y )
  {
    const double ny = ( y + 0.5 ) / h;
    for ( int x = 0; x < w; ++x )
    {
      const double nx = ( x + 0.5 ) / w;
      const std::size_t i = static_cast<std::size_t>( y ) * w + x;
      if ( want_dem )
        dem[i] = static_cast<float>( demElevation( nx, ny ) );
      if ( want_slope || want_aspect )
      {
        double dz_dnx = 0.0;
        double dz_dny = 0.0;
        demGradient( nx, ny, &dz_dnx, &dz_dny );
        double slope_deg = 0.0;
        double aspect_deg = 0.0;
        slopeAspectDegrees( dz_dnx, dz_dny, grid, &slope_deg, &aspect_deg );
        if ( want_slope )
          slope[i] = static_cast<float>( slope_deg );
        if ( want_aspect )
          aspect[i] = static_cast<float>( aspect_deg );
      }
    }
  }

  auto write = [&]( Product product, const std::vector<float> &buffer ) -> Outcome {
    Outcome wrote = writeRaster( grid, options.out_dir, productName( product ),
                                 options.seed, options.profile,
                                 RasterSpec{ 1, false, -9999.0, true }, {}, buffer, {} );
    if ( !wrote.ok )
      return wrote;
    return sink->record( std::string( productName( product ) ) + ".tif", product );
  };

  if ( want_dem )
  {
    Outcome wrote = write( Product::DemSample, dem );
    if ( !wrote.ok )
      return wrote;
  }
  if ( want_slope )
  {
    Outcome wrote = write( Product::DemSlopeTruth, slope );
    if ( !wrote.ok )
      return wrote;
  }
  if ( want_aspect )
  {
    Outcome wrote = write( Product::DemAspectTruth, aspect );
    if ( !wrote.ok )
      return wrote;
  }
  return okOut();
}

Outcome emitChangeGroup( const Options &options, const std::vector<Product> &selection,
                         const GridSpec &grid, FileSink *sink )
{
  const bool want_before = contains( selection, Product::ChangeBefore );
  const bool want_after = contains( selection, Product::ChangeAfter );
  const bool want_truth = contains( selection, Product::ChangeTruth );
  if ( !want_before && !want_after && !want_truth )
    return okOut();

  const int w = grid.width;
  const int h = grid.height;
  const std::size_t plane = static_cast<std::size_t>( w ) * h;

  std::vector<float> before( plane );
  std::vector<float> after( plane );
  std::vector<uint8_t> truth;
  if ( want_truth )
    truth.assign( plane, 0 );

  DetRng rng( options.seed );
  for ( int y = 0; y < h; ++y )
  {
    const double ny = ( y + 0.5 ) / h;
    for ( int x = 0; x < w; ++x )
    {
      const double nx = ( x + 0.5 ) / w;
      const bool changed = changePixel( nx, ny );
      if ( want_truth )
        truth[static_cast<std::size_t>( y ) * w + x] = changed ? 1 : 0;
      if ( !want_before && !want_after )
        continue;
      const std::size_t i = static_cast<std::size_t>( y ) * w + x;
      // Canonical draw order: before-noise then after-noise, ALWAYS both, so
      // each raster's bytes are independent of the requested selection.
      double b = changeBefore( nx, ny ) + rng.noise( 0.02 );
      clamp01( &b );
      double a = changeAfter( nx, ny, changed ) + rng.noise( 0.02 );
      clamp01( &a );
      if ( want_before )
        before[i] = static_cast<float>( b );
      if ( want_after )
        after[i] = static_cast<float>( a );
    }
  }

  auto write = [&]( Product product, const std::vector<float> &buffer ) -> Outcome {
    Outcome wrote = writeRaster( grid, options.out_dir, productName( product ),
                                 options.seed, options.profile,
                                 RasterSpec{ 1, false, -9999.0, true }, {}, buffer, {} );
    if ( !wrote.ok )
      return wrote;
    return sink->record( std::string( productName( product ) ) + ".tif", product );
  };
  if ( want_before )
  {
    Outcome wrote = write( Product::ChangeBefore, before );
    if ( !wrote.ok )
      return wrote;
  }
  if ( want_after )
  {
    Outcome wrote = write( Product::ChangeAfter, after );
    if ( !wrote.ok )
      return wrote;
  }
  if ( want_truth )
  {
    Outcome wrote = writeRaster( grid, options.out_dir, productName( Product::ChangeTruth ),
                                 options.seed, options.profile,
                                 RasterSpec{ 1, true, 255.0, true, "stable,changed", 2 }, {}, {},
                                 truth );
    if ( !wrote.ok )
      return wrote;
    Outcome recorded = sink->record( std::string( productName( Product::ChangeTruth ) ) + ".tif",
                                     Product::ChangeTruth );
    if ( !recorded.ok )
      return recorded;
  }
  return okOut();
}

} // namespace

// ---------------------------------------------------------------------------
// public API
// ---------------------------------------------------------------------------

GridSpec gridForProfile( Profile profile )
{
  GridSpec grid;
  if ( profile == Profile::Stress )
  {
    grid.width = 2048;
    grid.height = 2048;
  }
  else
  {
    grid.width = 256;
    grid.height = 256;
  }
  return grid;
}

const char *profileName( Profile profile )
{
  return profile == Profile::Stress ? "stress" : "lab";
}

bool parseProfile( const std::string &text, Profile *out )
{
  if ( text == "lab" )
  {
    *out = Profile::Lab;
    return true;
  }
  if ( text == "stress" )
  {
    *out = Profile::Stress;
    return true;
  }
  return false;
}

const std::vector<Product> &productCatalog()
{
  static const std::vector<Product> kCatalog = {
    Product::LandsatSample,  Product::DemSample,       Product::ChangeBefore,
    Product::ChangeAfter,    Product::TrainingSamples, Product::LandsatTruth,
    Product::ChangeTruth,    Product::DemSlopeTruth,   Product::DemAspectTruth,
  };
  return kCatalog;
}

const char *productName( Product product )
{
  switch ( product )
  {
    case Product::LandsatSample: return "landsat_sample";
    case Product::DemSample: return "dem_sample";
    case Product::ChangeBefore: return "change_before";
    case Product::ChangeAfter: return "change_after";
    case Product::TrainingSamples: return "training_samples";
    case Product::LandsatTruth: return "landsat_truth";
    case Product::ChangeTruth: return "change_truth";
    case Product::DemSlopeTruth: return "dem_slope_truth";
    case Product::DemAspectTruth: return "dem_aspect_truth";
  }
  return "unknown";
}

std::optional<Product> productByName( const std::string &id )
{
  for ( Product p : productCatalog() )
  {
    if ( id == productName( p ) )
      return p;
  }
  return std::nullopt;
}

uint8_t classifyPixel( double nx, double ny ) { return classifyPixelImpl( nx, ny ); }

const char *classNamesCsv()
{
  return "water,vegetation,urban,bare,forest,shadow";
}

double demElevation( double nx, double ny ) { return demElevationImpl( nx, ny ); }

void demGradient( double nx, double ny, double *dz_dnx, double *dz_dny )
{
  demGradientImpl( nx, ny, dz_dnx, dz_dny );
}

bool changePixel( double nx, double ny ) { return changePixelImpl( nx, ny ); }

double changeBefore( double nx, double ny ) { return changeBeforeImpl( nx, ny ); }

double changeAfter( double nx, double ny, bool changed )
{
  return changeAfterImpl( nx, ny, changed );
}

double uniformNoise( uint32_t raw, double sigma )
{
  return uniformNoiseValue( raw, sigma );
}

void slopeAspectDegrees( double dz_dnx, double dz_dny, const GridSpec &grid,
                         double *slope_deg, double *aspect_deg )
{
  slopeAspectDegreesImpl( dz_dnx, dz_dny, grid, slope_deg, aspect_deg );
}

Outcome loadSpec( const std::string &path, SpecRequest *request )
{
  std::error_code ec;
  if ( !fs::exists( path, ec ) )
    return fail( "spec", "spec path not found: " + path );

  if ( !fs::is_directory( path, ec ) )
    return parseSpecBytes( path, fs::path( path ).filename().string(), request );

  std::error_code iter_ec;
  fs::directory_iterator it( path, iter_ec );
  if ( iter_ec )
    return fail( "spec", "cannot list spec directory " + path + ": " + iter_ec.message() );
  std::vector<std::string> files;
  for ( fs::directory_iterator end; it != end; it.increment( iter_ec ) )
  {
    if ( iter_ec )
      return fail( "spec", "cannot list spec directory " + path + ": " + iter_ec.message() );
    if ( it->path().extension() == ".json" )
      files.push_back( it->path().string() );
  }
  std::sort( files.begin(), files.end() ); // fixed processing order
  if ( files.empty() )
    return fail( "spec", "no *.json specs in directory: " + path );
  for ( const std::string &file : files )
  {
    Outcome parsed = parseSpecBytes( file, fs::path( file ).filename().string(), request );
    if ( !parsed.ok )
      return parsed;
  }
  return okOut();
}

Outcome generate( const Options &options, GenerateResult *result )
{
  if ( result )
    *result = GenerateResult{};
  if ( options.out_dir.empty() )
    return fail( "usage", "output directory is required (--out=<dir>)" );

  std::vector<Product> selection = options.products;
  if ( selection.empty() )
    selection = productCatalog();
  // Canonical order: always the catalog order, whatever order the spec files
  // listed products in (and however they were merged across files).
  {
    const std::vector<Product> &catalog = productCatalog();
    auto position = [&catalog]( Product p ) {
      return std::find( catalog.begin(), catalog.end(), p ) - catalog.begin();
    };
    std::stable_sort( selection.begin(), selection.end(),
                      [&position]( Product a, Product b ) { return position( a ) < position( b ); } );
  }
  for ( std::size_t i = 0; i < selection.size(); ++i )
  {
    for ( std::size_t j = i + 1; j < selection.size(); ++j )
    {
      if ( selection[i] == selection[j] )
        return fail( "usage", std::string( "duplicate product in selection: " ) +
                                 productName( selection[i] ) );
    }
  }

  std::error_code ec;
  fs::create_directories( options.out_dir, ec );
  if ( !fs::is_directory( options.out_dir ) )
    return fail( "io", "cannot create output directory " + options.out_dir +
                              ( ec ? ( ": " + ec.message() ) : std::string() ) );

  // generate() writes only the current selection; a leftover catalog file from
  // a wider earlier selection would make --verify fail with "unlisted data
  // file". Known catalog basenames NOT in this selection are pruned here.
  // Nothing outside those names — user data, manifest.json included — is ever
  // touched.
  {
    std::vector<std::string> known;
    for ( Product p : productCatalog() )
      known.push_back( std::string( productName( p ) ) + ".tif" );
    for ( const char *ext : { ".shp", ".shx", ".dbf", ".prj", ".cpg" } )
      known.push_back( "training_samples" + std::string( ext ) );

    std::vector<std::string> expected;
    for ( Product p : selection )
      expected.push_back( std::string( productName( p ) ) + ".tif" );
    if ( contains( selection, Product::TrainingSamples ) )
      for ( const char *ext : { ".shp", ".shx", ".dbf", ".prj", ".cpg" } )
        expected.push_back( "training_samples" + std::string( ext ) );

    auto selected = [&expected]( const std::string &name ) {
      return std::find( expected.begin(), expected.end(), name ) != expected.end();
    };

    std::vector<std::string> stale;
    std::error_code iter_ec;
    fs::directory_iterator it( options.out_dir, iter_ec );
    if ( iter_ec )
        return fail( "io", "cannot list output directory " + options.out_dir +
                              ": " + iter_ec.message() );
    for ( fs::directory_iterator end; it != end; it.increment( iter_ec ) )
    {
      if ( iter_ec )
        return fail( "io", "cannot list output directory " + options.out_dir +
                              ": " + iter_ec.message() );
      std::error_code regular_ec;
      if ( !it->is_regular_file( regular_ec ) )
        continue;
      const std::string name = it->path().filename().string();
      if ( !contains( known, name ) || selected( name ) )
        continue;
      stale.push_back( it->path().string() );
    }
    for ( const std::string &path : stale )
    {
      std::error_code remove_ec;
      fs::remove( path, remove_ec );
    }
  }

  // ADR 0164: the emit path must not depend on the ambient GDAL environment.
  // No .aux.xml sidecars (PAM would break byte-determinism, D-012), DEFLATE is
  // forced single-threaded, and SHAPE_ENCODING is cleared so the shapefile
  // driver can never emit a host-dependent training_samples.cpg.
  CPLSetConfigOption( "GDAL_PAM_ENABLED", "NO" );
  CPLSetConfigOption( "GDAL_NUM_THREADS", "1" );
  CPLSetConfigOption( "SHAPE_ENCODING", nullptr );
  GDALAllRegister();

  const GridSpec grid = gridForProfile( options.profile );
  const std::size_t plane = static_cast<std::size_t>( grid.width ) * grid.height;

  const bool needs_class_map = contains( selection, Product::LandsatSample ) ||
                               contains( selection, Product::LandsatTruth ) ||
                               contains( selection, Product::TrainingSamples );
  std::vector<uint8_t> cls;
  if ( needs_class_map )
  {
    cls.resize( plane );
    for ( int y = 0; y < grid.height; ++y )
    {
      const double ny = ( y + 0.5 ) / grid.height;
      for ( int x = 0; x < grid.width; ++x )
      {
        const double nx = ( x + 0.5 ) / grid.width;
        cls[static_cast<std::size_t>( y ) * grid.width + x] = classifyPixel( nx, ny );
      }
    }
  }

  FileSink sink;
  sink.out_dir = options.out_dir;

  // Group emission order is fixed; the manifest sorts by name regardless.
  Outcome group = emitOpticalGroup( options, selection, cls, grid, &sink );
  if ( !group.ok )
    return group;
  group = emitDemGroup( options, selection, grid, &sink );
  if ( !group.ok )
    return group;
  group = emitChangeGroup( options, selection, grid, &sink );
  if ( !group.ok )
    return group;
  if ( contains( selection, Product::TrainingSamples ) )
  {
    std::vector<std::string> names;
    group = writeTrainingShapefile( cls, grid, options.out_dir, &names );
    if ( !group.ok )
      return group;
    for ( const std::string &name : names )
    {
      Outcome recorded = sink.record( name, Product::TrainingSamples );
      if ( !recorded.ok )
        return recorded;
    }
  }

  std::sort( sink.files.begin(), sink.files.end(),
             []( const EmittedFile &a, const EmittedFile &b ) { return a.name < b.name; } );

  Json::Value root = manifestRoot( options, selection, sink.files );
  root["self_fingerprint"] = selfFingerprint( root );

  std::string manifest_bytes = jsonDumps( root );
  manifest_bytes.push_back( '\n' );
  {
    const fs::path manifest_path = fs::path( options.out_dir ) / kManifestName;
    std::ofstream out( manifest_path, std::ios::binary | std::ios::trunc );
    if ( !out )
      return fail( "io", "cannot write " + manifest_path.string() );
    out.write( manifest_bytes.data(), static_cast<std::streamsize>( manifest_bytes.size() ) );
    out.flush();
    if ( !out )
      return fail( "io", "write failed on " + manifest_path.string() );
  }

  if ( result )
  {
    result->files = std::move( sink.files );
    result->manifest_bytes = std::move( manifest_bytes );
  }
  return okOut();
}

Outcome verifyDirectory( const std::string &out_dir, VerifyReport *report )
{
  if ( report )
    *report = VerifyReport{};
  const fs::path manifest_path = fs::path( out_dir ) / kManifestName;
  std::string bytes;
  std::string err;
  if ( !readFileBytes( manifest_path, &bytes, &err ) )
    return fail( "verify", err + " (run the generator first)" );

  Json::Value root;
  Outcome parsed = parseJsonStrict( bytes, kManifestName, &root );
  if ( !parsed.ok )
    return fail( "verify", parsed.error.message ); // report under verify, not spec
  if ( !root.isObject() )
    return fail( "verify", std::string( kManifestName ) + ": top-level JSON object expected" );

  auto addProblem = [report]( const std::string &problem ) {
    if ( report )
      report->problems.push_back( problem );
  };

  if ( !root["manifest_version"].isInt() ||
       root["manifest_version"].asInt() != kManifestVersion )
  {
    addProblem( "unsupported manifest_version" );
  }
  const Json::Value &files = root["files"];
  if ( !files.isArray() || files.empty() )
    return fail( "verify", std::string( kManifestName ) + ": 'files' array missing or empty" );

  std::map<std::string, const Json::Value *> listed;
  std::string prev_name;
  bool have_prev = false;
  for ( const Json::Value &entry : files )
  {
    if ( !entry.isObject() || !entry["name"].isString() || !entry["sha256"].isString() ||
         !entry["bytes"].isUInt64() )
    {
      return fail( "verify", "malformed file entry in " + std::string( kManifestName ) );
    }
    const std::string name = entry["name"].asString();
    if ( have_prev && !( prev_name < name ) )
      addProblem( "files not sorted by name near '" + name + "'" );
    if ( !listed.emplace( name, &entry ).second )
      addProblem( "duplicate manifest entry for '" + name + "'" );
    prev_name = name;
    have_prev = true;
  }

  // Re-hash every listed file.
  for ( const auto &item : listed )
  {
    const std::string &name = item.first;
    const Json::Value *entry = item.second;
    if ( report )
      ++report->files_checked;
    const fs::path path = fs::path( out_dir ) / name;
    std::error_code exists_ec;
    if ( !fs::exists( path, exists_ec ) )
    {
      addProblem( "missing: " + name );
      continue;
    }
    uint64_t actual_bytes = 0;
    std::string actual_hex;
    if ( !hashFile( path, &actual_bytes, &actual_hex, &err ) )
    {
      addProblem( "unreadable: " + name + " (" + err + ")" );
      continue;
    }
    if ( actual_bytes != ( *entry )["bytes"].asUInt64() )
      addProblem( "size drift: " + name );
    if ( actual_hex != ( *entry )["sha256"].asString() )
      addProblem( "sha256 drift: " + name );
  }

  // Any data file on disk that the manifest does not list is unexplained.
  std::error_code iter_ec;
  fs::directory_iterator it( out_dir, iter_ec );
  if ( iter_ec )
    return fail( "verify", "cannot list directory " + out_dir + ": " + iter_ec.message() );
  for ( fs::directory_iterator end; it != end; it.increment( iter_ec ) )
  {
    if ( iter_ec )
      return fail( "verify", "cannot list directory " + out_dir + ": " + iter_ec.message() );
    const std::string name = it->path().filename().string();
    if ( name == kManifestName || !isDataFileExtension( name ) )
      continue;
    if ( listed.find( name ) == listed.end() )
      addProblem( "unlisted data file: " + name );
  }

  // Self-integrity (ADR 0134 semantics): the recorded digest must match the
  // manifest content with the digest field removed.
  const Json::Value &recorded = root["self_fingerprint"];
  if ( !recorded.isString() )
    addProblem( "self_fingerprint missing" );
  else if ( selfFingerprint( root ) != recorded.asString() )
    addProblem( "self_fingerprint mismatch (manifest was edited after generation)" );

  if ( report )
    std::sort( report->problems.begin(), report->problems.end() );
  return okOut();
}

} // namespace sicnu::foundry
