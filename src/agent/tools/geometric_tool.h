// geometric_tool.h — D14 Package H: agent-facing geometric registration tool
// (ADR 0159), extended by F13 (multimodal registration evidence surface).
//
// Every response uses the platform envelope
//   { "success": bool, "action": string, "data": object,
//     "diagnostic_message": string }
// with snake_case keys. No method ever throws across the execute() boundary.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace rs::agent {

class GeometricTool {
  public:
    GeometricTool() = default;
    ~GeometricTool() = default;

    [[nodiscard]] QString toolName() const { return QStringLiteral("spatial:geometric_registration"); }
    [[nodiscard]] QString toolDescription() const;
    [[nodiscard]] QJsonObject parameterSchema() const;

    /// Primary invocation interface for the agent LLM. Dispatches on
    /// params["action"] in {audit_residuals, recommend_model,
    /// inspect_misalignment, multimodal_register, select_model,
    /// stack_register}; unknown/missing actions yield a structured error
    /// envelope, never an exception.
    QJsonObject execute(const QJsonObject& params);

    /// Autonomous misalignment pre-check: feature-matches the two rasters
    /// and reports inlier ratio / reprojection RMSE / estimated shift.
    QJsonObject inspectMisalignment(const QString& sourceImagePath, const QString& refImagePath);

    /// Model recommender decision tree (spec D14 §H).
    QJsonObject recommendOptimalModel(int activeGcpCount, double terrainRoughness,
                                      double coverageRatio);

    /// 3-sigma gross-blunder audit over a JSON array of GCP objects
    /// ({id, residual_x, residual_y[, residual_total]}).
    QJsonObject auditGcpResiduals(const QJsonArray& gcpArray, double rmseThreshold);

    /// F13: cross-modal (optical-SAR) tie-point matching with refusal
    /// semantics and a quality block (CE90, residual field, coverage).
    /// Structural shortfalls return success=true with data.status
    /// refused/low_confidence and a machine-readable reason — the caller
    /// must treat those as "no trustworthy alignment", not as tool failure.
    QJsonObject multimodalRegister(const QString& sourceImagePath, const QString& refImagePath,
                                   const QString& metric, int windowSize, int searchRadius);

    /// F13: evidence-driven model selection over correspondences
    /// ({source_x, source_y, target_x, target_y}) with the per-candidate
    /// held-out evidence table attached for explainability.
    QJsonObject selectModel(const QJsonArray& gcpArray, int folds, double minImprovement);

    /// F13: multi-scene global translation adjustment over pairwise
    /// observations ({from_id, to_id, tx, ty[, confidence, inlier_count]}).
    QJsonObject stackRegister(const QJsonArray& sceneIds, const QJsonArray& observations,
                              const QString& referenceId);
};

} // namespace rs::agent
