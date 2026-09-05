// src/agent/cartography/chart_registry.h
#pragma once

//
// Charts as first-class map components (Phase L).
//
// ChartSpec JSON (workspace entity + MapSpec component):
//   { id: "chart-1", kind: bar|line|pie|histogram|area|scatter, title,
//     binding: { mode: "inline" | "vector_expression",
//                data: [{label, value}]?                       (inline)
//                layer?: <workspace layer ref|name>,           (vector_expression)
//                x_expression?, y_expression?, filter?, series_name? },
//     style: { palette?: [...], show_legend?: true, font_pt?: 10 },
//     width_px: 480, height_px: 320 }
//
// Binding modes map to the two QGIS-native paths:
//   inline            → ChartRenderer paints a PNG (QPainter, no QtCharts);
//                       compile places it as a picture item.
//   vector_expression → QgsLayoutItemChart (QGIS-native plot: bar/line/pie)
//                       bound to the layer with feature expressions.
//

#include <json/json.h>

#include <QMap>
#include <QMutex>
#include <QString>

#include <string>
#include <vector>

class QgsLayoutItemChart;

namespace sicnu::agent::cartography {

/// Workspace chart entity registry (stable "chart-N" ids).
class ChartRegistry
{
  public:
    static ChartRegistry &instance();

    /// Validates and registers a chart spec; assigns/returns its id.
    /// Returns empty string with *error on invalid specs.
    QString createChart( Json::Value spec, QString *error = nullptr );

    /// Shallow-merges `patch` fields into the chart spec.
    bool updateChart( const QString &id, const Json::Value &patch, QString *error = nullptr );

    bool removeChart( const QString &id );

    Json::Value find( const QString &id ) const; ///< Null when unknown.

    /// All chart specs (bounded consumers: workspace summary caps at 25).
    Json::Value listCharts() const;

    void clear();

  private:
    ChartRegistry() = default;
    mutable QMutex mMutex;
    QMap<QString, Json::Value> mCharts;
};

/// Structural validation of a chart spec (kind, binding, inline data).
/// Empty returned vector = valid.
std::vector<std::string> validateChartSpec( const Json::Value &chart );

/// Renders an inline-data chart spec to a PNG file with plain QPainter.
/// Native kinds (vector_expression) render an explanatory placeholder —
/// the real render happens in QgsLayoutItemChart at layout render time.
bool renderChartToFile( const Json::Value &chart, const QString &path, QString *error = nullptr );

/// Renders a colorbar spec (ramp name + min/max + labels) to a PNG strip.
bool renderColorbarToFile( const Json::Value &colorbar, const QString &path, QString *error = nullptr );

/// Configures a QGIS-native layout chart item from a chart spec whose
/// binding.mode is "vector_expression". False with *error on mismatch.
bool bindNativeChart( QgsLayoutItemChart *item, const Json::Value &chart, QString *error = nullptr );

} // namespace sicnu::agent::cartography
