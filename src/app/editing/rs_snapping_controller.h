// rs_snapping_controller.h — F11 Package C: snapping configuration authority.
//
// The app never configured canvas snapping (baseline audit: zero references
// to QgsSnappingUtils in src/app before this track). This controller is the
// single writer of the canvas's QgsSnappingUtils config — there is exactly
// one snapping engine (the canvas's own), we only configure and report it.
//
// Design: configure-then-read-back. All setters return void but every value
// is readable via the corresponding getter (used by tests and by the
// editing:state agent surface). Values live on QgsSnappingConfig owned by
// the canvas's QgsSnappingUtils — this class never duplicates state.
#pragma once

#include <QObject>
#include <QPointer>

#include <qgssnappingutils.h>

class QgsMapCanvas;

class RsSnappingController : public QObject
{
    Q_OBJECT

  public:
    explicit RsSnappingController( QgsMapCanvas *canvas, QObject *parent = nullptr );

    /// Enable vertex+segment snapping on the active layer with the given
    /// tolerance (interpreted in \a mapUnits) and intersection snapping.
    /// This is the platform default digitizing profile.
    void enableVertexSegment( double tolerance, Qgis::MapToolUnit mapUnits, bool intersectionSnapping );

    /// Raw config access for advanced callers; the controller stays the
    /// single writer in app code.
    QgsSnappingUtils *snappingUtils() const { return mUtils.data(); }

    // Read-back facts (agent/test surface).
    bool enabled() const;
    Qgis::SnappingTypes types() const;
    double tolerance() const;
    Qgis::MapToolUnit units() const;
    Qgis::SnappingMode mode() const;
    bool intersectionSnapping() const;

  signals:
    void snappingChanged();

  private:
    QPointer<QgsMapCanvas> mCanvas;
    QPointer<QgsSnappingUtils> mUtils;
};
