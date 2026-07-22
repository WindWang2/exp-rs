// rs_object_hierarchy.h — Hierarchical OBIA V1: N flat maps + parent tables.
//
// Level 0 = finest, level L-1 = coarsest. Parent tables link adjacent levels
// (edge i connects level i → level i+1). Child indices are reverse-derived.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_parent_link.h"
#include "rs_segment_map.h"
#include "rs_segmenter_port.h"

#include <QMap>
#include <QString>
#include <QVector>

#include <functional>

class QGIS_ANALYSIS_EXPORT RsObjectHierarchy
{
  public:
    RsObjectHierarchy() = default;

    void clear();
    bool isEmpty() const { return mLevels.isEmpty(); }
    int levelCount() const { return mLevels.size(); }

    /// Level i segment map (0 = finest). Empty map if out of range.
    const RsSegmentMap &level( int i ) const;

    /// Parent table linking fineLevel → fineLevel+1 (keys = fine ids).
    /// Empty map if out of range.
    const QMap<quint32, quint32> &parentTable( int fineLevel ) const;

    /// Parent of fineId at fineLevel (looks at edge fineLevel → fineLevel+1).
    /// Returns 0 if orphan, unknown, or no coarser level.
    quint32 parentOf( int fineLevel, quint32 fineId ) const;

    /// Children of coarseId at coarseLevel (from edge coarseLevel-1 → coarseLevel).
    /// Empty if no finer level or id unknown.
    QVector<quint32> childrenOf( int coarseLevel, quint32 coarseId ) const;

    /// Number of children at level-1 (0 when level == 0 or no children).
    int childCount( int level, quint32 segmentId ) const;

    /// area(fine) / area(parent); 0 when parent missing or area parent == 0.
    double areaRatioToParent( int level, quint32 segmentId ) const;

    /// Install fixture levels + parent tables (no segmenter).
    /// levels.size() >= 1; parentTables.size() == levels.size()-1.
    /// All levels must share the same width/height. Fails hard on mismatch.
    bool setLevels( QVector<RsSegmentMap> levels,
                    QVector<RsParentTable> parentTables,
                    QString *error = nullptr );

    /// Segment each level via injected segmenter, then link adjacent levels.
    /// On any level failure the hierarchy is left cleared and false is returned.
    bool buildLevels( const QString &rasterPath,
                      const QVector<RsLevelSpec> &levelSpecs,
                      RsSegmenterPort &segmenter,
                      const RsParentLinkStrategy &linker,
                      QString *error = nullptr,
                      const std::function<bool()> &isCanceled = nullptr );

    /// Re-link only edges that touch changedLevel (i-1→i and i→i+1 when present).
    /// Requires levels already populated. Documents V1 re-segment guidance.
    bool relinkEdgesTouching( int changedLevel,
                              const RsParentLinkStrategy &linker,
                              QString *error = nullptr );

  private:
    void rebuildChildrenIndex();
    bool validateGridSizes( QString *error ) const;

    QVector<RsSegmentMap> mLevels;
    /// mParents[i] links level i → level i+1
    QVector<QMap<quint32, quint32>> mParents;
    /// mChildren[i] is reverse of mParents[i]: coarseId → fineIds at level i
    QVector<QMap<quint32, QVector<quint32>>> mChildren;

    static const RsSegmentMap sEmptyMap;
    static const QMap<quint32, quint32> sEmptyParentTable;
};
