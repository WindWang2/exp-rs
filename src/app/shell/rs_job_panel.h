/***************************************************************************
 * rs_job_panel.h  —  统一任务中心：列表 + 详情/日志 + 右键操作
 *
 * Read-only projection of Task Center Algorithm Tasks. Lifecycle actions
 * (cancel, clear completed) route through Task Center; this panel does not
 * own independent job state or submit to JobEngine.
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <json/json.h>

#include <QHash>
#include <QString>
#include <QStringList>

#include "processing/framework/task_center.h"

class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QTabWidget;
class QMenu;
class QPoint;
class QLabel;
namespace sicnu {
class RsEmptyStateWidget;
}

/**
 * Unified task list projected from Task Center.
 *
 * - 列表：标题 / 状态 / 进度 /「加载到主图」勾选
 * - 右键菜单：无任务时也可刷新/说明；有任务时显示详情、停止、加载输出等
 * - 详情页：方法 id、参数、输入输出、结果 JSON
 * - 成功后若勾选「加载到主图」则自动把输出路径加入主程序
 */
class RsJobPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit RsJobPanel( QWidget *parent = nullptr );

  public slots:
    void onTaskAdded( const sicnu::AlgorithmTaskInfo &info );
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );
    void onTaskLogAdded( long taskId, const QString &message );

  private slots:
    void onSelectionChanged();
    void onCancelClicked();
    void onClearFinishedClicked();
    void onFilterChanged();
    void onContextMenuRequested( const QPoint &pos );
    void onItemChanged( QTreeWidgetItem *item, int column );
    void onItemDoubleClicked( QTreeWidgetItem *item, int column );

  private:
    void setupUi();
    void applyHelpTips();
    void refreshAll();
    void upsertTaskRow( const sicnu::AlgorithmTaskInfo &info );
    QTreeWidgetItem *findTaskItem( long taskId ) const;
    void fillLogForTask( long taskId );
    void fillDetailsForTask( long taskId );
    void updateActionEnabled();
    long selectedTaskId() const;
    bool confirmDangerous( const QString &title, const QString &body ) const;
    bool passesFilter( const QString &stateText ) const;
    static QString statusToString( sicnu::TaskStatus status );
    static QString formatProgress( double progress );
    static QString formatEta( const sicnu::AlgorithmTaskInfo &info );
    static QString prettyJson( const std::string &jsonText );
    static QString prettyJsonValue( const Json::Value &v );

    bool loadToMainPreference( long taskId ) const;
    void setLoadToMainPreference( long taskId, bool on );
    QStringList collectOutputPaths( long taskId ) const;
    int loadPathsToMain( const QStringList &paths );
    void tryAutoLoadOutputs( const sicnu::AlgorithmTaskInfo &info );
    void showAboutDialog();
    void copyText( const QString &text );

    QTreeWidget *m_jobTree = nullptr;
    class QStackedWidget *m_treeStack = nullptr;
    sicnu::RsEmptyStateWidget *m_emptyState = nullptr;
    QTabWidget *m_detailTabs = nullptr;
    QPlainTextEdit *m_detailView = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_loadBtn = nullptr;
    QPushButton *m_clearFinishedBtn = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QLabel *m_hintLabel = nullptr;
    long m_selectedId = -1;
    /// Per-task "load outputs to main map" preference (UI override of autoLoad).
    QHash<long, bool> m_loadToMain;
    bool m_blockItemChanged = false;
};
