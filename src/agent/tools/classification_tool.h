// src/agent/tools/classification_tool.h — D15 Package H public seam.
//
// Agent-facing classification diagnosis: JSON in, structured diagnosis out.
// Computes pairwise Jeffries-Matusita separability from (mean, covariance)
// class statistics, excavates the worst-confused pair from a confusion
// matrix and emits prune recommendations for redundant low-gain features.
#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include <span>

namespace rs::agent
{

class ClassificationDiagnosisTool : public QObject
{
    Q_OBJECT
  public:
    explicit ClassificationDiagnosisTool( QObject *parent = nullptr );
    ~ClassificationDiagnosisTool() override = default;

    QString toolName() const { return QStringLiteral( "spatial:classification_diagnosis" ); }

    /// Input schema:
    /// {
    ///   "confusion_matrix": int[][] (row = truth),       // required
    ///   "class_labels":     string[]                     // required, size = matrix dim
    ///   "class_stats": {                                  // optional
    ///     "means": number[][],                            // per class
    ///     "covs":  number[][][],                          // per class, row-major dim x dim
    ///     "gains": number[],                              // optional, per feature
    ///     "feature_names": string[]                       // optional
    ///   }
    /// }
    /// Output schema:
    /// { "status": "OK" | "WARNING" | "ERROR", "message": string,
    ///   "kappa": number, "overall_accuracy": number,
    ///   "worst_pair": { "truth": string, "predicted": string, "count": int },
    ///   "jm_matrix": number[][],                          // when class_stats present
    ///   "code": "WARN_SEVERE_SPECTRAL_CONFUSION"?,        // when kappa < 0.6 or min JM < 1.4
    ///   "recommendations": [ { "action": "PRUNE_FEATURE", "feature_name": string } ... ] }
    QJsonObject execute( const QJsonObject &inputParameters ) const;

    /// Jeffries-Matusita distance 2(1 - e^-B) in [0, 2] between two
    /// Gaussian classes from sufficient statistics (@p cov1/@p cov2 are
    /// row-major dimension x dimension).  Identical distributions -> 0;
    /// Bhattacharyya distance >= 700 saturates at exactly 2.0.
    static double computeJeffriesMatusitaDistance( std::span<const double> mean1,
                                                   std::span<const double> cov1,
                                                   std::span<const double> mean2,
                                                   std::span<const double> cov2,
                                                   int dimension );
};

} // namespace rs::agent
