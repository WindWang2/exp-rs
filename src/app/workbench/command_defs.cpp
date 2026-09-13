/***************************************************************************
 * command_defs.cpp — capability → definition wiring (batch 1)
 ***************************************************************************/
#include "command_defs.h"

#include "command_registry.h"
#include "main_window.h"
#include "workflow/pipeline_editor_dock.h"
#include "shell/workflow_session_controller.h"
#include "workbench_host.h"

#include "dialogs/extract_band_dialog.h"

#include <qgsmapcanvas.h>
#include <qgsrasterlayer.h>

#include <functional>

using sicnu::app::CommandDefinition;
using sicnu::app::SelectionContextSnapshot;
namespace ContextRules = sicnu::app::ContextRules;

namespace
{

CommandDefinition base( const char *id, const QString &title, const QString &description,
                        const QString &icon, const QString &category )
{
    CommandDefinition def;
    def.id = QString::fromUtf8( id );
    def.title = title;
    def.description = description;
    def.iconName = icon;
    def.category = category;
    return def;
}

/// Standard edit-includes for brevity below.
#define RS_CMD( var, id, title, desc, icon, category ) \
    CommandDefinition var = base( id, title, desc, icon, category )

} // namespace

void registerShellCommands( sicnu::app::CommandRegistry *registry, QgisDesktopWindow *window )
{
    if ( !registry || !window )
        return;

    // ── 工程 Project ──────────────────────────────────────────────────
    {
        RS_CMD( d, "project.new", QObject::tr( "New Project" ),
                QObject::tr( "Create an empty project, clearing current layers and view state." ),
                "new_project", QObject::tr( "Project" ) );
        d.shortcut = QKeySequence::New;
        d.handler = [window] { window->newProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.open", QObject::tr( "Open Project..." ),
                QObject::tr( "Opens a saved project file." ),
                "o_en", QObject::tr( "Project" ) );
        d.shortcut = QKeySequence::Open;
        d.handler = [window] { window->openProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.save", QObject::tr( "Save Project" ),
                QObject::tr( "Saves the current project to its existing path." ),
                "s_ve", QObject::tr( "Project" ) );
        d.shortcut = QKeySequence::Save;
        d.handler = [window] { window->saveProject(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.saveAs", QObject::tr( "Save Project As..." ),
                QObject::tr( "Saves the project as a new file." ),
                "ex_ort", QObject::tr( "Project" ) );
        d.handler = [window] { window->saveProjectAs(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.importLayer", QObject::tr( "Import Layer..." ),
                QObject::tr( "Import raster or vector layers into the project." ),
                "i_ort", QObject::tr( "Project" ) );
        d.handler = [window] { window->importLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.stacBrowse", QObject::tr( "Browse STAC Catalog..." ),
                QObject::tr( "Browses STAC catalogs to find remote-sensing data." ),
                "cloud_sync", QObject::tr( "Project" ) );
        d.handler = [window] { window->browseStacCatalog(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.newLayout", QObject::tr( "New Layout..." ),
                QObject::tr( "Create a print layout / map product." ),
                "print_l_yout", QObject::tr( "Project" ) );
        // The layout bench opens the same designer — keep the workbench id
        // truthful when the surface goes through the command (review L #8).
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "layout" ) );
            else
                window->newLayout();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "project.exit", QObject::tr( "Exit" ),
                QObject::tr( "Quits the application." ), QString(), QObject::tr( "Project" ) );
        d.shortcut = QKeySequence::Quit;
        d.destructive = true;
        d.handler = [window] { window->close(); };
        registry->registerCommand( d );
    }

    // ── 图层 Layer ────────────────────────────────────────────────────
    {
        RS_CMD( d, "layer.addRaster", QObject::tr( "Add Raster Layer..." ),
                QObject::tr( "Add a raster layer from a file." ), "r_ster", QObject::tr( "Layer" ) );
        d.handler = [window] { window->addRasterLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.addVector", QObject::tr( "Add Vector Layer..." ),
                QObject::tr( "Add a vector layer from a file." ), "vector", QObject::tr( "Layer" ) );
        d.handler = [window] { window->addVectorLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.properties", QObject::tr( "Layer Properties..." ),
                QObject::tr( "Opens the current layer properties." ), "met_d_t_", QObject::tr( "Layer" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+I" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->layerProperties(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.remove", QObject::tr( "Remove Layer" ),
                QObject::tr( "Remove the current layer from the project." ), "er_se", QObject::tr( "Layer" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+Delete" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.destructive = true;
        d.handler = [window] { window->removeLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.zoomTo", QObject::tr( "Zoom to Layer" ),
                QObject::tr( "Zooms to the current layer's extent." ), "l_yer_m_n_ger", QObject::tr( "Layer" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+L" ) );
        d.availability = ContextRules::layerSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->zoomToLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.toggleEditing", QObject::tr( "Toggle Editing" ),
                QObject::tr( "Toggles editing of the current vector layer." ),
                "mActionToggleEditing", QObject::tr( "Vector Editing" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+E" ) );
        d.checkable = true;
        d.availability = ContextRules::editingAvailable;
        d.checkedState = ContextRules::editingActive;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->toggleEditing(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.saveEdits", QObject::tr( "Save Edits" ),
                QObject::tr( "Saves vector edits." ), "mActionSaveEdits", QObject::tr( "Vector Editing" ) );
        d.availability = ContextRules::editingActive;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->saveEdits(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.newVector", QObject::tr( "New Shapefile Layer..." ),
                QObject::tr( "Create a new Shapefile vector layer." ),
                "new_fe_ture_cl_ss", QObject::tr( "Layer" ) );
        d.handler = [window] { window->newVectorLayer(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "layer.attributeTable", QObject::tr( "Open Attribute Table" ),
                QObject::tr( "View/edit the current vector layer's attribute table." ),
                "t_ble", QObject::tr( "Layer" ) );
        d.availability = ContextRules::vectorSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] { window->openAttributeTable(); };
        registry->registerCommand( d );
    }

    // ── 视图 / 地图工具 View ─────────────────────────────────────────
    {
        RS_CMD( d, "map.zoomIn", QObject::tr( "Zoom In" ), QObject::tr( "Zooms the map view in." ),
                "zoo_in", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence::ZoomIn;
        d.handler = [window] { window->zoomIn(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.zoomOut", QObject::tr( "Zoom Out" ), QObject::tr( "Zooms the map view out." ),
                "zoo_out", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence::ZoomOut;
        d.handler = [window] { window->zoomOut(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.zoomFull", QObject::tr( "Full Extent" ), QObject::tr( "Zooms to the extent of all layers." ),
                "full_extent", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+F" ) );
        d.handler = [window] { window->zoomFullExtent(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.pan", QObject::tr( "Pan" ), QObject::tr( "Pans the map." ),
                "p_n", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "H" ) );
        d.handler = [window] { window->panMap(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.identify", QObject::tr( "Identify" ),
                QObject::tr( "Click the map to query feature / pixel attributes." ), "identify", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+I" ) );
        d.handler = [window] { window->identifyFeatures(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.measureDistance", QObject::tr( "Measure Distance" ), QObject::tr( "Measures distance." ),
                "me_sure_dist", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+D" ) );
        d.handler = [window] { window->measureDistance(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.measureArea", QObject::tr( "Measure Area" ), QObject::tr( "Measures area." ),
                "me_sure_are_", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+A" ) );
        d.handler = [window] { window->measureArea(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.refresh", QObject::tr( "Refresh" ), QObject::tr( "Refreshes the map rendering." ),
                "refresh_view", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "F5" ) );
        d.handler = [window] { window->refreshMap(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.compareLayers", QObject::tr( "Layer Comparison..." ),
                QObject::tr( "Compare two layers side by side." ), "overl_y", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+C" ) );
        d.handler = [window] { window->openComparisonDialog(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "map.swipe", QObject::tr( "Swipe Comparison" ),
                QObject::tr( "Drag the divider on the map to compare the layers above and below." ), "s_lit", QObject::tr( "Map" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+S" ) );
        d.handler = [window] { window->toggleSwipeTool(); };
        registry->registerCommand( d );
    }

    // ── 工作台面板 Workbench 7.0 panels (goal §C/D/E/F) ──────────────
    // Canonical shortcut owners live HERE (CommandRegistry), so the 窗口 menu
    // projects these actions instead of defining competing sequences
    // (goal §I: one owner per shortcut).
    {
        RS_CMD( d, "workbench.processingHistory", QObject::tr( "Processing History" ),
                QObject::tr( "View the unified processing history across the Task Center / workflows." ),
                "b_tch_queue", QObject::tr( "Workspace" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+H" ) );
        d.handler = [window] { window->showUnifiedProcessingHistory(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.temporal", QObject::tr( "Temporal Workbench" ),
                QObject::tr( "Browse time series collections, filter dates and preview epochs." ),
                "ch_rt", QObject::tr( "Workspace" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+T" ) );
        d.handler = [window] { window->showTemporalWorkbench(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.datasetExperiment", QObject::tr( "Datasets and Experiments" ),
                QObject::tr( "Browse dataset versions, samples, runs and metric comparisons." ),
                "d_t_b_se", QObject::tr( "Workspace" ) );
        // Ctrl+Shift+D is taken by map.measureDistance — use E (E-xperiment).
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+E" ) );
        d.handler = [window] { window->showDatasetExperimentBench(); };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.model", QObject::tr( "Model Workbench" ),
                QObject::tr( "Browse the model catalog, readiness and submit test inference." ),
                "model_builder", QObject::tr( "Workspace" ) );
        d.shortcut = QKeySequence( QStringLiteral( "Ctrl+Shift+M" ) );
        d.handler = [window] { window->showModelBench(); };
        registry->registerCommand( d );
    }

    // ── 工作区 Workbenches ───────────────────────────────────────────
    {
        RS_CMD( d, "workbench.classify", QObject::tr( "Classification Workspace..." ),
                QObject::tr( "Opens the interactive supervised/unsupervised classification workspace." ),
                "su_ervised", QObject::tr( "Workspace" ) );
        // Route through WorkbenchHost::activate (review L #8): the opener
        // alone left m_activeId on the previous bench, desyncing the switcher
        // and the selection context's workbench projection.
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "classify" ) );
            else
                window->openClassificationWindow();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.georefI2I", QObject::tr( "Image-to-Image Registration (I2I)..." ),
                QObject::tr( "Two-canvas SRC|REF ground-point registration with SIFT support. No RPC." ),
                "coregistr_tion", QObject::tr( "Workspace" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "georef-i2i" ) );
            else
                window->openGeorefImageToImage();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.georefI2M", QObject::tr( "Image-to-Map Registration (I2M)..." ),
                QObject::tr( "Source image + main-project map picking; RPC Physical supported." ),
                "geocorrection", QObject::tr( "Workspace" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "georef-i2m" ) );
            else
                window->openGeorefImageToMap();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workbench.obia", QObject::tr( "Object-Based Classification (OBIA)..." ),
                QObject::tr( "Segmentation + object features + object-based classification." ),
                "seg_ent_tion", QObject::tr( "Workspace" ) );
        d.handler = [window] {
            if ( sicnu::app::WorkbenchHost *host = window->workbenchHost() )
                host->activate( QStringLiteral( "obia" ) );
            else
                window->openObiaWindow();
        };
        registry->registerCommand( d );
    }

    // ── 处理 Processing（对话框开放器批次） ──────────────────────────
    auto rasterTool = [&]( const char *id, const QString &title, const QString &desc,
                           const QString &icon, void ( QgisDesktopWindow::*slot )() ) {
        CommandDefinition d = base( id, title, desc, icon, QObject::tr( "Processing" ) );
        d.availability = ContextRules::rasterSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window, slot] { ( window->*slot )(); };
        registry->registerCommand( d );
    };
    rasterTool( "rs.bandMath", QObject::tr( "Band Math..." ),
                QObject::tr( "Expression-driven multiband math." ), "b_nd_m_th",
                &QgisDesktopWindow::openBandMathDialog );
    rasterTool( "rs.spectralIndex", QObject::tr( "Spectral Indices..." ),
                QObject::tr( "Common indices such as NDVI / NDWI / NDBI." ), "s_ectr_l_profile",
                &QgisDesktopWindow::openSpectralIndexDialog );
    rasterTool( "rs.contrastStretch", QObject::tr( "Contrast Stretch..." ),
                QObject::tr( "Linear / percent clip / histogram equalization output." ), "enh_nce",
                &QgisDesktopWindow::openContrastStretchDialog );
    rasterTool( "rs.spatialFilter", QObject::tr( "Spatial Filtering..." ),
                QObject::tr( "Mean / Gaussian / median / Laplacian convolution." ), "r_ster_c_lc",
                &QgisDesktopWindow::openSpatialFilterDialog );
    rasterTool( "rs.pca", QObject::tr( "Principal Component Analysis..." ),
                QObject::tr( "Multiband PCA forward and inverse transforms." ), "pca", &QgisDesktopWindow::openPcaDialog );
    rasterTool( "rs.bandRatio", QObject::tr( "Band Ratio..." ),
                QObject::tr( "Two-band ratio / normalized-ratio output." ), "b_nd_m_th_pro",
                &QgisDesktopWindow::openBandRatioDialog );
    rasterTool( "rs.mosaic", QObject::tr( "Mosaic..." ),
                QObject::tr( "Mosaic multiple rasters into a continuous image." ), "mos_ic",
                &QgisDesktopWindow::openMosaicDialog );
    rasterTool( "rs.changeDetection", QObject::tr( "Change Detection..." ),
                QObject::tr( "Two-date differencing / ratio / CVA detection." ), "ch_nge_detect",
                &QgisDesktopWindow::openChangeDetectionDialog );
    rasterTool( "rs.atmospheric", QObject::tr( "Atmospheric Correction..." ),
                QObject::tr( "6S / DOS reflectance products." ), "at_os_corr",
                &QgisDesktopWindow::openAtmosphericCorrectionDialog );
    rasterTool( "rs.qaMask", QObject::tr( "Generate QA Mask..." ),
                QObject::tr( " Decodes Landsat/Sentinel QA bands into a mask." ), "cloud_m_sk",
                &QgisDesktopWindow::openQaMaskDialog );
    rasterTool( "rs.applyMask", QObject::tr( "Apply Mask..." ),
                QObject::tr( "Clip with a mask / set NoData." ), "fill_nod_t_",
                &QgisDesktopWindow::openApplyMaskDialog );
    rasterTool( "rs.radiometric", QObject::tr( "Radiometric Calibration..." ),
                QObject::tr( "DN → radiance / reflectance." ), "r_dio__c_lib",
                &QgisDesktopWindow::openRadiometricCalibrationDialog );
    rasterTool( "rs.ortho", QObject::tr( "Orthorectification..." ),
                QObject::tr( "RPC / GCP geometric correction to map coordinates." ), "geocorrection",
                &QgisDesktopWindow::openOrthorectificationDialog );
    rasterTool( "rs.terrain", QObject::tr( "Terrain Analysis..." ),
                QObject::tr( "DEM products such as slope / aspect / hillshade." ), "hillsh_de",
                &QgisDesktopWindow::openTerrainDialog );
    rasterTool( "rs.fusion", QObject::tr( "Image Fusion..." ),
                QObject::tr( "Pansharpening (Brovey / IHS / Gram-Schmidt)." ), "p_nsh_r_en",
                &QgisDesktopWindow::openFusionDialog );
    rasterTool( "rs.temporal", QObject::tr( "Time Series Analysis..." ),
                QObject::tr( "Time-series NDVI / phenology curve analysis." ), "ti_e_series",
                &QgisDesktopWindow::openTemporalAnalysisDialog );

    // SAR 感知：斑点滤波要求 raster + SAR 双条件。
    {
        RS_CMD( d, "rs.speckle", QObject::tr( "Speckle Filtering (SAR)..." ),
                QObject::tr( "Lee / Frost / Kuan / Gamma-MAP. Requires a SAR raster." ),
                "sar_process", QObject::tr( "Processing" ) );
        d.availability = []( const SelectionContextSnapshot &s ) {
            return ContextRules::rasterSelected( s ) && ContextRules::sarSelected( s );
        };
        d.explain = []( const SelectionContextSnapshot &s ) {
            if ( !ContextRules::rasterSelected( s ) )
                return QObject::tr( "A raster layer must be selected" );
            return QObject::tr( "SAR data must be selected" );
        };
        d.handler = [window] { window->openSpeckleFilterDialog(); };
        registry->registerCommand( d );
    }

    // 提取波段：菜单内联 lambda 能力收编为命令（行为一致：模态对话框）。
    {
        RS_CMD( d, "rs.extractBands", QObject::tr( "Extract Band..." ),
                QObject::tr( "Extract and save a single band from a multiband raster." ),
                "extr_ct_b_nd", QObject::tr( "Processing" ) );
        d.availability = ContextRules::rasterSelected;
        d.explain = [id = d.id]( const SelectionContextSnapshot &s ) { return ContextRules::unavailabilityReason( s, id ); };
        d.handler = [window] {
            ExtractBandDialog dlg( window );
            if ( auto *rl = qobject_cast<QgsRasterLayer *>( window->mapCanvas()->currentLayer() ) )
                dlg.setRasterLayer( rl );
            dlg.exec();
        };
        registry->registerCommand( d );
    }

    // ── 工作流 Workflow (Workbench 9.0 M2 — #882 residual: the pipeline
    // editor dock previously advertised Ctrl+N/O/S in tooltips without any
    // binding; the real capability now lives here as first-class commands,
    // discoverable via palette/help. No default shortcut: the canonical
    // Ctrl+N/O/S belong to project.new/open/save.) ──
    {
        RS_CMD( d, "workflow.new", QObject::tr( "New Workflow" ),
                QObject::tr( "Creates an empty workflow in the pipeline editor." ),
                "new_project", QObject::tr( "Workflow" ) );
        d.handler = [window] {
            if ( auto *dock = window->pipelineDock() )
            {
                dock->show();
                dock->raise();
                dock->onNewClicked();
            }
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workflow.open", QObject::tr( "Open Workflow..." ),
                QObject::tr( "Opens a .json workflow file in the pipeline editor." ),
                "document-open", QObject::tr( "Workflow" ) );
        d.handler = [window] {
            if ( auto *dock = window->pipelineDock() )
            {
                dock->show();
                dock->raise();
                dock->onOpenClicked();
            }
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workflow.save", QObject::tr( "Save Workflow" ),
                QObject::tr( "Saves the current workflow as a .json file." ),
                "document-save", QObject::tr( "Workflow" ) );
        d.handler = [window] {
            if ( auto *dock = window->pipelineDock() )
                dock->onSaveClicked();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workflow.run", QObject::tr( "Run Full Pipeline" ),
                QObject::tr( "Schedules and runs the current workflow in topological order." ),
                "media-playback-start", QObject::tr( "Workflow" ) );
        d.handler = [window] {
            if ( auto *controller = window->sessionController() )
                controller->runFullWorkflow();
        };
        registry->registerCommand( d );
    }
    {
        RS_CMD( d, "workflow.stop", QObject::tr( "Stop Workflow" ),
                QObject::tr( "Stops the running pipeline task." ),
                "media-playback-stop", QObject::tr( "Workflow" ) );
        d.handler = [window] {
            if ( auto *controller = window->sessionController() )
                controller->stopWorkflow();
        };
        registry->registerCommand( d );
    }
}
