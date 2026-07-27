#include "task_center_dock.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <algorithm>

namespace sicnu {

TaskCenterDock::TaskCenterDock(QWidget *parent)
    : QDockWidget(tr("任务中心 (Task Center)"), parent)
{
    setObjectName(QStringLiteral("TaskCenterDock"));
    setupUi();

    connect(&TaskCenter::instance(), &TaskCenter::taskAdded, this, &TaskCenterDock::onTaskAdded, Qt::QueuedConnection);
    connect(&TaskCenter::instance(), &TaskCenter::taskUpdated, this, &TaskCenterDock::onTaskUpdated, Qt::QueuedConnection);
    connect(&TaskCenter::instance(), &TaskCenter::taskLogAdded, this, &TaskCenterDock::onTaskLogAdded, Qt::QueuedConnection);
    connect(&TaskCenter::instance(), &TaskCenter::layerAutoLoadRequested, this, &TaskCenterDock::layerAutoLoadRequested);

    refreshTaskList();
}

void TaskCenterDock::setupUi()
{
    QWidget *mainWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QSplitter *splitter = new QSplitter(Qt::Vertical, mainWidget);

    m_taskTree = new QTreeWidget(splitter);
    m_taskTree->setHeaderLabels({tr("ID"), tr("算法名称"), tr("优先级"), tr("状态"), tr("进度"), tr("已用时间"), tr("预计剩余")});
    m_taskTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_taskTree->setSelectionMode(QAbstractItemView::SingleSelection);

    QTabWidget *detailsTab = new QTabWidget(splitter);
    m_parameterBrowser = new QTextBrowser(detailsTab);
    m_logBrowser = new QTextBrowser(detailsTab);

    detailsTab->addTab(m_parameterBrowser, tr("输入参数"));
    detailsTab->addTab(m_logBrowser, tr("运行日志"));

    splitter->addWidget(m_taskTree);
    splitter->addWidget(detailsTab);
    splitter->setSizes({200, 150});

    mainLayout->addWidget(splitter);

    // Bottom action buttons bar
    QHBoxLayout *actionLayout = new QHBoxLayout();

    m_autoLoadCheckBox = new QCheckBox(tr("自动加载结果图层"), mainWidget);
    m_autoLoadCheckBox->setChecked(true);
    actionLayout->addWidget(m_autoLoadCheckBox);

    actionLayout->addStretch();

    m_pauseBtn = new QPushButton(tr("暂停"), mainWidget);
    m_resumeBtn = new QPushButton(tr("恢复"), mainWidget);
    m_cancelBtn = new QPushButton(tr("取消"), mainWidget);
    m_retryBtn = new QPushButton(tr("重试"), mainWidget);
    m_clearBtn = new QPushButton(tr("清除已完成"), mainWidget);

    actionLayout->addWidget(m_pauseBtn);
    actionLayout->addWidget(m_resumeBtn);
    actionLayout->addWidget(m_cancelBtn);
    actionLayout->addWidget(m_retryBtn);
    actionLayout->addWidget(m_clearBtn);

    mainLayout->addLayout(actionLayout);
    setWidget(mainWidget);

    connect(m_taskTree, &QTreeWidget::itemSelectionChanged, this, &TaskCenterDock::onSelectionChanged);
    connect(m_pauseBtn, &QPushButton::clicked, this, &TaskCenterDock::onPauseClicked);
    connect(m_resumeBtn, &QPushButton::clicked, this, &TaskCenterDock::onResumeClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TaskCenterDock::onCancelClicked);
    connect(m_retryBtn, &QPushButton::clicked, this, &TaskCenterDock::onRetryClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &TaskCenterDock::onClearCompletedClicked);

    onSelectionChanged();
}

bool TaskCenterDock::autoLoadLayers() const
{
    return m_autoLoadCheckBox ? m_autoLoadCheckBox->isChecked() : true;
}

QString TaskCenterDock::formatStatus(TaskStatus status) const
{
    switch (status) {
        case TaskStatus::Queued: return tr("排队中");
        case TaskStatus::Running: return tr("运行中");
        case TaskStatus::Paused: return tr("已暂停");
        case TaskStatus::Completed: return tr("已完成");
        case TaskStatus::Failed: return tr("失败");
        case TaskStatus::Canceled: return tr("已取消");
        default: return tr("未知");
    }
}

QString TaskCenterDock::formatPriority(TaskPriority priority) const
{
    switch (priority) {
        case TaskPriority::High: return QStringLiteral("[高]");
        case TaskPriority::Normal: return QStringLiteral("[中]");
        case TaskPriority::Low: return QStringLiteral("[低]");
        default: return QStringLiteral("[中]");
    }
}

void TaskCenterDock::refreshTaskList()
{
    m_taskTree->clear();
    const auto tasks = TaskCenter::instance().allTasks();

    QMap<long, QTreeWidgetItem*> itemMap;

    // First pass: add parent items
    for (const auto& info : tasks) {
        if (info.parentTaskIds.isEmpty()) {
            QTreeWidgetItem *item = new QTreeWidgetItem(m_taskTree);
            item->setText(0, QString::number(info.taskId));
            item->setText(1, info.algorithmName);
            item->setText(2, formatPriority(info.priority));
            item->setText(3, formatStatus(info.status));
            item->setText(4, QString("%1%").arg(static_cast<int>(info.progressPercentage * 100)));

            qint64 elapsedSecs = info.startTime.secsTo(info.endTime.isValid() ? info.endTime : QDateTime::currentDateTime());
            item->setText(5, QString("%1s").arg(elapsedSecs));
            item->setText(6, QStringLiteral("--"));
            itemMap.insert(info.taskId, item);
        }
    }

    // Second pass: add dependent child items
    for (const auto& info : tasks) {
        if (!info.parentTaskIds.isEmpty()) {
            QTreeWidgetItem *parentItem = itemMap.value(info.parentTaskIds.first(), nullptr);
            QTreeWidgetItem *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(m_taskTree);

            item->setText(0, QString::number(info.taskId));
            item->setText(1, info.algorithmName);
            item->setText(2, formatPriority(info.priority));
            item->setText(3, formatStatus(info.status));
            item->setText(4, QString("%1%").arg(static_cast<int>(info.progressPercentage * 100)));

            qint64 elapsedSecs = info.startTime.secsTo(info.endTime.isValid() ? info.endTime : QDateTime::currentDateTime());
            item->setText(5, QString("%1s").arg(elapsedSecs));
            item->setText(6, QStringLiteral("--"));
            itemMap.insert(info.taskId, item);
        }
    }
    m_taskTree->expandAll();
}

void TaskCenterDock::onTaskAdded(const AlgorithmTaskInfo& info)
{
    Q_UNUSED(info);
    refreshTaskList();
}

void TaskCenterDock::onTaskUpdated(const AlgorithmTaskInfo& info)
{
    Q_UNUSED(info);
    refreshTaskList();
    onSelectionChanged();
}

void TaskCenterDock::onTaskLogAdded(long taskId, const QString& message)
{
    if (selectedTaskId() == taskId && m_logBrowser) {
        m_logBrowser->append(message);
    }
}

long TaskCenterDock::selectedTaskId() const
{
    QList<QTreeWidgetItem*> items = m_taskTree->selectedItems();
    if (items.isEmpty() || !items.first()) return -1;
    return items.first()->text(0).toLong();
}

void TaskCenterDock::onSelectionChanged()
{
    long id = selectedTaskId();
    if (id < 0) {
        m_parameterBrowser->clear();
        m_logBrowser->clear();
        m_pauseBtn->setEnabled(false);
        m_resumeBtn->setEnabled(false);
        m_cancelBtn->setEnabled(false);
        m_retryBtn->setEnabled(false);
        return;
    }

    auto info = TaskCenter::instance().getTaskInfo(id);

    QJsonObject obj;
    for (auto it = info.parameterMap.begin(); it != info.parameterMap.end(); ++it) {
        obj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    m_parameterBrowser->setText(QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented)));
    m_logBrowser->setText(info.logBuffer.join(QLatin1Char('\n')));

    m_pauseBtn->setEnabled(info.status == TaskStatus::Running);
    m_resumeBtn->setEnabled(info.status == TaskStatus::Paused);
    m_cancelBtn->setEnabled(info.status == TaskStatus::Queued || info.status == TaskStatus::Running || info.status == TaskStatus::Paused);
    m_retryBtn->setEnabled(info.status == TaskStatus::Failed || info.status == TaskStatus::Canceled);
}

void TaskCenterDock::onCancelClicked()
{
    long id = selectedTaskId();
    if (id > 0) {
        TaskCenter::instance().cancelTask(id);
    }
}

void TaskCenterDock::onPauseClicked()
{
    long id = selectedTaskId();
    if (id > 0) {
        TaskCenter::instance().pauseTask(id);
    }
}

void TaskCenterDock::onResumeClicked()
{
    long id = selectedTaskId();
    if (id > 0) {
        TaskCenter::instance().resumeTask(id);
    }
}

void TaskCenterDock::onRetryClicked()
{
    long id = selectedTaskId();
    if (id > 0) {
        TaskCenter::instance().retryTask(id);
    }
}

void TaskCenterDock::onClearCompletedClicked()
{
    TaskCenter::instance().clearCompletedTasks();
    refreshTaskList();
}

} // namespace sicnu
