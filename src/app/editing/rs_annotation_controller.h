// rs_annotation_controller.h — F11 Package B: app-level annotation surface.
//
// The vendored gui library compiles the full annotation toolset
// (create/modify/select) but nothing in the app consumed it (baseline audit:
// zero QgsAnnotation references in src/app). This controller provides:
//   * ownership of the project's single QgsAnnotationLayer (created lazily
//     in the project CRS, added to the project so project-remove semantics
//     are uniform with vector layers);
//   * programmatic item creation (point text / marker) — explicit calls,
//     never silent mutation;
//   * optional interactive tools (vendored create-point-text / modify /
//     select map tools) for hosting by the map tool manager.
// Annotation edits do not go through QgsVectorLayer edit buffers; the
// annotation layer has no undo buffer — undo for annotations is explicitly
// not-supported in this slice (documented in CAPABILITY_MATRIX).
#pragma once

#include <QPointer>
#include <QString>

#include <qgsgeometry.h>
#include <qgspointxy.h>

class QgsAnnotationLayer;
class QgsMapCanvas;
class QgsMapTool;
class QgsProject;
class QgsAdvancedDigitizingDockWidget;

class RsAnnotationController : public QObject
{
    Q_OBJECT

  public:
    explicit RsAnnotationController( QgsProject *project, QObject *parent = nullptr );

    /// The project's annotation layer, creating and adding it on first call.
    /// Returns null only if the project rejects the layer.
    QgsAnnotationLayer *annotationLayer();

    /// True when the annotation layer exists in the project.
    bool hasAnnotationLayer() const { return !mLayer.isNull(); }

    /// Add a point-text annotation at \a point (project CRS). Returns the
    /// item id, or an empty string when refused (no layer / empty text is
    /// allowed but a null layer is not).
    QString addPointText( const QString &text, const QgsPointXY &point, double sizePt = 14.0 );

    /// Add a marker annotation at \a point (project CRS). Returns item id.
    QString addMarker( const QgsPointXY &point );

    /// Remove one item; false when the layer is absent or id unknown.
    bool removeItem( const QString &itemId );

    /// Number of items (0 when no layer).
    int itemCount() const;

    /// Interactive tools (ownership: this controller). \a cadDock may be
    /// null in headless contexts; the create tool requires it though.
    QgsMapTool *createPointTextTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock );
    QgsMapTool *modifyTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock );
    QgsMapTool *selectTool( QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock );

  private:
    QPointer<QgsProject> mProject;
    QPointer<QgsAnnotationLayer> mLayer;
};
