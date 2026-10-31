// guided_workflow_widget.cpp — In-app guided workflow for RS lab exercises
#include "guided_workflow_widget.h"
#include "main_window.h"

#include "jobs/job_types.h"
#include "processing/framework/runtime_paths.h"
#include "shell/gui_job_adapter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextBrowser>
#include <QGroupBox>
#include <QSplitter>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

namespace {

/// Rows the aggregate load-failure entry occupies at the top of the workflow
/// list: exactly one, however many specs failed — the entry shows the failure
/// count inside that single row, never as one row per failure (#1536).
constexpr int kLoadErrorEntryRows = 1;

/// Render one LabSpec as the widget's display model.
Workflow toWorkflow( const lab::LabSpec &spec )
{
    Workflow wf;
    wf.id = spec.id;
    wf.title = QObject::tr( "%1 · %2" ).arg( spec.id.section( QLatin1Char( '_' ), 0, 0 ), spec.titleZh );
    wf.description = spec.objective;
    for ( const auto &step : spec.steps )
    {
        WorkflowStep s;
        s.title = step.titleZh.isEmpty() ? step.title : step.titleZh;
        s.titleZh = step.titleZh;
        s.description = step.descriptionZh;
        s.teachingNote = step.teachingNote;
        s.completionHint = step.completionHint;
        s.operatorId = step.operatorId;
        s.params = step.params;
        s.action = step.action;
        wf.steps << s;
    }
    return wf;
}

} // namespace

GuidedWorkflowWidget::GuidedWorkflowWidget(QgisDesktopWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_mainWindow(mainWindow)
    , m_jobHandle(std::make_unique<sicnu::app::GuiJobHandle>(this))
{
    setupUi();
    loadWorkflows();
}

GuidedWorkflowWidget::~GuidedWorkflowWidget() = default;

void GuidedWorkflowWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Title
    auto *titleLabel = new QLabel(tr("<b>Guided Workflow</b>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Splitter: workflow list on left, step details on right
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("rsGuidedWorkflowSplitter"));

    // Left: workflow list
    auto *leftWidget = new QWidget();
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    leftLayout->addWidget(new QLabel(tr("Select workflow:"), this));
    m_workflowList = new QListWidget(this);
    m_workflowList->setToolTip(tr("Choose a guided workflow." ));
    connect(m_workflowList, &QListWidget::currentRowChanged, this, &GuidedWorkflowWidget::onWorkflowSelected);
    leftLayout->addWidget(m_workflowList);

    m_startButton = new QPushButton(tr("Start Workflow"), this);
    m_startButton->setObjectName( QStringLiteral( "rsGuidedStartBtn" ) );
    m_startButton->setToolTip(tr("Starts the selected workflow." ));
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
    m_prevButton = new QPushButton(tr("Previous Step"), this);
    m_prevButton->setObjectName( QStringLiteral( "rsGuidedPrevBtn" ) );
    m_prevButton->setToolTip(tr("Returns to the previous step." ));
    m_prevButton->setEnabled(false);
    connect(m_prevButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onPreviousStep);
    navLayout->addWidget(m_prevButton);

    m_runButton = new QPushButton(tr("Run This Step"), this);
    m_runButton->setObjectName( QStringLiteral( "rsGuidedRunBtn" ) );
    m_runButton->setToolTip(tr("Runs the current step's action." ));
    m_runButton->setEnabled(false);
    m_runButton->setProperty("primary", true);
    connect(m_runButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onRunStepAction);
    navLayout->addWidget(m_runButton);

    m_nextButton = new QPushButton(tr("Next Step"), this);
    m_nextButton->setObjectName( QStringLiteral( "rsGuidedNextBtn" ) );
    m_nextButton->setToolTip(tr("Continue to the next step after finishing the current one." ));
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
    // Single source of truth: data/labs/*.lab.json. Load failures stay
    // visible as a typed error entry — never replaced by built-in content.
    loadWorkflowsFromDir( lab::defaultLabDirectory() );
}

void GuidedWorkflowWidget::loadWorkflowsFromDir( const QString &dir )
{
    m_loadResult = lab::loadLabSpecsFromDir( dir );

    m_workflows.clear();
    for ( const auto &spec : m_loadResult.labs )
        m_workflows << toWorkflow( spec );

    populateWorkflowList();
}

void GuidedWorkflowWidget::populateWorkflowList()
{
    m_workflowList->clear();

    const int errorCount = m_loadResult.errors.size();
    if ( errorCount > 0 )
    {
        // Typed error entry pinned to the top: selecting it shows the reasons.
        // It always occupies exactly kLoadErrorEntryRows row — row->lab math
        // in labIndexOfRow() depends on that.
        m_workflowList->addItem( tr( "⚠ Experiment spec load failures (%1)" ).arg( errorCount ) );
        QListWidgetItem *errorItem = m_workflowList->item( 0 );
        errorItem->setToolTip( m_loadResult.errors.first().toString() );
    }

    for ( const auto &wf : m_workflows )
        m_workflowList->addItem( wf.title );

    if ( errorCount > 0 )
        m_workflowList->setCurrentRow( 0 );
}

/// Row 0 is the load-failure entry only when at least one spec failed to load
/// (errorCount > 0), and that entry always occupies exactly
/// kLoadErrorEntryRows row: lab i then sits at row i + kLoadErrorEntryRows.
/// With no failures there is no error row and lab i sits at row i (#1536).
static int labIndexOfRow( int row, int errorCount )
{
    return row - ( errorCount > 0 ? kLoadErrorEntryRows : 0 );
}

void GuidedWorkflowWidget::onWorkflowSelected(int index)
{
    const int errorCount = m_loadResult.errors.size();
    if ( errorCount > 0 && index == 0 )
    {
        // The error page replaces the step view: the previous session must
        // not keep running Next/Run state against it.
        m_workflowActive = false;
        m_currentStepIndex = 0;
        m_prevButton->setEnabled(false);
        m_runButton->setEnabled(false);
        m_nextButton->setEnabled(false);
        m_startButton->setEnabled(false);
        m_stepLabel->setText( tr( "<b>Experiment spec load failures</b>" ) );
        QString errorHtml = tr( "<p>The following LabSpec files failed to load. Fix them and restart or reopen this panel:</p><ul>" );
        for ( const auto &error : m_loadResult.errors )
            errorHtml += QStringLiteral( "<li><code>%1</code></li>" ).arg( error.toString().toHtmlEscaped() );
        errorHtml += QLatin1String( "</ul>" );
        m_stepBrowser->setHtml( errorHtml );
        return;
    }

    const int labIndex = labIndexOfRow( index, errorCount );
    if ( labIndex < 0 || labIndex >= m_workflows.size() ) return;

    // Selecting a different workflow is a session boundary: the previous
    // experiment's step cursor and completion state must never leak into the
    // newly selected one. A stale cursor used to make Next declare the
    // never-started experiment complete and Run index past its steps. An
    // in-flight operator job keeps running (its completion callback is
    // workflow/step-guarded and only restores the button on the same step).
    if ( labIndex != m_currentWorkflowIndex )
    {
        m_workflowActive = false;
        m_currentStepIndex = 0;
        m_failedSteps.clear();
        m_prevButton->setEnabled(false);
        m_runButton->setEnabled(false);
        m_nextButton->setEnabled(false);
    }

    m_currentWorkflowIndex = labIndex;
    // LabSpec 2/3 labs may carry no steps (the operator sequence is
    // pipeline-owned, ADR 0166): there is nothing to walk, so Start stays
    // disabled and the overview says so instead of failing or dead-ending.
    const bool walkable = !m_workflows[labIndex].steps.isEmpty();
    m_startButton->setEnabled(walkable);

    // Show workflow description
    const auto &wf = m_workflows[labIndex];
    m_stepLabel->setText(QString("<b>%1</b>").arg(wf.title.toHtmlEscaped()));

    QString stepsHtml;
    for (int i = 0; i < wf.steps.size(); i++)
        stepsHtml += QString("<li>%1</li>").arg(wf.steps[i].title.toHtmlEscaped());

    const QString startHint = walkable
        ? tr( "Click <b>Start experiment</b> to begin." )
        : tr( "This experiment is a LabSpec 2/3 document: the operation sequence is defined by a pipeline; no step-by-step guidance yet." );
    m_stepBrowser->setHtml(
        QString("<p>%1</p><p><b>%2</b></p><ol>%3</ol>"
                "<p>%4</p>")
        .arg(wf.description.toHtmlEscaped(),
             tr( "Step" ),
             stepsHtml,
             startHint)
    );
}

void GuidedWorkflowWidget::onStartWorkflow()
{
    if (m_currentWorkflowIndex < 0 || m_currentWorkflowIndex >= m_workflows.size()) return;
    if (m_workflows[m_currentWorkflowIndex].steps.isEmpty()) return;

    m_workflowActive = true;
    m_currentStepIndex = 0;
    // A fresh start clears the previous run's failure records; a job still in
    // flight keeps its own guards (isRunning) independent of this set.
    m_failedSteps.clear();

    m_startButton->setEnabled(false);
    m_runButton->setEnabled(true);

    showStep(0);
    emit workflowStarted(m_workflows[m_currentWorkflowIndex].id);
}

void GuidedWorkflowWidget::onNextStep()
{
    if (!m_workflowActive) return;
    // #1453/WP02: a job for this step is still in flight — completing the
    // workflow now would declare "Experiment complete!" for a run that may
    // fail seconds later (and the inactive-workflow guard then suppresses
    // that failure from this pane). The button is disabled during the run;
    // this guard covers queued state updates that race the click.
    if (m_jobHandle->isRunning())
        return;
    if (m_currentWorkflowIndex < 0 || m_currentWorkflowIndex >= m_workflows.size()) return;
    const auto &wf = m_workflows[m_currentWorkflowIndex];
    if (m_currentStepIndex < 0 || m_currentStepIndex >= wf.steps.size()) return;

    if (m_currentStepIndex < wf.steps.size() - 1) {
        m_currentStepIndex++;
        showStep(m_currentStepIndex);
        emit stepCompleted(m_currentStepIndex - 1);
    } else {
        // Completion gate: navigating to the end is allowed (tour), but the
        // experiment is NOT complete while any operator step ended in
        // failure — declaring success would teach the student that broken
        // runs count as done.
        if (!m_failedSteps.isEmpty()) {
            QStringList failedTitles;
            for (int idx : m_failedSteps) {
                if (idx >= 0 && idx < wf.steps.size())
                    failedTitles << wf.steps[idx].title;
            }
            showRunMessage(
                tr( "Experiment not complete: %1 step(s) failed (%2). Go back, re-run them, then finish." )
                    .arg( QString::number( m_failedSteps.size() ),
                          failedTitles.join( QStringLiteral( ", " ) ) ),
                true );
            return;
        }
        // Workflow completed
        m_workflowActive = false;
        m_runButton->setEnabled(false);
        m_nextButton->setEnabled(false);
        m_stepLabel->setText(tr( "<b>Experiment complete!</b>" ));
        m_stepBrowser->setHtml(
            tr( "<p>Congratulations! You completed the <b>%1</b> experiment.</p><p>You can try other experiments or adjust parameters for further exploration.</p>" )
                .arg(wf.title.toHtmlEscaped())
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
    if (!m_workflowActive || m_currentWorkflowIndex < 0) return;
    if (m_currentWorkflowIndex >= m_workflows.size()) return;
    const auto &wf = m_workflows[m_currentWorkflowIndex];
    // Fail-closed: never index past the current workflow's steps (the caller
    // can observe a stale cursor across a workflow switch).
    if (m_currentStepIndex < 0 || m_currentStepIndex >= wf.steps.size()) return;
    const auto &step = wf.steps[m_currentStepIndex];

    if (step.hasOperator())
    {
        runOperatorStep(step);
        return;
    }

    if (!step.action.isEmpty() && m_mainWindow)
    {
        // UI-verb step: invoke the named slot on the main window.
        if ( !QMetaObject::invokeMethod( m_mainWindow, step.action.toUtf8().constData() ) )
            showRunMessage( tr( "Action \"%1\" does not exist on the main window; the experiment spec may be outdated." ).arg( step.action ), true );
        return;
    }
}

void GuidedWorkflowWidget::runOperatorStep( const WorkflowStep &step )
{
    if ( m_jobHandle->isRunning() )
    {
        showRunMessage( tr( "A task is already running; wait for it to finish before the next step." ), true );
        return;
    }

    // Resolve relative data/ and outputs/ references against the runtime root
    // so the operator sees the same absolute paths a headless caller produces.
    const QString labId = m_workflows[m_currentWorkflowIndex].id;
    const Json::Value params = lab::resolveLabParamPaths(
        step.params, labId,
        []( const QString &relative, lab::PathRole )
        { return sicnu::processing::resolveRuntimeDataPath( relative ); } );

    // Create the per-lab outputs directories up front. Walk the whole params
    // tree so nested references (e.g. arrays such as rs:mosaic inputs) count.
    // A single failure aborts the WHOLE submission: submitting after a
    // directory we could not create would just fail again inside the operator
    // with a less actionable message (double error, no repair hint).
    std::function<bool( const Json::Value & )> ensureOutputDirs = [&]( const Json::Value &node ) -> bool
    {
        if ( node.isString() )
        {
            const QString value = QString::fromStdString( node.asString() );
            if ( value.contains( QStringLiteral( "/output/labs/" ) ) )
            {
                const QString outputDir = QFileInfo( value ).absolutePath();
                if ( !QDir().mkpath( outputDir ) )
                {
                    showRunMessage( tr( "Cannot create the output directory: %1 — check disk space and permissions, then run this step again." ).arg( outputDir ), true );
                    return false;
                }
            }
            return true;
        }
        if ( node.isArray() )
        {
            for ( const auto &item : node )
                if ( !ensureOutputDirs( item ) )
                    return false;
            return true;
        }
        if ( node.isObject() )
        {
            for ( const auto &key : node.getMemberNames() )
                if ( !ensureOutputDirs( node[ key ] ) )
                    return false;
        }
        return true;
    };
    if ( !ensureOutputDirs( params ) )
        return;

    sicnu::jobs::JobRequest req;
    req.algorithmId = step.operatorId.toStdString();
    req.params = params;
    req.title = m_workflows[m_currentWorkflowIndex].title.toStdString();
    req.source = "guided_lab";

    // Identify the step this submission belongs to: the outcome restores the
    // run button only when the student is still looking at the same step.
    const QString workflowId = m_workflows[m_currentWorkflowIndex].id;
    const int stepIndex = m_currentStepIndex;
    const QString stepTitle = step.title;

    m_runButton->setEnabled(false);
    m_runButton->setText( tr( "Running…" ) );
    // showStep() evaluated isRunning() before this submission, so Next would
    // stay visibly clickable for the whole job while onNextStep's guard
    // silently drops every click (#1474 review). Disable it alongside Run;
    // restoreButton's updateStepDisplay() re-arms it from the terminal
    // callback — and the submission-rejected path below restores both.
    m_nextButton->setEnabled(false);

    // True when the student is still in the workflow the job belongs to (the
    // session boundary may have switched the view to another experiment).
    const auto onOwningWorkflow = [this, workflowId]()
    {
        return m_workflowActive && m_currentWorkflowIndex >= 0
               && m_currentWorkflowIndex < m_workflows.size()
               && m_workflows[m_currentWorkflowIndex].id == workflowId;
    };

    auto restoreButton = [this, workflowId, stepIndex]
    {
        m_runButton->setText( tr( "Run this step" ) );
        const bool onSameStep = m_workflowActive
            && m_currentWorkflowIndex >= 0
            && m_currentWorkflowIndex < m_workflows.size()
            && m_workflows[m_currentWorkflowIndex].id == workflowId
            && m_currentStepIndex == stepIndex;
        if ( onSameStep )
        {
            m_runButton->setEnabled( !m_jobHandle->isRunning() );
            updateStepDisplay();
        }
        else if ( m_workflowActive )
        {
            // A newer session owns the button now: refresh it against THAT
            // session's current step instead of force-disabling (which used
            // to dead-end the new session's Run until manual navigation).
            updateStepDisplay();
        }
        else
        {
            m_runButton->setEnabled( false );
        }
    };

    const long taskId = m_jobHandle->submitJob(
        req,
        [this, stepTitle, stepIndex, onOwningWorkflow, restoreButton]( const QString &outputPath, const Json::Value & )
        {
            restoreButton();
            // The outcome is reported in the owning workflow's transcript
            // only; the Task Center keeps the record for every other case.
            if ( onOwningWorkflow() )
            {
                // This step's outcome is now "succeeded": drop any earlier
                // failure record so the completion gate reflects the last run.
                m_failedSteps.remove( stepIndex );
                showRunMessage( tr( "\"%1\" finished. Output: %2" ).arg( stepTitle, outputPath ), false );
            }
        },
        [this, stepTitle, stepIndex, onOwningWorkflow, restoreButton]( const QString &error, bool canceled )
        {
            restoreButton();
            if ( !onOwningWorkflow() )
                return;
            if ( canceled )
            {
                // A cancelled step is an UNFINISHED step: the completion gate
                // must treat it like a failure, otherwise "Experiment
                // complete!" fires after cancelling the work that backs it.
                m_failedSteps.insert( stepIndex );
                showRunMessage( tr( "\"%1\" cancelled — run it again before finishing the experiment." ).arg( stepTitle ), true );
            }
            else
            {
                m_failedSteps.insert( stepIndex );
                showRunMessage( tr( "\"%1\" failed: %2 — fix the problem and run this step again; the experiment cannot be completed while a step has failed." ).arg( stepTitle, error ), true );
            }
        } );

    if ( taskId < 0 )
    {
        // Submission rejected (e.g. shutdown): no callback will fire.
        restoreButton();
        showRunMessage( tr( "Task submission was rejected; try again later." ), true );
    }
}

void GuidedWorkflowWidget::showRunMessage( const QString &message, bool isError )
{
    const QString color = isError ? QStringLiteral( "#b00" ) : QStringLiteral( "#060" );
    m_stepBrowser->append( QStringLiteral( "<p style=\"color:%1;\"><b>%2</b></p>" ).arg( color, message.toHtmlEscaped() ) );
}

void GuidedWorkflowWidget::showStep(int index)
{
    if (m_currentWorkflowIndex < 0 || m_currentWorkflowIndex >= m_workflows.size()) return;
    const auto &wf = m_workflows[m_currentWorkflowIndex];
    if (index < 0 || index >= wf.steps.size()) return;

    const auto &step = wf.steps[index];

    m_stepLabel->setText(
        tr( "<b>Step %1/%2: %3</b>" )
        .arg(index + 1)
        .arg(wf.steps.size())
        .arg(step.title.toHtmlEscaped())
    );

    QString binding;
    if ( step.hasOperator() )
        binding = tr( "<p><b>%1</b> <code>%2</code></p>" ).arg( tr( "Bound operator:" ), step.operatorId.toHtmlEscaped() );
    else if ( !step.action.isEmpty() )
        binding = tr( "<p><b>%1</b> <code>%2</code></p>" ).arg( tr( "UI action:" ), step.action.toHtmlEscaped() );

    QString hintHtml;
    if ( !step.teachingNote.isEmpty() )
        hintHtml += tr( "<p><b>%1</b> %2</p>" ).arg( tr( "Rationale:" ), step.teachingNote.toHtmlEscaped() );
    if ( !step.completionHint.isEmpty() )
        hintHtml += tr( "<p><b>%1</b> %2</p>" ).arg( tr( "Completion sign:" ), step.completionHint.toHtmlEscaped() );

    m_stepBrowser->setHtml(
        QString("<p><b>%1</b> %2</p>"
                "%3"
                "<hr>"
                "%4")
        .arg(tr( "Task:" ), step.description.toHtmlEscaped(), binding, hintHtml)
    );

    // Update navigation buttons
    m_prevButton->setEnabled(index > 0);
    // Next is state-driven exactly like Run (#1453/WP02): while this step's
    // operator job is in flight, "Experiment complete!" must not be
    // declarable — the completion branch below deactivates the workflow,
    // and the inactive-workflow guard then suppresses the job's eventual
    // failure message from this pane. restoreButton's updateStepDisplay()
    // re-arms Next when the job lands.
    m_nextButton->setEnabled(!m_jobHandle->isRunning());
    m_runButton->setEnabled(!step.isManual() && !m_jobHandle->isRunning());
}

void GuidedWorkflowWidget::updateStepDisplay()
{
    if (m_workflowActive && m_currentWorkflowIndex >= 0) {
        showStep(m_currentStepIndex);
    }
}
