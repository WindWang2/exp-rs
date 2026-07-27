// qgs_display_stretch.h — QGIS adapters + convenience facade for Display Stretch
#pragma once

#include "display_stretch.h"

class QgsRasterLayer;

namespace rs::display {

class QgsRasterDisplayTarget final : public RasterDisplayTarget
{
public:
  StretchResult<DisplayTargetInfo> inspect( void *layerToken ) const override;
  ApplyStretchResult apply( void *layerToken, const ResolvedStretch &resolved ) override;
};

class QgsBandStatsSource final : public BandStatsSource
{
public:
  StretchResult<BandStats> stats( void *layerToken, int band ) const override;
};

/**
 * Convenience: apply Spec to a QgsRasterLayer (GUI-thread).
 * Used by HistogramStretchWidget.
 */
ApplyStretchResult applyToLayer( QgsRasterLayer *layer, const StretchSpec &spec,
                                 int defaultStatsBand = 1 );

} // namespace rs::display
