#pragma once

#include <QObject>
#include <QList>
#include <QAction>

class QgsMapCanvas;
class QgisDesktopWindow;
class QgsMapTool;
class QgsMapToolPan;
class QgsMapToolZoom;
class CustomIdentifyTool;
class MeasureTool;
class QgsMapToolSelect;
class QgsMapToolAddFeature;
class QgsMapToolMoveFeature;
class QgsMapToolRotateFeature;
class QgsMapToolScaleFeature;
class QgsMapToolOffsetCurve;
class QgsMapToolReshape;
class QgsMapToolSplitFeatures;
class QgsMapToolSplitParts;
class QgsMapToolSimplify;
class QgsMapToolReverseLine;
class QgsMapToolAddRing;
class QgsMapToolAddPart;
class QgsMapToolFillRing;
class QgsMapToolDeletePart;
class QgsMapToolDeleteRing;
class QgsMapToolTrimExtendFeature;
class QgsMapToolChamferFillet;
class QgsMapToolFeatureArray;
class QgsVertexTool;
class QgsAdvancedDigitizingDockWidget;

class MapToolManager : public QObject
{
    Q_OBJECT
public:
    explicit MapToolManager(QgisDesktopWindow *mainWindow, QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock);
    ~MapToolManager() override;

    void setupTools();

    // Map tool getters
    QgsMapToolPan *panTool() const { return m_panTool; }
    QgsMapToolZoom *zoomInTool() const { return m_zoomInTool; }
    QgsMapToolZoom *zoomOutTool() const { return m_zoomOutTool; }
    CustomIdentifyTool *identifyTool() const { return m_identifyTool; }
    MeasureTool *measureDistanceTool() const { return m_measureDistanceTool; }
    MeasureTool *measureAreaTool() const { return m_measureAreaTool; }

    QgsMapToolSelect *selectTool() const { return m_selectTool; }
    QgsMapToolAddFeature *addFeatureTool() const { return m_addFeatureTool; }
    QgsMapToolMoveFeature *moveFeatureTool() const { return m_moveFeatureTool; }
    QgsMapToolRotateFeature *rotateFeatureTool() const { return m_rotateFeatureTool; }
    QgsMapToolScaleFeature *scaleFeatureTool() const { return m_scaleFeatureTool; }
    QgsMapToolOffsetCurve *offsetCurveTool() const { return m_offsetCurveTool; }
    QgsMapToolReshape *reshapeTool() const { return m_reshapeTool; }
    QgsMapToolSplitFeatures *splitFeaturesTool() const { return m_splitFeaturesTool; }
    QgsMapToolSplitParts *splitPartsTool() const { return m_splitPartsTool; }
    QgsMapToolSimplify *simplifyTool() const { return m_simplifyTool; }
    QgsMapToolReverseLine *reverseLineTool() const { return m_reverseLineTool; }
    QgsMapToolAddRing *addRingTool() const { return m_addRingTool; }
    QgsMapToolAddPart *addPartTool() const { return m_addPartTool; }
    QgsMapToolFillRing *fillRingTool() const { return m_fillRingTool; }
    QgsMapToolDeletePart *deletePartTool() const { return m_deletePartTool; }
    QgsMapToolDeleteRing *deleteRingTool() const { return m_deleteRingTool; }
    QgsMapToolTrimExtendFeature *trimExtendTool() const { return m_trimExtendTool; }
    QgsMapToolChamferFillet *chamferFilletTool() const { return m_chamferFilletTool; }
    QgsMapToolFeatureArray *featureArrayTool() const { return m_featureArrayTool; }
    QgsVertexTool *vertexTool() const { return m_vertexTool; }

private:
    QgisDesktopWindow *m_mainWindow = nullptr;
    QgsMapCanvas *m_canvas = nullptr;
    QgsAdvancedDigitizingDockWidget *m_cadDock = nullptr;

    QgsMapToolPan *m_panTool = nullptr;
    QgsMapToolZoom *m_zoomInTool = nullptr;
    QgsMapToolZoom *m_zoomOutTool = nullptr;
    CustomIdentifyTool *m_identifyTool = nullptr;
    MeasureTool *m_measureDistanceTool = nullptr;
    MeasureTool *m_measureAreaTool = nullptr;

    QgsMapToolSelect *m_selectTool = nullptr;
    QgsMapToolAddFeature *m_addFeatureTool = nullptr;
    QgsMapToolMoveFeature *m_moveFeatureTool = nullptr;
    QgsMapToolRotateFeature *m_rotateFeatureTool = nullptr;
    QgsMapToolScaleFeature *m_scaleFeatureTool = nullptr;
    QgsMapToolOffsetCurve *m_offsetCurveTool = nullptr;
    QgsMapToolReshape *m_reshapeTool = nullptr;
    QgsMapToolSplitFeatures *m_splitFeaturesTool = nullptr;
    QgsMapToolSplitParts *m_splitPartsTool = nullptr;
    QgsMapToolSimplify *m_simplifyTool = nullptr;
    QgsMapToolReverseLine *m_reverseLineTool = nullptr;
    QgsMapToolAddRing *m_addRingTool = nullptr;
    QgsMapToolAddPart *m_addPartTool = nullptr;
    QgsMapToolFillRing *m_fillRingTool = nullptr;
    QgsMapToolDeletePart *m_deletePartTool = nullptr;
    QgsMapToolDeleteRing *m_deleteRingTool = nullptr;
    QgsMapToolTrimExtendFeature *m_trimExtendTool = nullptr;
    QgsMapToolChamferFillet *m_chamferFilletTool = nullptr;
    QgsMapToolFeatureArray *m_featureArrayTool = nullptr;
    QgsVertexTool *m_vertexTool = nullptr;

    // Store all tools for cleanup
    QList<QgsMapTool*> m_allTools;
};
