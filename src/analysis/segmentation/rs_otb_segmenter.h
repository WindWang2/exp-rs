// rs_otb_segmenter.h — OTB Segmentation adapter behind RsSegmenterPort.
//
// Memory RsSegmentMap is the source of truth; temp GeoTIFF lives only inside
// the adapter. No silent teaching-segmenter fallback when OTB is missing.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segmenter_port.h"

class QGIS_ANALYSIS_EXPORT RsOtbSegmenter : public RsSegmenterPort
{
  public:
    /// True when otbcli_Segmentation (or bundle equivalent) is discoverable.
    static bool isAvailable();

    /// Post-segmentation void contract (R5 NoData audit): the OTB CLI has no
    /// input-sentinel parameter, so declared voids (e.g. -9999 padding from
    /// rs:align) can cluster into fake objects of their own. A pixel counts as
    /// void when ANY raster band equals that band's declared finite sentinel
    /// (the same any-band rule rs:obia_features applies). Every segment whose
    /// pixels are ALL void is relabeled 0 (the ADR 0054 nodata label).
    /// Mixed void/data segments survive — their per-pixel statistics are
    /// masked downstream. Returns false (map untouched) when @a rasterPath
    /// cannot be read; callers treat that as best-effort hygiene, not fatal.
    static bool relabelAllVoidSegments( RsSegmentMap &segMap, const QString &rasterPath );

    RsSegmenterResult segment(
        const QString &rasterPath,
        const RsLevelSpec &spec,
        const std::function<bool()> &isCanceled = nullptr ) override;
};
