// src/processing/algorithms/temporal/temporal_fusion.cpp
#include "temporal_fusion.h"

#include <cmath>
#include <cstring>

namespace sicnu::temporal
{

GridCompatibility checkGridCompatibility( const GridSignature &a,
                                          const GridSignature &b,
                                          double geotransformTolerance )
{
  GridCompatibility result;
  if ( a.width != b.width )
    result.mismatches.push_back( "width" );
  if ( a.height != b.height )
    result.mismatches.push_back( "height" );
  for ( int k = 0; k < 6; ++k )
  {
    if ( std::abs( a.geoTransform[k] - b.geoTransform[k] ) > geotransformTolerance )
    {
      result.mismatches.push_back( "geotransform" );
      break;
    }
  }
  // WKT comparison is textual: equivalent CRS in different dialects refuses
  // fusion — deliberate fail-closed, since a silently assumed match is worse
  // than a typed rejection the caller can act on.
  std::string pa = a.projection;
  std::string pb = b.projection;
  auto trim = []( std::string &s ) {
    const auto first = s.find_first_not_of( " \t\r\n" );
    const auto last = s.find_last_not_of( " \t\r\n" );
    s = first == std::string::npos ? std::string() : s.substr( first, last - first + 1 );
  };
  trim( pa );
  trim( pb );
  if ( pa != pb )
    result.mismatches.push_back( "projection" );
  result.compatible = result.mismatches.empty();
  return result;
}

std::vector<std::pair<std::string, std::string>>
fusionProvenanceItems( const FusionProvenance &prov )
{
  return {
    { "SICNU_FUSION_OPTICAL_PATH", prov.opticalPath },
    { "SICNU_FUSION_SAR_PATH", prov.sarPath },
    { "SICNU_FUSION_OPTICAL_BANDS", std::to_string( prov.opticalBandCount ) },
    { "SICNU_FUSION_SAR_BANDS", std::to_string( prov.sarBandCount ) },
    { "SICNU_FUSION_OPTICAL_PREFIX", prov.opticalPrefix },
    { "SICNU_FUSION_SAR_PREFIX", prov.sarPrefix },
    { "SICNU_FUSION_GTOL", std::to_string( prov.geotransformTolerance ) },
    { "SICNU_FUSION_MODE", "feature_stack_shared_grid" },
  };
}

} // namespace sicnu::temporal
