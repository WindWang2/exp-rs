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

    RsSegmenterResult segment(
        const QString &rasterPath,
        const RsLevelSpec &spec,
        const std::function<bool()> &isCanceled = nullptr ) override;
};
