#include "map_tool_manager.h"
#include "core/sicnu_logging.h"

#include <qgsmapcanvas.h>
#include <qgsmaptoolpan.h>
#include <qgsmaptoolzoom.h>
#include <qgsmaptoolidentify.h>
#include <qgsproject.h>

// Vector editing map tools
#include "qgsmaptooladdfeature.h"
#include "qgsmaptooladdpart.h"
#include "qgsmaptooladdring.h"
#include "qgsmaptoolmovefeature.h"
#include "qgsmaptoolrotatefeature.h"
#include "qgsmaptoolscalefeature.h"
#include "qgsmaptooloffsetcurve.h"
#include "qgsmaptoolreshape.h"
#include "qgsmaptoolsplitfeatures.h"
#include "qgsmaptoolsplitparts.h"
#include "qgsmaptoolsimplify.h"
#include "qgsmaptoolreverseline.h"
#include "qgsmaptoolfillring.h"
#include "qgsmaptooldeletepart.h"
#include "qgsmaptooldeletering.h"
#include "qgsmaptooltrimextendfeature.h"
#include "qgsmaptoolchamferfillet.h"
#include "qgsmaptoolfeaturearray.h"
#include "selecttools/qgsmaptoolselect.h"
#include "vertextool/qgsvertextool.h"

#include "app/main_window.h"
#include "map_tools/measure_tool.h"

MapToolManager::MapToolManager(QgisDesktopWindow *mainWindow, QgsMapCanvas *canvas, QgsAdvancedDigitizingDockWidget *cadDock)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
    , m_canvas(canvas)
    , m_cadDock(cadDock)
{
}

MapToolManager::~MapToolManager()
{
    // Clean up all map tools that weren't taken by the canvas
    for (QgsMapTool *tool : m_allTools) {
        // Only delete if not currently set on canvas
        if (m_canvas && m_canvas->mapTool() != tool) {
            delete tool;
        }
    }
    m_allTools.clear();
}

void MapToolManager::setupTools()
{
    SICNU_LOG_INFO( SicnuLogTags::MapTools, "Initializing map tools" );

    m_panTool = new QgsMapToolPan(m_canvas);
    m_zoomInTool = new QgsMapToolZoom(m_canvas, false);
    m_zoomOutTool = new QgsMapToolZoom(m_canvas, true);
    m_identifyTool = new CustomIdentifyTool(m_canvas);
    m_measureDistanceTool = new MeasureTool(m_canvas, MeasureTool::Distance, m_mainWindow);
    m_measureAreaTool = new MeasureTool(m_canvas, MeasureTool::Area, m_mainWindow);

    // Vector editing map tools
    m_selectTool = new QgsMapToolSelect(m_canvas);
    m_addFeatureTool = new QgsMapToolAddFeature(m_canvas, m_cadDock, QgsMapToolCapture::CaptureNone);
    m_moveFeatureTool = new QgsMapToolMoveFeature(m_canvas);
    m_rotateFeatureTool = new QgsMapToolRotateFeature(m_canvas);
    m_scaleFeatureTool = new QgsMapToolScaleFeature(m_canvas);
    m_offsetCurveTool = new QgsMapToolOffsetCurve(m_canvas);
    m_reshapeTool = new QgsMapToolReshape(m_canvas);
    m_splitFeaturesTool = new QgsMapToolSplitFeatures(m_canvas);
    m_splitPartsTool = new QgsMapToolSplitParts(m_canvas);
    m_simplifyTool = new QgsMapToolSimplify(m_canvas);
    m_reverseLineTool = new QgsMapToolReverseLine(m_canvas);
    m_addRingTool = new QgsMapToolAddRing(m_canvas);
    m_addPartTool = new QgsMapToolAddPart(m_canvas);

    // Store all tools for cleanup
    m_allTools = {
        m_panTool, m_zoomInTool, m_zoomOutTool, m_identifyTool,
        m_measureDistanceTool, m_measureAreaTool,
        m_selectTool, m_addFeatureTool, m_moveFeatureTool,
        m_rotateFeatureTool, m_scaleFeatureTool, m_offsetCurveTool,
        m_reshapeTool, m_splitFeaturesTool, m_splitPartsTool,
        m_simplifyTool, m_reverseLineTool, m_addRingTool, m_addPartTool
    };
    m_fillRingTool = new QgsMapToolFillRing(m_canvas);
    m_deletePartTool = new QgsMapToolDeletePart(m_canvas);
    m_deleteRingTool = new QgsMapToolDeleteRing(m_canvas);
    m_trimExtendTool = new QgsMapToolTrimExtendFeature(m_canvas);
    m_chamferFilletTool = new QgsMapToolChamferFillet(m_canvas);
    m_featureArrayTool = new QgsMapToolFeatureArray(m_canvas);
    m_vertexTool = new QgsVertexTool(m_canvas, m_cadDock);

    SICNU_LOG_SUCCESS( SicnuLogTags::MapTools, QString( "Map tools initialized: pan, zoom, identify, measure, %1 editing tools" )
        .arg( 19 ) ); // number of vector editing tools created
}
