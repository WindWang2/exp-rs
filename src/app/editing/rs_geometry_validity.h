// rs_geometry_validity.h — F11 Package C: geometry validity reporting.
//
// Contract (Oracle O2: validity problems are never swallowed):
//   * validate() reports structured issues; it never mutates anything.
//   * repairPreview() returns a makeValid CANDIDATE; applying it is always
//     the caller's explicit decision (through an edit command). There is no
//     API here that writes to a layer.
// Prefer QGIS internal validation (no GEOS-version-dependent messages in
// our contract tests); callers may select the GEOS engine explicitly.
#pragma once

#include <QVector>
#include <QString>

#include <qgis.h>

class QgsGeometry;

struct RsValidityIssue
{
    QString message;
    bool hasLocation = false;
    double x = 0.0;
    double y = 0.0;
};

class RsGeometryValidity
{
  public:
    /// Validate \a geometry; empty vector = valid.
    static QVector<RsValidityIssue> validate(
      const QgsGeometry &geometry,
      Qgis::GeometryValidationEngine engine = Qgis::GeometryValidationEngine::QgisInternal );

    /// True when the geometry has no reported issues (fast path identical to
    /// validate().isEmpty(), kept for readability at call sites).
    static bool isValid( const QgsGeometry &geometry,
                         Qgis::GeometryValidationEngine engine = Qgis::GeometryValidationEngine::QgisInternal );

    /// Build a repair candidate via makeValid WITHOUT applying it anywhere.
    /// \a ok reports whether makeValid produced a result (false for null
    /// input or an engine failure). The candidate still needs explicit
    /// validation — makeValid output is expected but not guaranteed valid.
    static QgsGeometry repairPreview( const QgsGeometry &geometry,
                                      bool keepCollapsed = false,
                                      bool *ok = nullptr,
                                      Qgis::MakeValidMethod method = Qgis::MakeValidMethod::Linework );
};
