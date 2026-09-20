// src/agent/spatial_tools/spectral_spatial_tools.cpp — D13 agent spectral tools
#include "spectral_spatial_tools.h"

#include <raster/qgsrasterlayer.h>
#include <qgsproject.h>

#include <QFileInfo>

#include <gdal_priv.h>

#include "processing/algorithms/spectral_library.h"
#include "processing/framework/runtime_paths.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using sicnu::agent::spatial_tools::SpatialToolResult;

namespace exp_agent
{
  namespace
  {
    constexpr double kNirTargetNm = 850.0;
    constexpr double kRedTargetNm = 660.0;
    constexpr double kWavelengthToleranceNm = 100.0;

    /// Resolves the GDAL path behind the tool input: explicit "path", or a
    /// project layer id via "layer_id" (QgsProject lookup — the same
    /// resolution idiom as the built-in spatial tools).
    std::string resolveRasterPath( const Json::Value &input, std::string *error )
    {
      if ( input.isMember( "path" ) && input["path"].isString() && !input["path"].asString().empty() )
        return input["path"].asString();

      if ( input.isMember( "layer_id" ) && input["layer_id"].isString() )
      {
        const QString layerId = QString::fromStdString( input["layer_id"].asString() );
        if ( QgsMapLayer *layer = QgsProject::instance()->mapLayer( layerId ) )
        {
          if ( QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>( layer ) )
            return raster->source().toStdString();
          *error = "layer is not a raster layer: " + input["layer_id"].asString();
          return std::string();
        }
        *error = "layer_id not found in project: " + input["layer_id"].asString();
        return std::string();
      }
      *error = "missing required parameter: layer_id or path";
      return std::string();
    }

    /// Reads one pixel (nearest, north-up affine) across all bands, plus the
    /// WAVELENGTH metadata grid. Returns false on any IO problem.
    bool readSpectrum( GDALDataset *ds, double x, double y, std::vector<double> *values,
                       std::vector<double> *wavelengthsNm )
    {
      double geotransform[6] = { 0, 1, 0, 0, 0, 1 };
      ds->GetGeoTransform( geotransform );
      const double col = ( x - geotransform[0] ) / geotransform[1];
      const double row = ( y - geotransform[3] ) / geotransform[5];
      if ( col < 0 || row < 0 || col >= ds->GetRasterXSize() || row >= ds->GetRasterYSize() )
        return false;

      const int bandCount = ds->GetRasterCount();
      values->assign( bandCount, std::numeric_limits<double>::quiet_NaN() );
      wavelengthsNm->assign( bandCount, 0.0 );

      for ( int b = 1; b <= bandCount; ++b )
      {
        GDALRasterBand *band = ds->GetRasterBand( b );
        double pixel[1] = { 0.0 };
        if ( band->RasterIO( GF_Read, static_cast<int>( col ), static_cast<int>( row ), 1, 1,
                             pixel, 1, 1, GDT_Float64, 0, 0 ) != CE_None )
          return false;
        int hasNoData = 0;
        const double sentinel = band->GetNoDataValue( &hasNoData );
        if ( ( hasNoData && pixel[0] == sentinel ) || !std::isfinite( pixel[0] ) )
          return false; // NoData or NaN pixels fail the read, never JSON null
        ( *values )[b - 1] = pixel[0];

        if ( const char *wl = band->GetMetadataItem( "WAVELENGTH" ) )
        {
          bool ok = false;
          const double nm = QString::fromUtf8( wl ).toDouble( &ok );
          ( *wavelengthsNm )[b - 1] = ok && nm > 0.0 ? nm : 0.0;
        }
      }
      return true;
    }

    /// Closest band to @p targetNm within tolerance; -1 when none qualifies
    /// (band selection is strictly wavelength-driven — never ordinals).
    int bandClosestTo( const std::vector<double> &wavelengthsNm, double targetNm )
    {
      int best = -1;
      double bestDistance = kWavelengthToleranceNm;
      for ( size_t i = 0; i < wavelengthsNm.size(); ++i )
      {
        const double distance = std::abs( wavelengthsNm[i] - targetNm );
        if ( wavelengthsNm[i] > 0.0 && distance < bestDistance )
        {
          bestDistance = distance;
          best = static_cast<int>( i );
        }
      }
      return best;
    }

    Json::Value doubleVector( const std::vector<double> &values )
    {
      Json::Value array( Json::arrayValue );
      for ( double v : values )
        array.append( v );
      return array;
    }
  } // namespace

  // ── spatial:spectral_inspect ──────────────────────────────────────────────

  std::string SpectralInspectTool::description() const
  {
    return "Extracts the multi-band reflectance spectrum at a map point from a raster layer "
           "(identified by project layer_id or direct path), fits continuum-removal absorption "
           "features (depth, center wavelength) and optionally matches the spectrum against a "
           "reference spectral library, returning the best material and confidence.";
  }

  std::vector<std::string> SpectralInspectTool::tags() const
  {
    return { "spectral", "raster", "inspection", "continuum" };
  }

  Json::Value SpectralInspectTool::inputSchema() const
  {
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";

    Json::Value props( Json::objectValue );
    Json::Value layerId( Json::objectValue );
    layerId["type"] = "string";
    layerId["description"] = "Project layer id of the raster (alternative to path)";
    props["layer_id"] = layerId;

    Json::Value path( Json::objectValue );
    path["type"] = "string";
    path["description"] = "Direct raster path (alternative to layer_id)";
    props["path"] = path;

    Json::Value point( Json::objectValue );
    point["type"] = "array";
    point["items"] = Json::Value( Json::objectValue );
    point["description"] = "[x, y] in the layer CRS; defaults to the raster center";
    props["point"] = point;

    Json::Value match( Json::objectValue );
    match["type"] = "boolean";
    match["description"] = "Match the spectrum against the reference library (default false)";
    props["match_library"] = match;

    Json::Value libraryPath( Json::objectValue );
    libraryPath["type"] = "string";
    libraryPath["description"] = "Reference spectral library JSON (default data/spectral/library.json)";
    props["library_path"] = libraryPath;

    schema["properties"] = props;
    Json::Value required( Json::arrayValue );
    required.append( "layer_id" );
    schema["required"] = required; // path is an accepted alternative at runtime
    return schema;
  }

  Json::Value SpectralInspectTool::outputSchema() const
  {
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    for ( const char *key : { "wavelengths", "reflectance" } )
      props[key] = Json::Value( Json::objectValue );
    props["absorption_depth"] = Json::Value( Json::objectValue );
    props["absorption_wavelength_nm"] = Json::Value( Json::objectValue );
    props["matched_material"] = Json::Value( Json::objectValue );
    props["confidence"] = Json::Value( Json::objectValue );
    schema["properties"] = props;
    return schema;
  }

  SpatialToolResult SpectralInspectTool::execute( const Json::Value &params )
  {
    std::string resolveError;
    const std::string path = resolveRasterPath( params, &resolveError );
    if ( path.empty() )
      return SpatialToolResult::failure( resolveError, "INVALID_PARAMETER", "validation", false );
    if ( QFileInfo::exists( QString::fromStdString( path ) ) == false )
      return SpatialToolResult::failure( "raster not found: " + path, "NOT_FOUND", "io", false );

    GDALDatasetUniquePtr ds( GDALDataset::Open( path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY ) );
    if ( !ds )
      return SpatialToolResult::failure( "GDAL could not open raster: " + path, "GDAL_OPEN_FAILED", "io", false );

    double x = 0.0, y = 0.0;
    if ( params.isMember( "point" ) && params["point"].isArray() && params["point"].size() == 2 )
    {
      if ( !params["point"][0].isNumeric() || !params["point"][1].isNumeric() )
        return SpatialToolResult::failure( "point must be [x, y] numbers", "INVALID_PARAMETER",
                                           "validation", false );
      x = params["point"][0].asDouble();
      y = params["point"][1].asDouble();
    }
    else
    {
      double geotransform[6] = { 0, 1, 0, 0, 0, 1 };
      ds->GetGeoTransform( geotransform );
      x = geotransform[0] + geotransform[1] * ds->GetRasterXSize() / 2.0;
      y = geotransform[3] + geotransform[5] * ds->GetRasterYSize() / 2.0;
    }

    std::vector<double> spectrum, wavelengthsNm;
    if ( !readSpectrum( ds.get(), x, y, &spectrum, &wavelengthsNm ) )
      return SpatialToolResult::failure( "no valid pixel at the requested point", "NO_DATA", "io", false );

    Json::Value out( Json::objectValue );
    out["wavelengths"] = doubleVector( wavelengthsNm );
    out["reflectance"] = doubleVector( spectrum );

    // Continuum-removed absorption: depth at the spectral minimum relative to
    // the linear two-endpoint continuum.
    const size_t n = spectrum.size();
    if ( n >= 2 )
    {
      size_t minIndex = 0;
      for ( size_t i = 0; i < n; ++i )
        if ( spectrum[i] < spectrum[minIndex] )
          minIndex = i;
      const double continuum = spectrum.front() +
                               ( spectrum.back() - spectrum.front() ) *
                                 ( wavelengthsNm[minIndex] - wavelengthsNm.front() ) /
                                 std::max( 1e-9, wavelengthsNm.back() - wavelengthsNm.front() );
      out["absorption_depth"] = std::max( 0.0, continuum - spectrum[minIndex] );
      out["absorption_wavelength_nm"] = wavelengthsNm[minIndex];
    }
    else
    {
      out["absorption_depth"] = 0.0;
      out["absorption_wavelength_nm"] = 0.0;
    }

    out["matched_material"] = Json::Value( "" );
    out["confidence"] = 0.0;
    bool matchLibrary = false;
    if ( params.isMember( "match_library" ) )
    {
      if ( !params["match_library"].isBool() )
        return SpatialToolResult::failure( "match_library must be a boolean", "INVALID_PARAMETER",
                                           "validation", false );
      matchLibrary = params["match_library"].asBool();
    }
    if ( matchLibrary )
    {
      // Runtime-resolved default (same discovery as the operators): a hardcoded
      // relative path only works when the CWD happens to be the repo root.
      const std::string libraryPath =
          params.isMember( "library_path" ) && params["library_path"].isString()
              ? params["library_path"].asString()
              : sicnu::processing::resolveRuntimeDataPath( QStringLiteral( "spectral/library.json" ) )
                    .toStdString();
      SpectralLibrary::Library library;
      QString error;
      if ( !SpectralLibrary::Library::load( QString::fromStdString( libraryPath ), &library, &error ) )
        return SpatialToolResult::failure( "library load failed: " + error.toStdString(),
                                           "LIBRARY_UNAVAILABLE", "io", true );

      // The legacy match seam is float-typed: convert once, explicitly.
      const std::vector<float> floatSpectrum( spectrum.begin(), spectrum.end() );
      const auto matches = SpectralLibrary::matchSpectrum( floatSpectrum, library );
      if ( !matches.empty() && std::isfinite( matches.front().angleDegrees ) )
      {
        // Confidence: 1 at zero angle, 0 at 0.5 rad (28.6°) or beyond.
        out["matched_material"] = matches.front().material.toStdString();
        out["confidence"] = std::max( 0.0, 1.0 - matches.front().angleDegrees / 28.64788975654116 );
      }
    }
    return SpatialToolResult::ok( out );
  }

  // ── spatial:validate_boa_physics ──────────────────────────────────────────

  std::string ValidateBoaPhysicsTool::description() const
  {
    return "Physics self-consistency audit for BOA surface reflectance: range check "
           "(rho in [0, 1] per band), water-surface inversion rule (rho(NIR) > rho(Red) and "
           "rho(NIR) > 0.15) and optional vegetation red-edge ratio rule (rho(NIR)/rho(Red) >= 2). "
           "Bands are chosen strictly by WAVELENGTH metadata. Returns structured anomaly codes "
           "and self-healing suggestions for the agent.";
  }

  std::vector<std::string> ValidateBoaPhysicsTool::tags() const
  {
    return { "spectral", "raster", "physics", "qa" };
  }

  Json::Value ValidateBoaPhysicsTool::inputSchema() const
  {
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";

    Json::Value props( Json::objectValue );
    Json::Value layerId( Json::objectValue );
    layerId["type"] = "string";
    layerId["description"] = "Project layer id of the BOA raster (alternative to path)";
    props["layer_id"] = layerId;

    Json::Value path( Json::objectValue );
    path["type"] = "string";
    path["description"] = "Direct raster path (alternative to layer_id)";
    props["path"] = path;

    Json::Value point( Json::objectValue );
    point["type"] = "array";
    point["items"] = Json::Value( Json::objectValue );
    point["description"] = "[x, y] in the layer CRS; defaults to the raster center";
    props["point"] = point;

    Json::Value surface( Json::objectValue );
    surface["type"] = "string";
    surface["enum"] = Json::Value( Json::arrayValue );
    surface["enum"].append( "water" );
    surface["enum"].append( "vegetation" );
    surface["description"] = "Expected surface type driving the audit rules (default water)";
    props["expected_surface"] = surface;

    schema["properties"] = props;
    Json::Value required( Json::arrayValue );
    required.append( "layer_id" );
    schema["required"] = required;
    return schema;
  }

  Json::Value ValidateBoaPhysicsTool::outputSchema() const
  {
    Json::Value schema( Json::objectValue );
    schema["type"] = "object";
    Json::Value props( Json::objectValue );
    props["has_anomaly"] = Json::Value( Json::objectValue );
    props["anomaly_code"] = Json::Value( Json::objectValue );
    props["anomalies"] = Json::Value( Json::objectValue );
    props["suggestion"] = Json::Value( Json::objectValue );
    schema["properties"] = props;
    return schema;
  }

  SpatialToolResult ValidateBoaPhysicsTool::execute( const Json::Value &params )
  {
    std::string resolveError;
    const std::string path = resolveRasterPath( params, &resolveError );
    if ( path.empty() )
      return SpatialToolResult::failure( resolveError, "INVALID_PARAMETER", "validation", false );
    if ( QFileInfo::exists( QString::fromStdString( path ) ) == false )
      return SpatialToolResult::failure( "raster not found: " + path, "NOT_FOUND", "io", false );

    GDALDatasetUniquePtr ds( GDALDataset::Open( path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY ) );
    if ( !ds )
      return SpatialToolResult::failure( "GDAL could not open raster: " + path, "GDAL_OPEN_FAILED", "io", false );

    double x = 0.0, y = 0.0;
    if ( params.isMember( "point" ) && params["point"].isArray() && params["point"].size() == 2 )
    {
      if ( !params["point"][0].isNumeric() || !params["point"][1].isNumeric() )
        return SpatialToolResult::failure( "point must be [x, y] numbers", "INVALID_PARAMETER",
                                           "validation", false );
      x = params["point"][0].asDouble();
      y = params["point"][1].asDouble();
    }
    else
    {
      double geotransform[6] = { 0, 1, 0, 0, 0, 1 };
      ds->GetGeoTransform( geotransform );
      x = geotransform[0] + geotransform[1] * ds->GetRasterXSize() / 2.0;
      y = geotransform[3] + geotransform[5] * ds->GetRasterYSize() / 2.0;
    }

    std::vector<double> spectrum, wavelengthsNm;
    if ( !readSpectrum( ds.get(), x, y, &spectrum, &wavelengthsNm ) )
      return SpatialToolResult::failure( "no valid pixel at the requested point", "NO_DATA", "io", false );

    const int nirBand = bandClosestTo( wavelengthsNm, kNirTargetNm );
    const int redBand = bandClosestTo( wavelengthsNm, kRedTargetNm );
    if ( nirBand < 0 || redBand < 0 )
      return SpatialToolResult::failure(
        "WAVELENGTH band metadata missing or too sparse to identify NIR/Red — refusing to guess "
        "band ordinals; stack the product with SICNU wavelength metadata first",
        "MISSING_WAVELENGTH_METADATA", "validation", false );

    Json::Value anomalies( Json::arrayValue );
    for ( double value : spectrum )
    {
      if ( !std::isfinite( value ) || value < 0.0 || value > 1.0 )
      {
        anomalies.append( "UNPHYSICAL_REFLECTANCE_RANGE" );
        break;
      }
    }

    const double nir = spectrum[static_cast<size_t>( nirBand )];
    const double red = spectrum[static_cast<size_t>( redBand )];

    const std::string expectedSurface =
      params.isMember( "expected_surface" ) && params["expected_surface"].isString()
        ? params["expected_surface"].asString()
        : std::string( "water" );

    std::string suggestion;
    if ( expectedSurface == "water" && nir > red && nir > 0.15 )
    {
      anomalies.append( "INVERTED_WATER_SPECTRUM" );
      suggestion = "Water pixels must absorb in NIR (rho(NIR) ~ 0). The near-infrared reflectance is "
                   "inflated above the red band — the pixel is vegetation-drifted or still carries "
                   "path scatter. Suggested action: call the rs:atmospheric_correction operator "
                   "(6S LUT inversion) and re-audit before classification.";
    }
    else if ( expectedSurface == "vegetation" && red > 0.0 && nir / red < 2.0 )
    {
      anomalies.append( "INVERTED_VEGETATION_RATIO" );
      suggestion = "Healthy green vegetation requires rho(NIR)/rho(Red) >= 2.0 (red-edge preserved). "
                   "The ratio is inverted — vegetation is senescent or the band is miscalibrated. "
                   "Suggested action: re-run radiometric calibration, then rs:atmospheric_correction.";
    }
    if ( anomalies.size() > 0 && suggestion.empty() )
    {
      suggestion = "Reflectance outside [0, 1] indicates a physical-unit violation. Suggested "
                   "action: verify the calibration chain (DN -> Radiance -> BOA) via "
                   "rs:radiometric_calibration and rs:atmospheric_correction.";
    }

    Json::Value out( Json::objectValue );
    out["has_anomaly"] = anomalies.size() > 0;
    out["anomaly_code"] = anomalies.size() > 0 ? anomalies[0] : Json::Value( "" );
    out["anomalies"] = anomalies;
    out["suggestion"] = suggestion;
    out["audit"] = Json::Value( Json::objectValue );
    out["audit"]["nir_band_1based"] = nirBand + 1;
    out["audit"]["red_band_1based"] = redBand + 1;
    out["audit"]["nir_wavelength_nm"] = wavelengthsNm[static_cast<size_t>( nirBand )];
    out["audit"]["red_wavelength_nm"] = wavelengthsNm[static_cast<size_t>( redBand )];
    out["audit"]["nir"] = nir;
    out["audit"]["red"] = red;
    return SpatialToolResult::ok( out );
  }
} // namespace exp_agent
