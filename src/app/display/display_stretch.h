// display_stretch.h — Display-only stretch deep module (resolve + apply)
#pragma once

#include "display_stretch_ports.h"
#include "display_stretch_types.h"

namespace rs::display {

/** Validate Spec without I/O. nullopt = valid. */
std::optional<StretchError> validate( const StretchSpec &spec );

/**
 * Pure resolve: Spec + band stats → concrete display min/max.
 * Does not touch renderers.
 */
ResolveStretchResult resolve( const StretchSpec &spec, const BandStats &stats );

/**
 * Resolve + apply through ports.
 * statsBand: 1-based; if Spec has statsBand, that wins.
 */
ApplyStretchResult apply( RasterDisplayTarget &target,
                          BandStatsSource &statsSource,
                          void *layerToken,
                          const StretchSpec &spec,
                          int defaultStatsBand = 1 );

bool needsBandStats( const StretchSpec &spec );
bool needsMeanStd( const StretchSpec &spec );

} // namespace rs::display
