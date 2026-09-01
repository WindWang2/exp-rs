#ifndef TASK_CENTER_DOCK_H
#define TASK_CENTER_DOCK_H

#include <QDockWidget>
#include <QTreeWidget>
#include <QTextBrowser>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QVariantMap>
#include <QStackedWidget>

#include "processing/framework/task_center.h"

namespace sicnu {

class TaskCenterDock : public QDockWidget {
    Q_OBJECT
public:
    explicit TaskCenterDock(QWidget *parent = nullptr);
    ~TaskCenterDock() override = default;

    bool autoLoadLayers() const;
    QString formatStatus(TaskStatus status) const;
    QString formatPriority(TaskPriority priority) const;

public slots:
    void refreshTaskList();
    void onTaskAdded(const AlgorithmTaskInfo& info);
    void onTaskUpdated(const AlgorithmTaskInfo& info);
    void onTaskLogAdded(long taskId, const QString& message);
    void onSelectionChanged();

signals:
    void layerAutoLoadRequested(const QString& filePath);

private slots:
    void onCancelClicked();
    void onPauseClicked();
    void onResumeClicked();
    void onRetryClicked();
    void onClearCompletedClicked();

private:
    void setupUi();
    long selectedTaskId() const;

    QTreeWidget *m_taskTree = nullptr;
    QStackedWidget *m_treeStack = nullptr;
    class RsEmptyStateWidget *m_emptyState = nullptr;
    QTextBrowser *m_parameterBrowser = nullptr;
    QTextBrowser *m_logBrowser = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_resumeBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QCheckBox *m_autoLoadCheckBox = nullptr;
};

} // namespace sicnu

#endif // TASK_CENTER_DOCK_H
