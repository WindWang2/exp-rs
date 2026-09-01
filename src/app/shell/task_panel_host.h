/***************************************************************************
 * task_panel_host.h  —  right-side task panel host for atomic RS tools
 ***************************************************************************/
#pragma once

#include "schema_form_builder.h"

#include <QWidget>
#include <QStringList>
#include <json/json.h>

class QLabel;
class QProgressBar;
class QPushButton;
class QCheckBox;

/**
 * Host widget for single-tool (atomic) workflow UI.
 *
 * Layout (top → bottom): title, help summary, schema form, progress,
 * hint/error line, action bar (Help | load-to-map | Run | Close).
 */
class TaskPanelHost : public QWidget
{
    Q_OBJECT
  public:
    explicit TaskPanelHost( QWidget *parent = nullptr );

    void showTool( const QString &title, const QString &helpSummary, const Json::Value &schema );
    void setRasterLayerChoices( const QStringList &ids, const QStringList &names );
    void setHints( const QStringList &hints );
    void setRunning( bool running );
    void setSuccess( const QString &message );
    void setFailed( const QString &message );

    Json::Value formValues() const;
    void setFormValues( const Json::Value &v );
    bool loadResultToMap() const;
    SchemaFormBuilder *form() const;

  signals:
    void runClicked();
    /// #704: the Run button becomes a Stop button while a task is in flight;
    /// owners cancel their pending task/pipeline on this signal.
    void stopClicked();
    void helpClicked();
    void closeClicked();

  private:
    void applyHintStyle( bool isError );
    void onActionButtonClicked();

    bool m_running = false;

    QLabel *m_title = nullptr;
    QLabel *m_help = nullptr;
    SchemaFormBuilder *m_form = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_hint = nullptr;
    QCheckBox *m_loadToMap = nullptr;
    QPushButton *m_helpBtn = nullptr;
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
