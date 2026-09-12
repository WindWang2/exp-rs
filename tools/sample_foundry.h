// sample_foundry.h — deterministic lab sample data foundry (goal D1).
//
// Emits the assets promised by docs/labs/README.md (landsat_sample.tif,
// dem_sample.tif, change_before.tif, change_after.tif, training_samples.shp)
// plus mandatory ground-truth companions, and a SHA-256 manifest for drift
// verification. Qt-free on purpose (std C++20 + GDAL + jsoncpp only, mirroring
// the src/geospatial leaf policy) so `sicnu_generate_samples` builds in every
// configure lane and ships as a standalone binary (D7 offline bundle).
//
// Determinism contract (docs/adr/0146):
//   * std::mt19937 raw output is standardized and portable; std::distributions
//     are implementation-defined and are NOT used. Noise is a portable uniform
//     transform of one raw draw.
//   * the synthetic surface / class map / change model use only +, -, *, /
//     on IEEE doubles (no libm transcendentals), compiled with FP contraction
//     off, so emitted bytes are identical across platforms and runs. Only the
//     slope/aspect truth touches atan (bit-identical per host+compiler).
//   * canonical draw order: row-major pixels; bands innermost (optical);
//     before-noise then after-noise (change pair). Truth layers draw nothing.
//   * no wall clock, locale, hash-order or thread dependence in the emit path.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::foundry
{

inline constexpr char kGeneratorName[] = "sicnu_generate_samples";
inline constexpr char kGeneratorVersion[] = "1.0.0";
inline constexpr int kManifestVersion = 1;
inline constexpr char kManifestName[] = "manifest.json";
inline constexpr char kCrsEpsg[] = "EPSG:32648"; // UTM 48N (epic default #4)

enum class Profile
{
  Lab,    // 256 x 256
  Stress, // 2048 x 2048
};

struct GridSpec
{
  int width = 0;
  int height = 0;
  double pixel_size_m = 30.0;
  double origin_easting = 500000.0;   // top-left corner
  double origin_northing = 4060000.0; // top-left corner
};

GridSpec gridForProfile( Profile profile );
const char *profileName( Profile profile );
/// Strict parse ("lab"/"stress"); false on anything else.
bool parseProfile( const std::string &text, Profile *out );

/// Fixed product catalog. Order is the canonical generation order and the
/// order used for the manifest "products" array, regardless of --spec order.
enum class Product : uint8_t
{
  LandsatSample,
  DemSample,
  ChangeBefore,
  ChangeAfter,
  TrainingSamples,
  LandsatTruth,
  ChangeTruth,
  DemSlopeTruth,
  DemAspectTruth,
};

const std::vector<Product> &productCatalog();
const char *productName( Product product );
std::optional<Product> productByName( const std::string &id );

/// Typed failure. `category` is one of "usage" | "io" | "gdal" | "spec" |
/// "verify" and maps onto the documented CLI exit codes.
struct Error
{
  std::string category;
  std::string message;
};
struct Outcome
{
  bool ok = false;
  Error error;
};

struct Options
{
  Profile profile = Profile::Lab;
  std::string out_dir;             // required
  uint32_t seed = 42;
  std::vector<Product> products;   // empty = full catalog
};

struct EmittedFile
{
  std::string name;     // base name inside out_dir
  std::string product;  // catalog id the file belongs to
  uint64_t bytes = 0;
  std::string sha256;   // lowercase hex
};

struct GenerateResult
{
  std::vector<EmittedFile> files; // sorted by name; manifest.json not included
  std::string manifest_bytes;     // exact bytes written to <out>/manifest.json
};

/// Generate the selected products + companions into out_dir and write
/// manifest.json. Overwrites foundry-owned outputs; never touches anything else.
Outcome generate( const Options &options, GenerateResult *result );

struct VerifyReport
{
  std::vector<std::string> problems; // one typed line per drift
  std::size_t files_checked = 0;
};

/// Re-check an existing directory against its manifest.json (D-002 / ADR 0146).
Outcome verifyDirectory( const std::string &out_dir, VerifyReport *report );

struct SpecRequest
{
  std::string experiment;
  std::vector<Product> products; // catalog order, deduplicated
};

/// Data-spec intake (epic package G, contract for D3). @p path is a spec file
/// or a directory of *.json (processed in sorted order). Unknown product,
/// missing path, parse error, duplicate or empty product list are typed
/// refusals — never a silent default.
Outcome loadSpec( const std::string &path, SpecRequest *request );

// ---------------------------------------------------------------------------
// Deterministic kernels, exposed for tests. Coordinates are normalized pixel
// space: nx = (x + 0.5) / width, ny = (y + 0.5) / height (y grows southward).
// ---------------------------------------------------------------------------

/// Land-cover class ids: 1 water, 2 vegetation, 3 urban, 4 bare, 5 forest,
/// 6 shadow (docs/labs table order). Seed-independent.
uint8_t classifyPixel( double nx, double ny );
/// Comma-joined class names by id, "water,vegetation,urban,bare,forest,shadow".
const char *classNamesCsv();

/// Analytic elevation surface in meters (closed form, rational arithmetic).
double demElevation( double nx, double ny );
/// Partial derivatives of demElevation w.r.t. nx and ny (ny southward).
void demGradient( double nx, double ny, double *dz_dnx, double *dz_dny );

/// Change model (seed-independent predicate; values get uniform noise).
bool changePixel( double nx, double ny );
double changeBefore( double nx, double ny );
double changeAfter( double nx, double ny, bool changed );

/// Portable noise transform of one mt19937 raw draw: ((raw - 2^31) / 2^31) * sigma.
double uniformNoise( uint32_t raw, double sigma );

/// Slope (degrees, quantized to 0.01) and aspect (degrees clockwise from
/// north, direction of steepest descent, quantized to 0.01; flat -> NoData
/// -9999) from grid-scaled gradients.
void slopeAspectDegrees( double dz_dnx, double dz_dny, const GridSpec &grid,
                         double *slope_deg, double *aspect_deg );
inline constexpr double kAspectNoData = -9999.0;

} // namespace sicnu::foundry
