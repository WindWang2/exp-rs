// guided_workflow_widget.cpp — In-app guided workflow for RS lab exercises
#include "guided_workflow_widget.h"
#include "main_window.h"

#include "jobs/job_types.h"
#include "processing/framework/runtime_paths.h"

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
    , m_jobHandle(this)
{
    setupUi();
    loadWorkflows();
}

void GuidedWorkflowWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Title
    auto *titleLabel = new QLabel(tr("<b>引导式实验</b>"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    // Splitter: workflow list on left, step details on right
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("rsGuidedWorkflowSplitter"));

    // Left: workflow list
    auto *leftWidget = new QWidget();
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    leftLayout->addWidget(new QLabel(tr("选择实验："), this));
    m_workflowList = new QListWidget(this);
    m_workflowList->setToolTip(tr("选择一个引导式实验。" ));
    connect(m_workflowList, &QListWidget::currentRowChanged, this, &GuidedWorkflowWidget::onWorkflowSelected);
    leftLayout->addWidget(m_workflowList);

    m_startButton = new QPushButton(tr("开始实验"), this);
    m_startButton->setToolTip(tr("开始所选实验。" ));
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
    m_prevButton = new QPushButton(tr("上一步"), this);
    m_prevButton->setToolTip(tr("返回上一个步骤。" ));
    m_prevButton->setEnabled(false);
    connect(m_prevButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onPreviousStep);
    navLayout->addWidget(m_prevButton);

    m_runButton = new QPushButton(tr("执行此步"), this);
    m_runButton->setToolTip(tr("执行当前步骤绑定的算子或界面操作。" ));
    m_runButton->setEnabled(false);
    m_runButton->setProperty("primary", true);
    connect(m_runButton, &QPushButton::clicked, this, &GuidedWorkflowWidget::onRunStepAction);
    navLayout->addWidget(m_runButton);

    m_nextButton = new QPushButton(tr("下一步"), this);
    m_nextButton->setToolTip(tr("完成当前步骤后进入下一步。" ));
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
    m_loadResult = lab::loadLabSpecsFromDir( lab::defaultLabDirectory() );

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
        m_workflowList->addItem( tr( "⚠ 实验规格加载失败（%1 项）" ).arg( errorCount ) );
        QListWidgetItem *errorItem = m_workflowList->item( 0 );
        errorItem->setToolTip( m_loadResult.errors.first().toString() );
    }

    for ( const auto &wf : m_workflows )
        m_workflowList->addItem( wf.title );

    if ( errorCount > 0 )
        m_workflowList->setCurrentRow( 0 );
}

/// Rows [0 .. errorCount-1] are the error entry; labs follow.
static int labIndexOfRow( int row, int errorCount ) { return row - errorCount; }

void GuidedWorkflowWidget::onWorkflowSelected(int index)
{
    const int errorCount = m_loadResult.errors.size();
    if ( errorCount > 0 && index == 0 )
    {
        m_startButton->setEnabled(false);
        m_stepLabel->setText( tr( "<b>实验规格加载失败</b>" ) );
        QString errorHtml = tr( "<p>以下 LabSpec 文件无法加载，请修复后重启或重新打开本面板：</p><ul>" );
        for ( const auto &error : m_loadResult.errors )
            errorHtml += QStringLiteral( "<li><code>%1</code></li>" ).arg( error.toString().toHtmlEscaped() );
        errorHtml += QLatin1String( "</ul>" );
        m_stepBrowser->setHtml( errorHtml );
        return;
    }

    const int labIndex = labIndexOfRow( index, errorCount );
    if ( labIndex < 0 || labIndex >= m_workflows.size() ) return;

    m_currentWorkflowIndex = labIndex;
    m_startButton->setEnabled(true);

    // Show workflow description
    const auto &wf = m_workflows[labIndex];
    m_stepLabel->setText(QString("<b>%1</b>").arg(wf.title.toHtmlEscaped()));

    QString stepsHtml;
    for (int i = 0; i < wf.steps.size(); i++)
        stepsHtml += QString("<li>%1</li>").arg(wf.steps[i].title.toHtmlEscaped());

    m_stepBrowser->setHtml(
        QString("<p>%1</p><p><b>%2</b></p><ol>%3</ol>"
                "<p>%4</p>")
        .arg(wf.description.toHtmlEscaped(),
             tr("步骤"),
             stepsHtml,
             tr("点击 <b>开始实验</b> 以开始。"))
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
        m_stepLabel->setText(tr("<b>实验完成！</b>"));
        m_stepBrowser->setHtml(
            tr("<p>恭喜！你已完成 <b>%1</b> 实验。</p>"
               "<p>可以继续尝试其他实验，或调整参数进行更多探索。</p>")
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

    const auto &wf = m_workflows[m_currentWorkflowIndex];
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
            showRunMessage( tr( "主窗口上不存在操作 “%1”，实验规格可能已过期。" ).arg( step.action ), true );
        return;
    }
}

void GuidedWorkflowWidget::runOperatorStep( const WorkflowStep &step )
{
    if ( m_jobHandle.isRunning() )
    {
        showRunMessage( tr( "已有任务正在运行，请等待其完成后再执行下一步。" ), true );
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
    std::function<void( const Json::Value & )> ensureOutputDirs = [&]( const Json::Value &node )
    {
        if ( node.isString() )
        {
            const QString value = QString::fromStdString( node.asString() );
            if ( value.contains( QStringLiteral( "/output/labs/" ) ) )
            {
                const QString outputDir = QFileInfo( value ).absolutePath();
                if ( !QDir().mkpath( outputDir ) )
                {
                    showRunMessage( tr( "无法创建输出目录：%1" ).arg( outputDir ), true );
                    return;
                }
            }
            return;
        }
        if ( node.isArray() )
        {
            for ( const auto &item : node )
                ensureOutputDirs( item );
            return;
        }
        if ( node.isObject() )
        {
            for ( const auto &key : node.getMemberNames() )
                ensureOutputDirs( node[ key ] );
        }
    };
    ensureOutputDirs( params );

    sicnu::jobs::JobRequest req;
    req.algorithmId = step.operatorId.toStdString();
    req.params = params;
    req.title = m_workflows[m_currentWorkflowIndex].title.toStdString();
    req.source = "guided_lab";

    // Identify the step this submission belongs to: completion restores the
    // run button only when the student is still looking at the same step.
    const QString workflowId = m_workflows[m_currentWorkflowIndex].id;
    const int stepIndex = m_currentStepIndex;
    const QString stepTitle = step.title;

    m_runButton->setEnabled(false);
    m_runButton->setText( tr( "运行中…" ) );

    auto restoreButton = [this, workflowId, stepIndex]
    {
        m_runButton->setText( tr( "执行此步" ) );
        const bool onSameStep = m_workflowActive
            && m_currentWorkflowIndex >= 0
            && m_workflows[m_currentWorkflowIndex].id == workflowId
            && m_currentStepIndex == stepIndex;
        m_runButton->setEnabled( onSameStep && !m_jobHandle.isRunning() );
        if ( onSameStep )
            updateStepDisplay();
    };

    const long taskId = m_jobHandle.submitJob(
        req,
        [this, stepTitle, restoreButton]( const QString &outputPath, const Json::Value & )
        {
            restoreButton();
            showRunMessage( tr( "“%1” 完成。输出：%2" ).arg( stepTitle, outputPath ), false );
        },
        [this, stepTitle, restoreButton]( const QString &error, bool canceled )
        {
            restoreButton();
            if ( canceled )
                showRunMessage( tr( "“%1” 已取消。" ).arg( stepTitle ), true );
            else
                showRunMessage( tr( "“%1” 失败：%2" ).arg( stepTitle, error ), true );
        } );

    if ( taskId < 0 )
    {
        // Submission rejected (e.g. shutdown): no callback will fire.
        restoreButton();
        showRunMessage( tr( "任务提交被拒绝，请稍后重试。" ), true );
    }
}

void GuidedWorkflowWidget::showRunMessage( const QString &message, bool isError )
{
    const QString color = isError ? QStringLiteral( "#b00" ) : QStringLiteral( "#060" );
    m_stepBrowser->append( QStringLiteral( "<p style=\"color:%1;\"><b>%2</b></p>" ).arg( color, message.toHtmlEscaped() ) );
}

void GuidedWorkflowWidget::showStep(int index)
{
    const auto &wf = m_workflows[m_currentWorkflowIndex];
    if (index < 0 || index >= wf.steps.size()) return;

    const auto &step = wf.steps[index];

    m_stepLabel->setText(
        tr("<b>步骤 %1/%2：%3</b>")
        .arg(index + 1)
        .arg(wf.steps.size())
        .arg(step.title.toHtmlEscaped())
    );

    QString binding;
    if ( step.hasOperator() )
        binding = tr( "<p><b>%1</b> <code>%2</code></p>" ).arg( tr( "绑定算子：" ), step.operatorId.toHtmlEscaped() );
    else if ( !step.action.isEmpty() )
        binding = tr( "<p><b>%1</b> <code>%2</code></p>" ).arg( tr( "界面操作：" ), step.action.toHtmlEscaped() );

    QString hintHtml;
    if ( !step.teachingNote.isEmpty() )
        hintHtml += tr( "<p><b>%1</b> %2</p>" ).arg( tr( "原理：" ), step.teachingNote.toHtmlEscaped() );
    if ( !step.completionHint.isEmpty() )
        hintHtml += tr( "<p><b>%1</b> %2</p>" ).arg( tr( "完成标志：" ), step.completionHint.toHtmlEscaped() );

    m_stepBrowser->setHtml(
        QString("<p><b>%1</b> %2</p>"
                "%3"
                "<hr>"
                "%4")
        .arg(tr("任务："), step.description.toHtmlEscaped(), binding, hintHtml)
    );

    // Update navigation buttons
    m_prevButton->setEnabled(index > 0);
    m_nextButton->setEnabled(true);
    m_runButton->setEnabled(!step.isManual() && !m_jobHandle.isRunning());
}

void GuidedWorkflowWidget::updateStepDisplay()
{
    if (m_workflowActive && m_currentWorkflowIndex >= 0) {
        showStep(m_currentStepIndex);
    }
}
