// display_stretch_ports.h — Ports for display stretch apply (layer token is opaque void*)
#pragma once

#include "display_stretch_types.h"

#include <vector>

namespace rs::display {

/**
 * Write path for resolved stretch onto a display subject.
 * layerToken is production: QgsRasterLayer*; tests may pass any non-null cookie.
 */
class RasterDisplayTarget
{
public:
  virtual ~RasterDisplayTarget() = default;

  virtual StretchResult<DisplayTargetInfo> inspect( void *layerToken ) const = 0;

  /**
   * Install CE from resolved stretch. Must clone-then-replace, never mutate live renderer.
   */
  virtual ApplyStretchResult apply( void *layerToken, const ResolvedStretch &resolved ) = 0;
};

class BandStatsSource
{
public:
  virtual ~BandStatsSource() = default;

  virtual StretchResult<BandStats> stats( void *layerToken, int band ) const = 0;
};

/** Test double: records apply calls; returns canned inspect/stats. */
class RecordingDisplayTarget final : public RasterDisplayTarget
{
public:
  DisplayTargetInfo cannedInfo;
  StretchError *failInspect = nullptr;
  StretchError *failApply = nullptr;

  struct Call
  {
    enum class Op
    {
      Inspect,
      Apply
    } op;
    ResolvedStretch resolved;
  };
  mutable std::vector<Call> calls;

  StretchResult<DisplayTargetInfo> inspect( void *layerToken ) const override;
  ApplyStretchResult apply( void *layerToken, const ResolvedStretch &resolved ) override;
};

class RecordingBandStats final : public BandStatsSource
{
public:
  BandStats canned;
  StretchError *fail = nullptr;
  mutable int lastBand = -1;

  StretchResult<BandStats> stats( void *layerToken, int band ) const override;
};

} // namespace rs::display
