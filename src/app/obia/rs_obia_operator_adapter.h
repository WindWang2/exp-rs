// rs_obia_operator_adapter.h — Thin OBIA GUI ↔ operator mapping layer (#663).
//
// Pure functions, no widgets and no orchestration: build the JSON parameter
// payloads for the rs:obia_* operators from GUI-side values, and rehydrate
// session state (segment maps, feature stats, labels, accuracy, hierarchy)
// from the operators' file outputs. The window owns task submission,
// progress and presentation; this adapter owns the WHAT (contract mapping),
// so "GUI defaults = operator schema defaults" has one translation point.
#pragma once

#include "rs_object_hierarchy.h"
#include "rs_segment_features.h"
#include "rs_accuracy_assessment.h"

#include <json/json.h>

#include <QColor>
#include <QHash>
#include <QMap>
#include <QString>
#include <QVector>

namespace RsObiaOperatorAdapter
{

/// Segmentation options as the GUI collects them (defaults mirror the
/// rs:obia_segment schema; the window initializes widgets from the schema).
struct SegmentOptions
{
    QString rasterPath;
    QString outputLabelsPath;
    QString engine = QStringLiteral( "auto" ); // simple | otb | auto
    int smoothKernel = 5;
    int quantizeBins = 32;
    int minRegionSize = 50;
    int spatialRadius = 5;
    double rangeRadius = 15.0;
    int maxIterations = 100;
    double threshold = 0.1;
};

/// Classifier options shared by the flat and hierarchy classify flows
/// (defaults mirror the rs:obia_classify schema / factory struct).
struct ClassifierOptions
{
    QString method = QStringLiteral( "svm" );
    int rfNumTrees = 100;
    int rfMaxDepth = 10;
    int rfMinSampleCount = 5;
    int mlpHiddenLayerSize = 16;
    int mlpMaxIter = 500;
    bool scale = true;
};

// ---- Parameter builders (WHAT to execute) ----

Json::Value buildSegmentParams( const SegmentOptions &opts );

Json::Value buildFeaturesParams( const QString &rasterPath,
                                 const QString &labelsPath,
                                 const QString &outputCsvPath );

Json::Value buildLabelParams( const QString &rasterPath,
                              const QString &labelsPath,
                              const QString &trainingPath,
                              const QString &classField,
                              int minLabelPixels );

Json::Value buildFlatClassifyParams( const QString &rasterPath,
                                     const QString &labelsPath,
                                     const QString &outputPath,
                                     const QMap<quint32, int> &segmentClasses,
                                     const ClassifierOptions &classifier,
                                     const RsFeatureSelection &featureSelection,
                                     const QHash<int, QColor> &classColors,
                                     const QString &outputUncertaintyPath );

Json::Value buildHierarchyBuildParams( const QString &rasterPath,
                                       const QString &outputFinePath,
                                       const QString &outputCoarsePath,
                                       const QString &outputParentsPath,
                                       int spatialRadius,
                                       double rangeRadius,
                                       int minRegionSize,
                                       double watershedThreshold );

Json::Value buildHierarchyClassifyParams( const QString &rasterPath,
                                          const QString &labelsFinePath,
                                          const QString &labelsCoarsePath,
                                          const QString &parentsPath,
                                          const QString &outputPath,
                                          int classifyLevel,
                                          const QMap<quint32, int> &segmentClasses,
                                          const ClassifierOptions &classifier,
                                          const QHash<int, QColor> &classColors,
                                          const QString &outputUncertaintyPath );

Json::Value buildPolygonizeParams( const QString &classRasterPath,
                                   const QString &outputVectorPath );

// ---- Value → JSON fragments ----

Json::Value segmentClassesJson( const QMap<quint32, int> &segmentClasses );
Json::Value classColorsJson( const QHash<int, QColor> &classColors );
Json::Value featureSelectionJson( const RsFeatureSelection &selection );

/// Toolbar classifier label ("NormalBayes"/"SVM"/"RandomForest"/"KMeans"/"MLP")
/// → rs:obia_classify `method` value.
QString methodForClassifierLabel( const QString &label );

// ---- Output rehydration (operator files → session state) ----

/// Parse an rs:obia_features CSV back into per-segment stats.
/// Column lookup is by header name; returns false with \a error on a
/// malformed file.
bool parseFeaturesCsv( const QString &csvPath,
                       QMap<quint32, RsSegmentFeatures::SegmentStat> &stats,
                       QString *error = nullptr );

/// Parse an rs:obia_label CSV (segment_id,class_id).
bool parseSegmentClassesCsv( const QString &csvPath,
                             QMap<quint32, int> &segmentClasses,
                             QString *error = nullptr );

/// Parse an rs:obia_classify / rs:obia_hierarchy uncertainty CSV
/// (segment_id, entropy, class_id).
bool parseUncertaintyCsv( const QString &csvPath,
                          QMap<quint32, double> &uncertainties,
                          QMap<quint32, int> &predictedClasses,
                          QString *error = nullptr );

/// Result-payload accuracy object → RsAccuracyAssessment::Result.
bool parseAccuracyJson( const Json::Value &accuracy,
                        RsAccuracyAssessment::Result &result );

/// Rebuild an RsObjectHierarchy from the label GeoTIFFs (+ parents CSV)
/// written by rs:obia_hierarchy's build mode.
bool rehydrateHierarchy( const QString &finePath,
                         const QString &coarsePath,
                         const QString &parentsPath,
                         RsObjectHierarchy &hierarchy,
                         QString *error = nullptr );

} // namespace RsObiaOperatorAdapter
