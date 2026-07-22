// rs_parent_link.h — Hierarchical OBIA V1: pure parent-link port (P1 majority).
//
// Maps fine-level segment ids to coarse parents without OTB or UI.
// Sparse table: only non-zero fine labels appear as keys.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

#include <QMap>
#include <QString>

/// Sparse fineId → parentCoarseId (0 = orphan / no valid votes).
struct QGIS_ANALYSIS_EXPORT RsParentTable
{
    QMap<quint32, quint32> fineToParent;
    bool ok = true;
    QString errorMessage;
};

/// Pluggable parent-link strategy (P4).
class QGIS_ANALYSIS_EXPORT RsParentLinkStrategy
{
  public:
    virtual ~RsParentLinkStrategy() = default;

    /// Link fine objects to coarse parents. Size mismatch → ok=false.
    virtual RsParentTable link( const RsSegmentMap &fine,
                                const RsSegmentMap &coarse ) const = 0;
};

/// P1: pixel majority under each fine object.
/// - Skip fine/coarse label 0 (nodata).
/// - Max votes wins; tie → smaller coarse id.
/// - No valid votes → parent 0.
class QGIS_ANALYSIS_EXPORT RsPixelMajorityParentLink : public RsParentLinkStrategy
{
  public:
    RsParentTable link( const RsSegmentMap &fine,
                        const RsSegmentMap &coarse ) const override;
};
