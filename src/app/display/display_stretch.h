// display_stretch.h — Display-only stretch deep module (resolve + apply)
#pragma once

#include "display_stretch_ports.h"
#include "display_stretch_types.h"

namespace rs::display {

/**
 * Deep module encapsulating raster display stretch validation, resolution, and application.
 */
class DisplayStretchPipeline {
public:
  /** Validate Spec without I/O. nullopt = valid. */
  static std::optional<StretchError> validate( const StretchSpec &spec );

  /**
   * Pure resolve: Spec + band stats -> concrete display min/max.
   * Does not touch renderers.
   */
  static ResolveStretchResult resolve( const StretchSpec &spec, const BandStats &stats );

  /**
   * Resolve + apply through ports.
   * statsBand: 1-based; if Spec has statsBand, that wins.
   */
  static ApplyStretchResult apply( RasterDisplayTarget &target,
                                  BandStatsSource &statsSource,
                                  void *layerToken,
                                  const StretchSpec &spec,
                                  int defaultStatsBand = 1 );

  static bool needsBandStats( const StretchSpec &spec );
  static bool needsMeanStd( const StretchSpec &spec );
};

// Backward-compatible free-function seam.
inline std::optional<StretchError> validate( const StretchSpec &spec ) {
  return DisplayStretchPipeline::validate( spec );
}

inline ResolveStretchResult resolve( const StretchSpec &spec, const BandStats &stats ) {
  return DisplayStretchPipeline::resolve( spec, stats );
}

inline ApplyStretchResult apply( RasterDisplayTarget &target,
                                 BandStatsSource &statsSource,
                                 void *layerToken,
                                 const StretchSpec &spec,
                                 int defaultStatsBand = 1 ) {
  return DisplayStretchPipeline::apply( target, statsSource, layerToken, spec, defaultStatsBand );
}

inline bool needsBandStats( const StretchSpec &spec ) {
  return DisplayStretchPipeline::needsBandStats( spec );
}

inline bool needsMeanStd( const StretchSpec &spec ) {
  return DisplayStretchPipeline::needsMeanStd( spec );
}

} // namespace rs::display
