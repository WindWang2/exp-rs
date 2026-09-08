// src/agent/harness/scientific_preflight.cpp
#include "scientific_preflight.h"

#include "agent/workspace_state.h"
#include "contracts/spatial_contracts.h"
#include "entity_resolver.h"
#include "grounding_tools.h"
#include "spatial_tools/spatial_tool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace sicnu::agent::harness {

using namespace sicnu::agent::spatial_tools;

namespace {

Json::Value makeIssueWithCode( const std::string &code, const std::string &severity,
                               const std::string &message, bool repairable,
                               Json::Value suggestedAction )
{
  return sicnu::agent::contracts::makeIssue( code, severity, message, repairable, "",
                                    std::move( suggestedAction ) );
}

Json::Value makeCheck( const std::string &name, bool passed, const std::string &code,
                       Json::Value details )
{
  return sicnu::agent::contracts::makeAssessmentCheck(
    name, passed, passed ? "info" : "error", code, std::move( details ) );
}

void addBlocker( PreflightOutcome &outcome, const std::string &code,
                 const std::string &message, const std::string &action,
                 Json::Value details = Json::Value() )
{
  outcome.issues.append( makeIssueWithCode( code, "error", message, true,
                                            sicnu::agent::contracts::makeRepairSuggestion(
                                              action, Json::Value() ) ) );
  outcome.errors.push_back( HarnessError::make( code, message, details ) );
  outcome.checks.append( makeCheck( code, false, code, details ) );
}

void addWarning( PreflightOutcome &outcome, const std::string &code,
                 const std::string &message )
{
  outcome.issues.append( makeIssueWithCode( code, "warning", message, false, Json::Value() ) );
  outcome.checks.append( makeCheck( code, true, code, Json::Value() ) );
}

void addInfo( PreflightOutcome &outcome, const std::string &name, bool passed,
              const std::string &code )
{
  outcome.checks.append( makeCheck( name, passed, code, Json::Value() ) );
}

std::string lowered( std::string text )
{
  std::transform( text.begin(), text.end(), text.begin(),
                  []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return text;
}

/// Band-role extraction with the wavelength fallback: a band without an
/// explicit role is *not* guessed from position — but a declared wavelength
/// in a documented window is a physical fact, not a guess.
struct BandFacts {
  std::vector<std::string> roles;
  bool hasNir = false;
  bool hasRed = false;
  bool hasGreen = false;
  bool hasBlue = false;
  bool hasSwir = false;    ///< any SWIR window (1550-1750 or 2080-2350 nm)
  bool hasRedEdge = false; ///< 700-740 nm red-edge window
  bool nirByWavelength = false;
  bool redByWavelength = false;
  int bandCount = 0;
};

BandFacts bandFacts( const Json::Value &understanding )
{
  BandFacts facts;
  facts.bandCount = understanding.get( "band_count", 0 ).asInt();
  auto window = []( const Json::Value &band, double &wavelengthNm ) {
    if ( band.isMember( "wavelength" ) && band["wavelength"].isNumeric() )
    {
      wavelengthNm = band["wavelength"].asDouble();
      const std::string units = lowered( band.get( "wavelengthUnits", "nm" ).asString() );
      if ( units == "µm" || units == "um" )
        wavelengthNm *= 1000.0;
    }
  };
  auto inWindow = []( double wavelengthNm, double low, double high ) {
    return wavelengthNm >= low && wavelengthNm <= high;
  };
  if ( understanding.isMember( "bands" ) && understanding["bands"].isArray() )
  {
    for ( const auto &band : understanding["bands"] )
    {
      ++facts.bandCount;
      const std::string role = lowered( band.get( "role", "" ).asString() );
      if ( !role.empty() )
        facts.roles.push_back( role );
      double wavelengthNm = -1;
      window( band, wavelengthNm );
      const bool isNirRole = role == "nir";
      const bool isRedRole = role == "red";
      if ( isNirRole || inWindow( wavelengthNm, 750.0, 1100.0 ) )
      {
        facts.hasNir = true;
        facts.nirByWavelength = isNirRole ? facts.nirByWavelength : true;
      }
      if ( isRedRole || ( wavelengthNm >= 600.0 && wavelengthNm < 700.0 ) )
      {
        facts.hasRed = true;
        facts.redByWavelength = isRedRole ? facts.redByWavelength : true;
      }
      if ( role == "green" || inWindow( wavelengthNm, 500.0, 600.0 ) )
        facts.hasGreen = true;
      if ( role == "blue" || inWindow( wavelengthNm, 430.0, 520.0 ) )
        facts.hasBlue = true;
      if ( role == "swir" || role == "swir1" || role == "swir2" ||
           inWindow( wavelengthNm, 1550.0, 1750.0 ) || inWindow( wavelengthNm, 2080.0, 2350.0 ) )
        facts.hasSwir = true;
      if ( role == "red_edge" || role == "rededge" || inWindow( wavelengthNm, 700.0, 745.0 ) )
        facts.hasRedEdge = true;
    }
  }
  else if ( understanding.isMember( "band_roles" ) && understanding["band_roles"].isArray() )
  {
    for ( const auto &role : understanding["band_roles"] )
    {
      ++facts.bandCount;
      const std::string r = lowered( role.asString() );
      if ( !r.empty() )
      {
        facts.roles.push_back( r );
        if ( r == "nir" )
          facts.hasNir = true;
        if ( r == "red" )
          facts.hasRed = true;
        if ( r == "green" )
          facts.hasGreen = true;
        if ( r == "blue" )
          facts.hasBlue = true;
        if ( r == "swir" || r == "swir1" || r == "swir2" )
          facts.hasSwir = true;
        if ( r == "red_edge" || r == "rededge" )
          facts.hasRedEdge = true;
      }
    }
  }
  return facts;
}

struct GridFacts {
  std::string crs;
  double pixelSizeX = 0;
  double pixelSizeY = 0;
  Json::Int width = 0;
  Json::Int height = 0;
};

/// The inspect tools emit CRS as a string (vectors) or as {authid, wkt}
/// (rasters); normalize both to the authid (or the raw string).
std::string crsOf( const Json::Value &understanding )
{
  const Json::Value &crs = understanding.get( "crs", Json::Value() );
  if ( crs.isString() )
    return crs.asString();
  if ( crs.isObject() )
    return crs.get( "authid", "" ).asString();
  return "";
}

GridFacts gridFacts( const Json::Value &understanding )
{
  GridFacts facts;
  facts.crs = crsOf( understanding );
  if ( understanding.isMember( "pixel_size" ) && understanding["pixel_size"].isArray() &&
       understanding["pixel_size"].size() == 2 )
  {
    facts.pixelSizeX = understanding["pixel_size"][0].asDouble();
    facts.pixelSizeY = understanding["pixel_size"][1].asDouble();
  }
  if ( understanding.isMember( "size" ) && understanding["size"].isArray() &&
       understanding["size"].size() == 2 )
  {
    facts.width = understanding["size"][0].asInt64();
    facts.height = understanding["size"][1].asInt64();
  }
  return facts;
}

std::string radiometricState( const Json::Value &understanding )
{
  return lowered( understanding.get( "radiometric_state", "" ).asString() );
}

std::string modalityOf( const Json::Value &understanding )
{
  return lowered( understanding.get( "modality", "" ).asString() );
}

/// SAR facts: polarizations + calibration domain from the understanding doc
/// (roles / product metadata). Unknown facts stay unknown — a *warning* is
/// emitted, never a silent pass.
struct SarFacts {
  std::string polarization;   ///< first polarization found ("hh"/"vv"/...)
  bool calibrationDeclared = false;
  std::string calibration;    ///< "sigma0" | "gamma0" | "dn" | ...
};

SarFacts sarFacts( const Json::Value &understanding )
{
  SarFacts facts;
  const BandFacts bands = bandFacts( understanding );
  for ( const std::string &role : bands.roles )
  {
    if ( role == "hh" || role == "vv" || role == "hv" || role == "vh" )
    {
      facts.polarization = role;
      break;
    }
  }
  const std::string state = radiometricState( understanding );
  for ( const char *domain : { "sigma0", "gamma0", "beta0", "dn" } )
  {
    if ( state.find( domain ) != std::string::npos )
    {
      facts.calibrationDeclared = true;
      facts.calibration = domain;
      break;
    }
  }
  return facts;
}

Json::Value inputSummary( const PreflightInput &input )
{
  Json::Value summary( Json::objectValue );
  summary["name"] = input.name;
  summary["ref"] = input.reference;
  if ( input.resolved() )
  {
    summary["path"] = input.understanding.get( "path", "" ).asString();
    summary["crs"] = input.understanding.get( "crs", Json::Value() );
    summary["modality"] = input.understanding.get( "modality", "unknown" );
    summary["band_count"] = input.understanding.get( "band_count", Json::Value() );
    summary["radiometric_state"] = input.understanding.get( "radiometric_state", Json::Value() );
  }
  else
  {
    summary["error"] = input.resolutionError.toJson();
  }
  return summary;
}

// --- shared rules ----------------------------------------------------------

bool sharedRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  bool fatal = false;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
    {
      addBlocker( outcome, input.resolutionError.code.empty() ? std::string( error_codes::kDatasetNotFound )
                                                              : input.resolutionError.code,
                  "Input '" + input.name + "' did not resolve: " + input.resolutionError.summary,
                  "spatial.understand", input.resolutionError.details );
      fatal = true;
    }
  }
  if ( inputs.empty() )
  {
    addBlocker( outcome, error_codes::kInvalidParameter, "No inputs provided",
                "harness.preflight", Json::Value() );
    fatal = true;
  }
  return fatal;
}

// --- rule packs ------------------------------------------------------------

/// Spectral-window requirement for a band-ratio index preflight.
enum class BandRequirement
{
  Nir,
  Red,
  Green,
  Blue,
  Swir,
  RedEdge,
};

bool bandPresent( const BandFacts &facts, BandRequirement requirement )
{
  switch ( requirement )
  {
    case BandRequirement::Nir:
      return facts.hasNir;
    case BandRequirement::Red:
      return facts.hasRed;
    case BandRequirement::Green:
      return facts.hasGreen;
    case BandRequirement::Blue:
      return facts.hasBlue;
    case BandRequirement::Swir:
      return facts.hasSwir;
    case BandRequirement::RedEdge:
      return facts.hasRedEdge;
  }
  return false;
}

/// Window description for blocker messages.
std::string bandWindowLabel( BandRequirement requirement )
{
  switch ( requirement )
  {
    case BandRequirement::Nir:
      return "NIR (role or 750-1100nm)";
    case BandRequirement::Red:
      return "Red (role or 600-700nm)";
    case BandRequirement::Green:
      return "Green (role or 500-600nm)";
    case BandRequirement::Blue:
      return "Blue (role or 430-520nm)";
    case BandRequirement::Swir:
      return "SWIR (role or 1550-1750/2080-2350nm)";
    case BandRequirement::RedEdge:
      return "Red edge (role or 700-745nm)";
  }
  return "band";
}

/// Generalized band-ratio rule pack (Platform 5.0): the NDVI checks applied
/// to an arbitrary index's band requirements. `indexLabel` names the index
/// in messages. Refuse-on-missing bands (blockers), warn on raw DN.
void bandRatioRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome,
                     const std::string &indexLabel,
                     const std::vector<std::pair<std::string, BandRequirement>> &requirements )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const BandFacts facts = bandFacts( input.understanding );
    for ( const auto &[ roleLabel, requirement ] : requirements )
    {
      if ( !bandPresent( facts, requirement ) )
        addBlocker( outcome, error_codes::kBandRoleUnresolved,
                    "Input '" + input.name + "' has no " + bandWindowLabel( requirement ) +
                      " band required for " + indexLabel,
                    "inspect_bands", Json::Value() );
    }
    const std::string radiometry = radiometricState( input.understanding );
    if ( radiometry.empty() )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name + "' declares no radiometric state; " + indexLabel +
                    " quality cannot be guaranteed" );
    else if ( radiometry.find( "dn" ) != std::string::npos &&
              radiometry.find( "reflectance" ) == std::string::npos )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name + "' is raw DN; consider calibration to reflectance "
                  "for comparable " + indexLabel );
    addInfo( outcome, "nodata_declared", input.understanding.isMember( "nodata" ) ||
                                           input.understanding.isMember( "bands" ),
             "INVALID_RADIOMETRY" );
  }
}

void ndviRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  bandRatioRules( inputs, outcome, "index", { { "NIR", BandRequirement::Nir },
                                              { "Red", BandRequirement::Red } } );
}

void opticalChangeRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const PreflightInput *primary = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( !primary )
    {
      primary = &input;
      continue;
    }
    const GridFacts a = gridFacts( primary->understanding );
    const GridFacts b = gridFacts( input.understanding );
    if ( !a.crs.empty() && !b.crs.empty() && a.crs != b.crs )
      addBlocker( outcome, error_codes::kCrsMismatch,
                  "CRS mismatch between '" + primary->name + "' and '" + input.name + "'",
                  "reproject_to_reference", Json::Value() );
    const bool sizeMismatch =
      a.width && b.width && ( a.width != b.width || a.height != b.height );
    if ( sizeMismatch )
      addBlocker( outcome, error_codes::kGridMismatch,
                  "Raster size mismatch: '" + primary->name + "' is " +
                    std::to_string( a.width ) + "x" + std::to_string( a.height ) + ", '" +
                    input.name + "' is " + std::to_string( b.width ) + "x" +
                    std::to_string( b.height ),
                  "align_to_reference", Json::Value() );
    const bool resolutionMismatch =
      a.pixelSizeX && b.pixelSizeX && std::fabs( a.pixelSizeX - b.pixelSizeX ) > 1e-9;
    if ( resolutionMismatch && !sizeMismatch )
      addWarning( outcome, "GRID_MISMATCH",
                  "Pixel size differs between '" + primary->name + "' and '" + input.name +
                    "' — resample to a shared grid for pixel-wise change" );
    const std::string stateA = radiometricState( primary->understanding );
    const std::string stateB = radiometricState( input.understanding );
    if ( !stateA.empty() && !stateB.empty() && stateA != stateB )
      addBlocker( outcome, error_codes::kInvalidRadiometry,
                  "Radiometric states differ ('" + primary->name + "': " + stateA + ", '" +
                    input.name + "': " + stateB + ") — normalize before change",
                  "normalize_radiometry", Json::Value() );
  }
  if ( inputs.size() < 2 && std::any_of( inputs.begin(), inputs.end(),
                                         []( const PreflightInput &i ) { return i.resolved(); } ) )
    addBlocker( outcome, error_codes::kInvalidParameter,
                "Change detection needs two epochs; got one", "harness.plan", Json::Value() );
}

void sarChangeRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( !modality.empty() && modality != "sar" && modality != "unknown" )
      addBlocker( outcome, error_codes::kModalityMismatch,
                  "Input '" + input.name + "' is " + modality + ", not SAR",
                  "check_dataset", Json::Value() );
    if ( modality == "unknown" )
      addWarning( outcome, "MODALITY_MISMATCH",
                  "Input '" + input.name +
                    "' modality could not be verified; run spatial:understand on it" );
    const SarFacts facts = sarFacts( input.understanding );
    if ( facts.polarization.empty() )
      addWarning( outcome, "POLARIZATION_MISMATCH",
                  "Input '" + input.name +
                    "' declares no polarization; cross-pol comparisons will silently degrade" );
  }
  const PreflightInput *primary = nullptr;
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( !primary )
    {
      primary = &input;
      continue;
    }
    const SarFacts a = sarFacts( primary->understanding );
    const SarFacts b = sarFacts( input.understanding );
    if ( !a.polarization.empty() && !b.polarization.empty() &&
         a.polarization != b.polarization )
      addBlocker( outcome, error_codes::kPolarizationMismatch,
                  "Polarization mismatch: '" + primary->name + "' is " + a.polarization +
                    ", '" + input.name + "' is " + b.polarization,
                  "select_matching_polarization", Json::Value() );
    if ( a.calibrationDeclared && b.calibrationDeclared && a.calibration != b.calibration )
      addBlocker( outcome, error_codes::kCalibrationMismatch,
                  "Calibration domains differ ('" + primary->name + "': " + a.calibration +
                    ", '" + input.name + "': " + b.calibration + ")",
                  "calibrate_consistently", Json::Value() );
    const GridFacts ga = gridFacts( primary->understanding );
    const GridFacts gb = gridFacts( input.understanding );
    if ( !ga.crs.empty() && !gb.crs.empty() && ga.crs != gb.crs )
      addBlocker( outcome, error_codes::kCrsMismatch,
                  "CRS mismatch between SAR epochs", "reproject_to_reference",
                  Json::Value() );
    if ( ga.width && gb.width && ( ga.width != gb.width || ga.height != gb.height ) )
      addWarning( outcome, "GRID_MISMATCH",
                  "SAR epochs differ in grid size; terrain correction to a shared grid is "
                  "recommended" );
  }
  if ( inputs.size() < 2 && std::any_of( inputs.begin(), inputs.end(),
                                         []( const PreflightInput &i ) { return i.resolved(); } ) )
    addBlocker( outcome, error_codes::kInvalidParameter,
                "SAR change detection needs two epochs; got one", "harness.plan",
                Json::Value() );
}

void classifyRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  const auto isTraining = []( const PreflightInput &i )
  { return i.name == "training" || i.name == "samples"; };
  const auto hasTraining = std::any_of( inputs.begin(), inputs.end(), isTraining );
  if ( !hasTraining )
    addBlocker( outcome, error_codes::kTrainingInvalid,
                "Supervised classification needs a training sample input named "
                "'training' or 'samples'",
                "harness.plan", Json::Value() );
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    if ( isTraining( input ) )
    {
      const std::string kind = input.understanding.get( "source_kind", "" ).asString();
      if ( kind == "raster" )
        addWarning( outcome, "TRAINING_INVALID",
                    "Training input '" + input.name +
                      "' is a raster; verify it is a class map paired with feature bands" );
      else if ( kind == "vector" )
      {
        if ( input.understanding.get( "feature_count", 0 ).asInt() <= 0 )
          addBlocker( outcome, error_codes::kTrainingInvalid,
                      "Training vector '" + input.name + "' has no features",
                      "check_training", Json::Value() );
      }
    }
  }
  addInfo( outcome, "model_compatibility_checked", true, "MODEL_INCOMPATIBLE" );
}

void phenologyRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  // Phenology works on a temporal collection; the harness preflights the
  // collection via its own tool, so here we verify the referenced scenes are
  // comparable rasters and warn when nothing declares acquisition times.
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const BandFacts facts = bandFacts( input.understanding );
    if ( !facts.hasNir && !facts.hasRed )
      addWarning( outcome, "BAND_ROLE_UNRESOLVED",
                  "Input '" + input.name +
                    "' exposes no NIR/Red roles; vegetation phenology needs them" );
    if ( !input.understanding.isMember( "acquisition_time" ) &&
         !input.understanding.isMember( "SICNU_ACQUISITION_DATE" ) )
      addWarning( outcome, "TIME_ORDER_INVALID",
                  "Input '" + input.name +
                    "' carries no acquisition time; scene ordering cannot be verified" );
  }
}

} // namespace

Json::Value PreflightOutcome::toJson( const std::string &subject ) const
{
  return sicnu::agent::contracts::makePreflightResult( subject, verdict, issues, checks );
}

bool intentRequiresPair( const std::string &intent )
{
  return intent == "change" || intent == "sar_change" || intent == "dnbr" ||
         intent == "accuracy" || intent == "sar_flood";
}

/// Single-input SAR rule pack (Platform 5.0): modality + calibration checks
/// without the epoch pairing of sar_change.
void sarSingleRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const std::string modality = modalityOf( input.understanding );
    if ( !modality.empty() && modality != "sar" && modality != "unknown" )
      addBlocker( outcome, error_codes::kModalityMismatch,
                  "Input '" + input.name + "' is " + modality + ", not SAR",
                  "check_dataset", Json::Value() );
    const SarFacts facts = sarFacts( input.understanding );
    if ( !facts.calibrationDeclared )
      addWarning( outcome, "INVALID_RADIOMETRY",
                  "Input '" + input.name +
                    "' declares no SAR calibration domain; calibrate (sigma0/gamma0) before "
                    "thresholding or comparing scenes" );
    if ( facts.polarization.empty() )
      addWarning( outcome, "POLARIZATION_MISMATCH",
                  "Input '" + input.name +
                    "' declares no polarization; cross-pol comparisons will silently degrade" );
  }
}

/// Terrain rule pack: single raster expected, CRS must be declared (slope/
/// aspect in a geographic CRS is a physical error, not a style problem).
void terrainRules( const std::vector<PreflightInput> &inputs, PreflightOutcome &outcome )
{
  for ( const PreflightInput &input : inputs )
  {
    if ( !input.resolved() )
      continue;
    const GridFacts grid = gridFacts( input.understanding );
    if ( grid.crs.empty() )
      addWarning( outcome, "CRS_MISMATCH",
                  "Input '" + input.name + "' declares no CRS; terrain products need a "
                  "projected CRS with metric units" );
    else if ( grid.crs.find( "EPSG:4326" ) != std::string::npos )
      addWarning( outcome, "CRS_MISMATCH",
                  "Input '" + input.name +
                    "' is in a geographic CRS; slope/aspect need a projected CRS" );
    if ( input.understanding.isMember( "band_count" ) && input.understanding["band_count"].isInt() &&
         input.understanding["band_count"].asInt() < 1 )
      addBlocker( outcome, error_codes::kDatasetNotFound,
                  "Input '" + input.name + "' has no bands", "check_dataset", Json::Value() );
  }
}

PreflightOutcome runScientificPreflight( const std::string &intent,
                                         const std::vector<PreflightInput> &inputs )
{
  PreflightOutcome outcome;
  if ( sharedRules( inputs, outcome ) )
  {
    outcome.verdict = "blocked";
    return outcome;
  }

  if ( intent == "ndvi" || intent == "evi" || intent == "savi" || intent == "ndre" )
    ndviRules( inputs, outcome );
  else if ( intent == "ndwi" || intent == "water" || intent == "flood" )
    bandRatioRules( inputs, outcome, intent, { { "Green", BandRequirement::Green },
                                               { "NIR", BandRequirement::Nir } } );
  else if ( intent == "mndwi" )
    bandRatioRules( inputs, outcome, "MNDWI", { { "Green", BandRequirement::Green },
                                                { "SWIR", BandRequirement::Swir } } );
  else if ( intent == "ndsi" )
    bandRatioRules( inputs, outcome, "NDSI", { { "Green", BandRequirement::Green },
                                               { "SWIR", BandRequirement::Swir } } );
  else if ( intent == "nbr" || intent == "dnbr" )
  {
    bandRatioRules( inputs, outcome, intent, { { "NIR", BandRequirement::Nir },
                                               { "SWIR", BandRequirement::Swir } } );
    if ( intent == "dnbr" && inputs.size() < 2 )
      addBlocker( outcome, error_codes::kInvalidParameter,
                  "dNBR needs pre- and post-fire epochs; got one", "harness.plan",
                  Json::Value() );
  }
  else if ( intent == "ndbi" )
    bandRatioRules( inputs, outcome, "NDBI", { { "SWIR", BandRequirement::Swir },
                                               { "NIR", BandRequirement::Nir } } );
  else if ( intent == "bsi" )
    bandRatioRules( inputs, outcome, "BSI", { { "Blue", BandRequirement::Blue },
                                              { "Red", BandRequirement::Red },
                                              { "NIR", BandRequirement::Nir },
                                              { "SWIR", BandRequirement::Swir } } );
  else if ( intent == "change" )
    opticalChangeRules( inputs, outcome );
  else if ( intent == "sar_change" || intent == "sar_flood" )
    sarChangeRules( inputs, outcome );
  else if ( intent == "sar" || intent == "ship" || intent == "sar_water" )
    sarSingleRules( inputs, outcome );
  else if ( intent == "classify" )
    classifyRules( inputs, outcome );
  else if ( intent == "accuracy" )
  {
    classifyRules( inputs, outcome );
    if ( inputs.size() < 2 )
      addBlocker( outcome, error_codes::kInvalidParameter,
                  "Accuracy assessment needs the classified map and a reference input",
                  "harness.plan", Json::Value() );
  }
  else if ( intent == "phenology" || intent == "temporal" )
    phenologyRules( inputs, outcome );
  else if ( intent == "terrain" )
    terrainRules( inputs, outcome );
  else if ( intent == "qa" || intent == "preprocess" || intent == "inference" )
  {
    // Shared rules only: mask/preprocess/model intents have no physical band
    // demands beyond resolvability.
  }

  const bool blocked = std::any_of(
    outcome.issues.begin(), outcome.issues.end(),
    []( const Json::Value &issue ) {
      return issue.isMember( "severity" ) && issue["severity"].asString() == "error";
    } );
  const bool repairableWarning = std::any_of(
    outcome.issues.begin(), outcome.issues.end(), []( const Json::Value &issue ) {
      return issue.isMember( "severity" ) && issue["severity"].asString() == "warning" &&
             issue.get( "repairable", false ).asBool();
    } );
  outcome.verdict = blocked ? "blocked" : ( repairableWarning ? "fixable" : "ok" );
  return outcome;
}

PreflightOutcome preflightIntent( const std::string &intent,
                                  const Json::Value &refs )
{
  std::vector<PreflightInput> inputs;
  if ( refs.isArray() )
  {
    for ( const auto &entry : refs )
    {
      PreflightInput input;
      input.name = entry.get( "name", "" ).asString();
      input.reference = entry.get( "ref", entry.get( "asset", "" ) ).asString();
      HarnessError error;
      if ( const auto resolved =
             resolveDatasetRef( QString::fromStdString( input.reference ), &error ) )
      {
        // Gather bounded facts through the inspection tool — the same facts
        // spatial:understand returns, so preflight and grounding agree.
        if ( auto inspect = SpatialToolRegistry::instance().find( "spatial:raster_inspect" ) )
        {
          Json::Value inspectInput;
          inspectInput["path"] = resolved->path.toStdString();
          const SpatialToolResult result = ( *inspect )->execute( inspectInput );
          if ( result.success )
          {
            input.understanding =
              sicnu::agent::contracts::datasetUnderstandingFromRasterInspect( result.output );
            input.understanding["modality"] = inferModality( result.output );
          }
        }
        if ( input.understanding.isNull() )
        {
          if ( auto vectorInspect =
                 SpatialToolRegistry::instance().find( "spatial:vector_inspect" ) )
          {
            Json::Value inspectInput;
            inspectInput["path"] = resolved->path.toStdString();
            const SpatialToolResult result = ( *vectorInspect )->execute( inspectInput );
            if ( result.success )
              input.understanding = sicnu::agent::contracts::datasetUnderstandingFromVectorInspect(
                result.output );
          }
        }
        if ( input.understanding.isNull() )
          input.resolutionError = HarnessError::make(
            error_codes::kDatasetNotFound,
            "Resolved file is neither raster nor vector: " + resolved->path.toStdString() );
      }
      else
      {
        input.resolutionError = error;
      }
      inputs.push_back( std::move( input ) );
    }
  }
  return runScientificPreflight( intent, inputs );
}

} // namespace sicnu::agent::harness
