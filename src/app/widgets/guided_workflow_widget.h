// guided_workflow_widget.h — In-app guided workflow for RS lab exercises
#pragma once

#include <QWidget>
#include <QList>
#include <QString>

class QLabel;
class QPushButton;
class QListWidget;
class QTextBrowser;
class QVBoxLayout;
class QgisDesktopWindow;

/**
 * A single step in a guided workflow.
 */
struct WorkflowStep {
    QString title;           // Step title
    QString description;     // What to do
    QString instructions;    // Detailed instructions (HTML)
    QString actionId;        // Menu action to trigger (e.g., "openBandMathDialog")
    QString completionHint;  // How to know the step is done
};

/**
 * A complete guided workflow with multiple steps.
 */
struct Workflow {
    QString id;              // Unique workflow ID
    QString title;           // Display title
    QString description;     // Short description
    QList<WorkflowStep> steps;
};

/**
 * Dock widget that provides guided workflows for RS lab exercises.
 * Shows step-by-step instructions and launches appropriate tools.
 */
class GuidedWorkflowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GuidedWorkflowWidget(QgisDesktopWindow *mainWindow, QWidget *parent = nullptr);

    // Load available workflows
    void loadWorkflows();

    // Get all available workflows
    QList<Workflow> workflows() const { return m_workflows; }

signals:
    void workflowStarted(const QString &workflowId);
    void workflowCompleted(const QString &workflowId);
    void stepCompleted(int stepIndex);

private slots:
    void onWorkflowSelected(int index);
    void onStartWorkflow();
    void onNextStep();
    void onPreviousStep();
    void onRunStepAction();

private:
    void setupUi();
    void populateWorkflowList();
    void showStep(int index);
    void updateStepDisplay();

    // Built-in workflows
    Workflow createSpectralAnalysisWorkflow();
    Workflow createClassificationWorkflow();
    Workflow createChangeDetectionWorkflow();
    Workflow createTerrainAnalysisWorkflow();
    Workflow createImageEnhancementWorkflow();

    QgisDesktopWindow *m_mainWindow = nullptr;

    // UI elements
    QListWidget *m_workflowList = nullptr;
    QTextBrowser *m_stepBrowser = nullptr;
    QLabel *m_stepLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_runButton = nullptr;

    // State
    QList<Workflow> m_workflows;
    int m_currentWorkflowIndex = -1;
    int m_currentStepIndex = 0;
    bool m_workflowActive = false;
};
