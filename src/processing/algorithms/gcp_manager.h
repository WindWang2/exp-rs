// gcp_manager.h — D14 Package A: ground control point (GCP) management and
// spatial distribution analytics (ADR 0159).
//
// Pure domain object: no signals, no I/O side effects beyond explicit
// load/save calls. All metrics are computed on demand over *active*
// (enabled) points in source-image pixel space.
#pragma once

#include <QJsonValue>
#include <QString>

#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace rs::core {

struct GcpPoint {
    QString id;
    double sourceX{0.0};
    double sourceY{0.0};
    double targetX{0.0};
    double targetY{0.0};
    double residualX{0.0};
    double residualY{0.0};
    double residualTotal{0.0};
    bool enabled{true};
};

struct GcpDistributionMetrics {
    int activeCount{0};
    double convexHullArea{0.0};               // Shoelace area of the source-space hull
    double coverageRatio{0.0};                // convexHullArea / (imageWidth * imageHeight)
    double meanNearestNeighborDist{0.0};      // Observed mean distance r̄_A
    double expectedNearestNeighborDist{0.0};  // Expected mean distance r̄_E
    double clarkEvansIndex{0.0};              // R = r̄_A / r̄_E (0 when undefined)
    double maxDelaunayAspectRatio{0.0};       // Worst triangle R_circum / (2·r_in)
    double globalRmse{0.0};                   // sqrt(mean(Δx²+Δy²)) over active points
};

class GcpManager {
  public:
    GcpManager() = default;
    ~GcpManager() = default;

    // Point CRUD. addPoint rejects empty/duplicate ids and non-finite
    // coordinates; updatePoint keeps the id fixed and validates coordinates.
    bool addPoint(const GcpPoint& pt);
    bool removePoint(const QString& id);
    bool updatePoint(const GcpPoint& pt);
    void setPointEnabled(const QString& id, bool enabled);
    void clear();

    [[nodiscard]] std::vector<GcpPoint> activePoints() const;
    [[nodiscard]] std::vector<GcpPoint> allPoints() const;
    [[nodiscard]] std::optional<GcpPoint> findPoint(const QString& id) const;
    [[nodiscard]] size_t size() const noexcept;
    [[nodiscard]] size_t activeCount() const noexcept;

    // Spatial distribution analysis (source-image pixel space).
    // imageWidth/imageHeight must be > 0; with fewer than 3 active points the
    // hull area and coverage ratio are exactly 0; the Clark-Evans index is 0
    // when fewer than 2 active points or a degenerate image area.
    [[nodiscard]] GcpDistributionMetrics evaluateDistribution(double imageWidth,
                                                              double imageHeight) const;
    [[nodiscard]] std::vector<std::pair<double, double>> computeConvexHull() const;
    [[nodiscard]] std::vector<std::tuple<QString, QString, QString>> computeDelaunayTriangles() const;

    // Residual bookkeeping. transformedCoords must hold exactly one (x, y)
    // pair per active point, in activePoints() order; any other size is a
    // no-op. Each residual total is sqrt(Δx²+Δy²).
    void updateResiduals(const std::vector<std::pair<double, double>>& transformedCoords);
    [[nodiscard]] double computeGlobalRmse() const;

    // Persistence. CSV columns:
    //   id,source_x,source_y,target_x,target_y,residual_x,residual_y,residual_total,enabled
    // Malformed rows are skipped on load. JSON is a {"gcps": [...]} envelope
    // with snake_case keys; fromJson returns false on any parse error.
    bool loadFromCsv(const QString& filePath);
    bool saveToCsv(const QString& filePath) const;
    [[nodiscard]] QString toJson() const;
    bool fromJson(const QString& jsonStr);

  private:
    std::vector<GcpPoint> mPoints;
};

} // namespace rs::core
