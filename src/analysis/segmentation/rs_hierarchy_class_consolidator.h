// rs_hierarchy_class_consolidator.h — OBIA Multi-scale class consistency consolidation.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_object_hierarchy.h"
#include <QMap>
#include <QVector>

enum class RsConsolidationMode
{
  BottomUpMajorityVote,    /// Child segments vote to determine parent segment class.
  TopDownInheritance,       /// Parent segment class overrides child segment classes.
  ProbabilityWeightedVote  /// Child segments vote weighted by area to determine parent class.
};

class QGIS_ANALYSIS_EXPORT RsHierarchyClassConsolidator
{
  public:
    /// Consolidates multi-level classification maps across the hierarchy.
    /// levelSegmentClasses: levelIndex -> (segmentId -> classId).
    /// Returns updated levelSegmentClasses ensuring parent-child class consistency.
    static QMap<int, QMap<quint32, int>> consolidate(
      const RsObjectHierarchy &hierarchy,
      const QMap<int, QMap<quint32, int>> &levelSegmentClasses,
      RsConsolidationMode mode = RsConsolidationMode::BottomUpMajorityVote );
};
