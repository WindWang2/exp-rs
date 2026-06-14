// guided_workflow_widget.cpp — In-app guided workflow for RS lab exercises
#include "guided_workflow_widget.h"
#include "main_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextBrowser>
#include <QGroupBox>
#include <QSplitter>

GuidedWorkflowWidget::GuidedWorkflowWidget(QgisDesktopWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
{
    setupUi();
    loadWorkflows();
}

void GuidedWorkflowWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Title
    auto *titleLabel = new QLabel(tr("<b>Guided Workflows</b>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Splitter: workflow list on left, step details on right
    auto *splitter = new QSplitter(Qt::Horizontal, this);

    // Left: workflow list
    auto *leftWidget = new QWidget();
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    leftLayout->addWidget(new QLabel(tr("Select Workflow:"), this));
    m_workflowList = new QListWidget(this);
    connect(m_workflowList, &QListWidget::currentRowChanged, this, &GuidedWorkflowWidget::onWorkflowSelected);
    leftLayout->addWidget(m_workflowList);

    m_startButton = new QPushButton(tr("Start Workflow"), this);
    m_startButton->setEnabled(false);
    connect(m_startButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onStartWorkflow);
    leftLayout->addWidget(m_startButton);

    splitter->addWidget(leftWidget);

    // Right: step details
    auto *rightWidget = new QWidget();
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_stepLabel = new QLabel(this);
    rightLayout->addWidget(m_stepLabel);

    m_stepBrowser = new QTextBrowser(this);
    m_stepBrowser->setOpenExternalLinks(false);
    rightLayout->addWidget(m_stepBrowser);

    // Navigation buttons
    auto *navLayout = new QHBoxLayout();
    m_prevButton = new QPushButton(tr("<< Previous"), this);
    m_prevButton->setEnabled(false);
    connect(m_prevButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onPreviousStep);
    navLayout->addWidget(m_prevButton);

    m_runButton = new QPushButton(tr("Run Step"), this);
    m_runButton->setEnabled(false);
    m_runButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; font-weight: bold; }");
    connect(m_runButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onRunStepAction);
    navLayout->addWidget(m_runButton);

    m_nextButton = new QPushButton(tr("Next >>"), this);
    m_nextButton->setEnabled(false);
    connect(m_nextButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onNextStep);
    navLayout->addWidget(m_nextButton);

    rightLayout->addLayout(navLayout);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    mainLayout->addWidget(splitter);
}

void GuidedWorkflowWidget::loadWorkflows()
{
    m_workflows.clear();
    m_workflows << createSpectralAnalysisWorkflow()
                << createImageEnhancementWorkflow()
                << createClassificationWorkflow()
                << createChangeDetectionWorkflow()
                << createTerrainAnalysisWorkflow();

    populateWorkflowList();
}

void GuidedWorkflowWidget::populateWorkflowList()
{
    m_workflowList->clear();
    for (const auto &wf : m_workflows) {
        m_workflowList->addItem(wf.title);
    }
}

void GuidedWorkflowWidget::onWorkflowSelected(int index)
{
    if (index < 0 || index >= m_workflows.size()) return;

    m_currentWorkflowIndex = index;
    m_startButton->setEnabled(true);

    // Show workflow description
    const auto &wf = m_workflows[index];
    m_stepLabel->setText(QString("<b>%1</b>").arg(wf.title));

    QString stepsHtml;
    for (int i = 0; i < wf.steps.size(); i++) {
        stepsHtml += QString("<li>%1</li>").arg(wf.steps[i].title);
    }

    m_stepBrowser->setHtml(
        QString("<p>%1</p><p><b>Steps:</b></p><ol>%2</ol>"
                "<p>Click <b>Start Workflow</b> to begin.</p>")
        .arg(wf.description, stepsHtml)
    );
}

void GuidedWorkflowWidget::onStartWorkflow()
{
    if (m_currentWorkflowIndex < 0) return;

    m_workflowActive = true;
    m_currentStepIndex = 0;

    m_startButton->setEnabled(false);
    m_runButton->setEnabled(true);

    showStep(0);
    emit workflowStarted(m_workflows[m_currentWorkflowIndex].id);
}

void GuidedWorkflowWidget::onNextStep()
{
    if (!m_workflowActive) return;

    const auto &wf = m_workflows[m_currentWorkflowIndex];
    if (m_currentStepIndex < wf.steps.size() - 1) {
        m_currentStepIndex++;
        showStep(m_currentStepIndex);
        emit stepCompleted(m_currentStepIndex - 1);
    } else {
        // Workflow completed
        m_workflowActive = false;
        m_runButton->setEnabled(false);
        m_nextButton->setEnabled(false);
        m_stepLabel->setText(tr("<b>Workflow Complete!</b>"));
        m_stepBrowser->setHtml(
            tr("<p>Congratulations! You have completed the <b>%1</b> workflow.</p>"
               "<p>You can now try other workflows or experiment with different parameters.</p>")
            .arg(wf.title)
        );
        emit workflowCompleted(wf.id);
    }
}

void GuidedWorkflowWidget::onPreviousStep()
{
    if (!m_workflowActive || m_currentStepIndex <= 0) return;

    m_currentStepIndex--;
    showStep(m_currentStepIndex);
}

void GuidedWorkflowWidget::onRunStepAction()
{
    if (!m_workflowActive) return;

    const auto &wf = m_workflows[m_currentWorkflowIndex];
    const auto &step = wf.steps[m_currentStepIndex];

    if (!step.actionId.isEmpty() && m_mainWindow) {
        // Trigger the action
        QMetaObject::invokeMethod(m_mainWindow, step.actionId.toUtf8().constData());
    }
}

void GuidedWorkflowWidget::showStep(int index)
{
    const auto &wf = m_workflows[m_currentWorkflowIndex];
    if (index < 0 || index >= wf.steps.size()) return;

    const auto &step = wf.steps[index];

    m_stepLabel->setText(
        tr("<b>Step %1/%2: %3</b>")
        .arg(index + 1)
        .arg(wf.steps.size())
        .arg(step.title)
    );

    m_stepBrowser->setHtml(
        QString("<p><b>Task:</b> %1</p>"
                "<p>%2</p>"
                "<hr>"
                "<p><b>Hint:</b> %3</p>")
        .arg(step.title)
        .arg(step.instructions)
        .arg(step.completionHint)
    );

    // Update navigation buttons
    m_prevButton->setEnabled(index > 0);
    m_nextButton->setEnabled(true);
    m_runButton->setEnabled(!step.actionId.isEmpty());
}

void GuidedWorkflowWidget::updateStepDisplay()
{
    if (m_workflowActive && m_currentWorkflowIndex >= 0) {
        showStep(m_currentStepIndex);
    }
}

// ============================================================================
// Built-in Workflows
// ============================================================================

Workflow GuidedWorkflowWidget::createSpectralAnalysisWorkflow()
{
    Workflow wf;
    wf.id = "spectral_analysis";
    wf.title = tr("Spectral Analysis (光谱分析)");
    wf.description = tr("Learn to analyze spectral characteristics of different land cover types "
                        "using vegetation indices and band math.");

    // Step 1: Load data
    WorkflowStep step1;
    step1.title = tr("Load Sample Data");
    step1.description = tr("Load the sample Landsat image");
    step1.instructions = tr("<p>First, load the sample multi-band Landsat image:</p>"
                           "<ol>"
                           "<li>Go to <b>File > Add Raster Layer...</b></li>"
                           "<li>Navigate to the <code>samples_data/</code> directory</li>"
                           "<li>Select <code>landsat_sample.tif</code></li>"
                           "<li>Click <b>Open</b></li>"
                           "</ol>"
                           "<p>This is a 7-band Landsat-like image with vegetation, water, urban, and bare soil areas.</p>");
    step1.actionId = "addRasterLayer";
    step1.completionHint = tr("The image should appear in the map canvas and layer panel.");
    wf.steps << step1;

    // Step 2: View spectral profile
    WorkflowStep step2;
    step2.title = tr("Examine Spectral Profiles");
    step2.description = tr("Click on different land cover types to see their spectral signatures");
    step2.instructions = tr("<p>Use the Identify tool to examine spectral characteristics:</p>"
                           "<ol>"
                           "<li>Go to <b>View > Identify</b> (or press Ctrl+Shift+I)</li>"
                           "<li>Click on different areas of the image:</li>"
                           "<ul>"
                           "<li><b>Dark area</b> (bottom) — Water</li>"
                           "<li><b>Green area</b> (middle) — Vegetation</li>"
                           "<li><b>Bright area</b> (top-left) — Urban</li>"
                           "<li><b>Brown area</b> (right) — Bare soil</li>"
                           "</ul>"
                           "<li>Observe the spectral profile in the Identify Results panel</li>"
                           "</ol>"
                           "<p>Note how vegetation has high NIR (band 5) reflectance!</p>");
    step2.actionId = "identifyFeatures";
    step2.completionHint = tr("You should see different spectral curves for different land cover types.");
    wf.steps << step2;

    // Step 3: Calculate NDVI
    WorkflowStep step3;
    step3.title = tr("Calculate NDVI");
    step3.description = tr("Compute the Normalized Difference Vegetation Index");
    step3.instructions = tr("<p>NDVI highlights vegetation:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Vegetation Index...</b></li>"
                           "<li>Select <b>NDVI</b> from the dropdown</li>"
                           "<li>Set Red band = <b>Band 4</b></li>"
                           "<li>Set NIR band = <b>Band 5</b></li>"
                           "<li>Set output file (e.g., <code>ndvi_result.tif</code>)</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>"
                           "<p>NDVI = (NIR - Red) / (NIR + Red). Values range from -1 to 1.</p>");
    step3.actionId = "openSpectralIndexDialog";
    step3.completionHint = tr("NDVI values: Vegetation > 0.3, Water < 0, Bare soil ≈ 0.");
    wf.steps << step3;

    // Step 4: Custom band math
    WorkflowStep step4;
    step4.title = tr("Custom Band Ratio");
    step4.description = tr("Use Band Math to create a custom spectral index");
    step4.instructions = tr("<p>Try a band ratio (NIR/Red):</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Band Math...</b></li>"
                           "<li>Enter expression: <code>b5 / b4</code></li>"
                           "<li>Set output file (e.g., <code>band_ratio.tif</code>)</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>"
                           "<p>Band ratios can enhance differences between land cover types.</p>");
    step4.actionId = "openBandMathDialog";
    step4.completionHint = tr("The ratio image should show vegetation areas with high values.");
    wf.steps << step4;

    // Step 5: Compare results
    WorkflowStep step5;
    step5.title = tr("Compare Results");
    step5.description = tr("Use the comparison tool to view results side-by-side");
    step5.instructions = tr("<p>Compare the original image with NDVI:</p>"
                           "<ol>"
                           "<li>Go to <b>View > Compare Layers...</b></li>"
                           "<li>Select the original image as left layer</li>"
                           "<li>Select NDVI result as right layer</li>"
                           "<li>Use <b>Split Screen</b> mode to compare</li>"
                           "<li>Try <b>Flicker</b> mode to see differences</li>"
                           "</ol>"
                           "<p>Can you identify which areas have the most vegetation?</p>");
    step5.actionId = "openComparisonDialog";
    step5.completionHint = tr("High NDVI values correspond to green vegetation areas.");
    wf.steps << step5;

    return wf;
}

Workflow GuidedWorkflowWidget::createImageEnhancementWorkflow()
{
    Workflow wf;
    wf.id = "image_enhancement";
    wf.title = tr("Image Enhancement (影像增强)");
    wf.description = tr("Learn contrast enhancement and spatial filtering techniques.");

    // Step 1: Load data
    WorkflowStep step1;
    step1.title = tr("Load Sample Data");
    step1.instructions = tr("<p>Load the sample Landsat image: <code>samples_data/landsat_sample.tif</code></p>");
    step1.actionId = "addRasterLayer";
    step1.completionHint = tr("Image loaded in map canvas.");
    wf.steps << step1;

    // Step 2: Contrast stretch
    WorkflowStep step2;
    step2.title = tr("Contrast Stretch");
    step2.instructions = tr("<p>Enhance image contrast:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Enhancement > Contrast Stretch...</b></li>"
                           "<li>Try different methods:</li>"
                           "<ul>"
                           "<li><b>Linear Stretch</b> — Simple min-max mapping</li>"
                           "<li><b>Percent Clip</b> — Remove 2% outliers</li>"
                           "<li><b>StdDev Stretch</b> — Mean ± 2σ</li>"
                           "<li><b>Histogram Equalization</b> — Uniform distribution</li>"
                           "</ul>"
                           "<li>Compare the results</li>"
                           "</ol>");
    step2.actionId = "openContrastStretchDialog";
    step2.completionHint = tr("Enhanced images show more detail.");
    wf.steps << step2;

    // Step 3: Spatial filtering
    WorkflowStep step3;
    step3.title = tr("Spatial Filtering");
    step3.instructions = tr("<p>Apply spatial filters:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Enhancement > Spatial Filter...</b></li>"
                           "<li>Try different filters:</li>"
                           "<ul>"
                           "<li><b>Mean 3×3</b> — Smooth noise</li>"
                           "<li><b>Median 3×3</b> — Remove salt-and-pepper noise</li>"
                           "<li><b>Sobel</b> — Detect edges</li>"
                           "<li><b>Laplacian</b> — Enhance edges</li>"
                           "</ul>"
                           "</ol>");
    step3.actionId = "openSpatialFilterDialog";
    step3.completionHint = tr("Edge detection highlights boundaries between land cover types.");
    wf.steps << step3;

    return wf;
}

Workflow GuidedWorkflowWidget::createClassificationWorkflow()
{
    Workflow wf;
    wf.id = "classification";
    wf.title = tr("Image Classification (影像分类)");
    wf.description = tr("Learn supervised and unsupervised classification methods.");

    // Step 1: Load data
    WorkflowStep step1;
    step1.title = tr("Load Data");
    step1.instructions = tr("<p>Load both the image and training samples:</p>"
                           "<ol>"
                           "<li>File > Add Raster Layer... → <code>landsat_sample.tif</code></li>"
                           "<li>File > Add Vector Layer... → <code>training_samples.shp</code></li>"
                           "</ol>");
    step1.actionId = "addRasterLayer";
    step1.completionHint = tr("Both layers visible in map canvas.");
    wf.steps << step1;

    // Step 2: Supervised classification
    WorkflowStep step2;
    step2.title = tr("Supervised Classification");
    step2.instructions = tr("<p>Classify the image using training samples:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Classification...</b></li>"
                           "<li>Select <code>landsat_sample.tif</code> as input</li>"
                           "<li>Select <code>training_samples.shp</code> as training data</li>"
                           "<li>Choose <b>NormalBayes</b> classifier</li>"
                           "<li>Set output file</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>");
    step2.actionId = "openClassificationWindow";
    step2.completionHint = tr("Classified image shows different land cover classes.");
    wf.steps << step2;

    // Step 3: Accuracy assessment
    WorkflowStep step3;
    step3.title = tr("Accuracy Assessment");
    step3.instructions = tr("<p>Evaluate classification accuracy:</p>"
                           "<ol>"
                           "<li>In the Classification window, click <b>Accuracy Assessment</b></li>"
                           "<li>View the confusion matrix</li>"
                           "<li>Note Overall Accuracy and Kappa coefficient</li>"
                           "<li>Export results to CSV</li>"
                           "</ol>");
    step3.actionId = "";
    step3.completionHint = tr("Overall accuracy > 80% is good for this simple example.");
    wf.steps << step3;

    return wf;
}

Workflow GuidedWorkflowWidget::createChangeDetectionWorkflow()
{
    Workflow wf;
    wf.id = "change_detection";
    wf.title = tr("Change Detection (变化检测)");
    wf.description = tr("Detect changes between two time periods.");

    // Step 1: Load data
    WorkflowStep step1;
    step1.title = tr("Load Before/After Images");
    step1.instructions = tr("<p>Load both time period images:</p>"
                           "<ol>"
                           "<li>File > Add Raster Layer... → <code>change_before.tif</code></li>"
                           "<li>File > Add Raster Layer... → <code>change_after.tif</code></li>"
                           "</ol>");
    step1.actionId = "addRasterLayer";
    step1.completionHint = tr("Both images loaded.");
    wf.steps << step1;

    // Step 2: Visual comparison
    WorkflowStep step2;
    step2.title = tr("Visual Comparison");
    step2.instructions = tr("<p>Compare images visually:</p>"
                           "<ol>"
                           "<li>Go to <b>View > Compare Layers...</b></li>"
                           "<li>Use <b>Flicker</b> mode to spot changes</li>"
                           "<li>Use <b>Split Screen</b> to compare side-by-side</li>"
                           "</ol>");
    step2.actionId = "openComparisonDialog";
    step2.completionHint = tr("You should see a dark patch in the 'after' image.");
    wf.steps << step2;

    // Step 3: Change detection
    WorkflowStep step3;
    step3.title = tr("Run Change Detection");
    step3.instructions = tr("<p>Compute change automatically:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Change Detection...</b></li>"
                           "<li>Select <b>Normalized Difference</b> method</li>"
                           "<li>Set 'Before' image</li>"
                           "<li>Set 'After' image</li>"
                           "<li>Set output file</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>");
    step3.actionId = "openChangeDetectionDialog";
    step3.completionHint = tr("Change map highlights areas of change.");
    wf.steps << step3;

    return wf;
}

Workflow GuidedWorkflowWidget::createTerrainAnalysisWorkflow()
{
    Workflow wf;
    wf.id = "terrain_analysis";
    wf.title = tr("Terrain Analysis (地形分析)");
    wf.description = tr("Analyze terrain characteristics from DEM data.");

    // Step 1: Load DEM
    WorkflowStep step1;
    step1.title = tr("Load DEM");
    step1.instructions = tr("<p>Load the sample DEM:</p>"
                           "<ol>"
                           "<li>File > Add Raster Layer... → <code>dem_sample.tif</code></li>"
                           "</ol>");
    step1.actionId = "addRasterLayer";
    step1.completionHint = tr("DEM loaded with elevation values.");
    wf.steps << step1;

    // Step 2: Hillshade
    WorkflowStep step2;
    step2.title = tr("Generate Hillshade");
    step2.instructions = tr("<p>Create a hillshade for 3D visualization:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Terrain Analysis > Slope/Aspect/Hillshade...</b></li>"
                           "<li>Check <b>Hillshade</b></li>"
                           "<li>Set azimuth = 315°, elevation = 45°</li>"
                           "<li>Set output file</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>");
    step2.actionId = "openTerrainDialog";
    step2.completionHint = tr("Hillshade creates a 3D-like appearance.");
    wf.steps << step2;

    // Step 3: Slope
    WorkflowStep step3;
    step3.title = tr("Calculate Slope");
    step3.instructions = tr("<p>Compute slope from DEM:</p>"
                           "<ol>"
                           "<li>Go to <b>Raster > Terrain Analysis</b></li>"
                           "<li>Check <b>Slope</b></li>"
                           "<li>Set output file</li>"
                           "<li>Click <b>Run</b></li>"
                           "</ol>"
                           "<p>Slope values range from 0° (flat) to 90° (vertical).</p>");
    step3.actionId = "openTerrainDialog";
    step3.completionHint = tr("Steep slopes appear bright in the slope image.");
    wf.steps << step3;

    return wf;
}
