// src/agent/harness/band_facts.h
#pragma once

//
// Harness 7.0 shared dataset-fact extraction (mission Areas B/C).
//
// One implementation of the physical-fact extraction from DatasetUnderstanding
// documents for both the scientific preflight rule packs and the capability
// graph feasibility checks. Two copies of these functions would drift; there
// is exactly one.
//
// Band-role extraction keeps the wavelength fallback: a band without an
// explicit role is *not* guessed from position — but a declared wavelength in
// a documented window is a physical fact, not a guess.
//

#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::agent::harness::facts {

std::string lowered( std::string text );

struct BandFacts {
  std::vector<std::string> roles;
  bool hasNir = false;
  bool hasRed = false;
  bool hasGreen = false;
  bool hasBlue = false;
  bool hasSwir = false;    ///< any SWIR window (1550-1750 or 2080-2350 nm)
  bool hasRedEdge = false; ///< 700-745 nm red-edge window
  bool nirByWavelength = false;
  bool redByWavelength = false;
  int bandCount = 0;
};

BandFacts bandFacts( const Json::Value &understanding );

struct GridFacts {
  std::string crs;
  double pixelSizeX = 0;
  double pixelSizeY = 0;
  Json::Int width = 0;
  Json::Int height = 0;
};

/// The inspect tools emit CRS as a string (vectors) or as {authid, wkt}
/// (rasters); normalize both to the authid (or the raw string).
std::string crsOf( const Json::Value &understanding );

GridFacts gridFacts( const Json::Value &understanding );

std::string radiometricState( const Json::Value &understanding );

std::string modalityOf( const Json::Value &understanding );

/// SAR facts: polarizations + calibration domain from the understanding doc
/// (roles / product metadata). Unknown facts stay unknown — callers warn,
/// never silently pass.
struct SarFacts {
  std::string polarization;   ///< first polarization found ("hh"/"vv"/...)
  bool calibrationDeclared = false;
  std::string calibration;    ///< "sigma0" | "gamma0" | "dn" | ...
};

SarFacts sarFacts( const Json::Value &understanding );

/// Acquisition time of an understanding document, or empty when undeclared.
std::string acquisitionTime( const Json::Value &understanding );

} // namespace sicnu::agent::harness::facts
