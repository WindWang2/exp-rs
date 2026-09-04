/***************************************************************************
 * rs_obia_common.h  —  shared internal helpers for the rs:obia_* operators
 *
 * Parameter parsing / result assembly pieces that are identical across
 * rs:obia_classify and rs:obia_hierarchy (issue #663 convergence): the
 * object-or-string JSON param convention, classifier hyperparameters
 * (RsClassifierBackendParams is the single default source), class palettes,
 * training-set accuracy serialization and the entropy sidecar writer.
 ***************************************************************************/
#pragma once

#include "analysis/classification/rs_accuracy_assessment.h"
#include "analysis/classification/rs_classifier_backend_factory.h"

#include "operators/framework/rs_operator.h"

#include <QColor>
#include <QHash>
#include <QMap>
#include <QString>

#include <string>

namespace sicnu::operators::rs {
namespace obia {

/// Structured params reach operators as real JSON objects (GUI adapter,
/// programmatic callers) or as JSON-encoded strings (CLI/pipeline JSON files
/// where nested objects are quoted). Returns an empty object when absent.
Json::Value objectParam(const Json::Value& params, const std::string& key);

/// True when `key` holds a non-empty object (or non-empty string encoding one).
bool hasObjectParam(const Json::Value& params, const std::string& key);

/// Strict integer key parse (JSON object keys are strings).
/// Throws RSOperatorError(InvalidParameter) on non-numeric keys.
int intKey(const std::string& key, const std::string& paramName);

/// Parse classifier hyperparameters (absent fields keep the
/// RsClassifierBackendParams defaults). Throws InvalidParameter on values <= 0.
RsClassifierBackendParams classifierHyperParams(const Json::Value& params);

/// {classId: "#rrggbb"} → palette hash. Throws InvalidParameter on bad entries.
QHash<int, QColor> parseClassColors(const Json::Value& colors);

/// Training-set accuracy → result JSON (overallAccuracy, kappa, classes,
/// confusion rows, producer/user/f1 keyed by class id).
Json::Value accuracyToJson(const RsAccuracyAssessment::Result& acc);

/// Training-set accuracy from true/predicted label pairs (empty Result when
/// the pairs are empty).
RsAccuracyAssessment::Result trainingAccuracy(const QMap<quint32, int>& trainLabels,
                                              const QMap<quint32, int>& segmentClasses);

/// Write the per-segment entropy CSV sidecar (segment_id, entropy, class_id —
/// class_id is the predicted class, 0 = not predicted). Throws FileNotWritable
/// on failure.
void writeUncertaintyCsv(const std::string& path,
                         const QMap<quint32, double>& uncertainties,
                         const QMap<quint32, int>& segmentClasses);

} // namespace obia
} // namespace sicnu::operators::rs
