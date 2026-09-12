// guided_workflow_widget.h — In-app guided workflow for RS lab exercises
//
// The widget is a renderer over LabSpec data: every workflow is loaded from
// data/labs/*.lab.json (see lab_spec_loader.h). There is deliberately no
// built-in content — a missing or invalid LabSpec is surfaced as a typed
// error entry, never silently replaced (ADR 0146).
#pragma once

#include <QWidget>
#include <QList>
#include <QString>

#include <json/json.h>

#include <memory>

#include "lab_spec_loader.h"

class QLabel;
class QPushButton;
class QListWidget;
class QTextBrowser;
class QVBoxLayout;
class QgisDesktopWindow;

namespace sicnu::app { class GuiJobHandle; }

/**
 * A single step in a guided workflow. Operator-bound steps carry
 * {operatorId, params} and execute through the same JobRequest path as
 * headless callers; UI-verb steps carry {action} (a main-window slot name);
 * manual steps carry neither and are pure teaching content.
 */
struct WorkflowStep {
    QString title;           // Step title
    QString titleZh;         // Chinese step title (中文标题)
    QString description;     // What to do (中文说明)
    QString teachingNote;    // Background theory the student should take away
    QString completionHint;  // How to know the step is done
    QString operatorId;      // Processing Registry operator ("rs:*"), or empty
    Json::Value params;      // Operator parameters; only meaningful with operatorId
    QString action;          // Main-window slot name for UI verbs, or empty

    bool hasOperator() const { return !operatorId.isEmpty(); }
    bool isManual() const { return operatorId.isEmpty() && action.isEmpty(); }
};

/**
 * A complete guided workflow with multiple steps, loaded from one
 * <id>.lab.json file.
 */
struct Workflow {
    QString id;              // Canonical LabSpec id (labNN_slug)
    QString title;           // Display title (Chinese-first)
    QString description;     // Teaching objective
    QList<WorkflowStep> steps;
};

/**
 * Dock widget that provides guided workflows for RS lab exercises.
 * Shows step-by-step instructions and launches the bound operator or UI tool.
 */
class GuidedWorkflowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GuidedWorkflowWidget(QgisDesktopWindow *mainWindow, QWidget *parent = nullptr);
    // Out-of-line: owns the GuiJobHandle via unique_ptr of an incomplete type.
    ~GuidedWorkflowWidget() override;

    // Load available workflows from the LabSpec directory
    void loadWorkflows();

    // Get all available workflows
    QList<Workflow> workflows() const { return m_workflows; }

    // Typed load failures from the most recent loadWorkflows() run
    QStringList loadErrorStrings() const { return lab::errorStrings(m_loadResult); }

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
    void runOperatorStep(const WorkflowStep &step);
    void showRunMessage(const QString &message, bool isError);

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
    lab::LabLoadResult m_loadResult;
    QList<Workflow> m_workflows;
    std::unique_ptr<sicnu::app::GuiJobHandle> m_jobHandle;
    int m_currentWorkflowIndex = -1;
    int m_currentStepIndex = 0;
    bool m_workflowActive = false;
};
