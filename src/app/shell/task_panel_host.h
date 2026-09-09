/***************************************************************************
 * task_panel_host.h  —  right-side task panel host for atomic RS tools
 *
 * The standard processing surface (Desktop Workbench UX 4.0):
 *   schema form → inline validation → (estimate line) → run/stop →
 *   TaskCenter tracking → structured result summary.
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
class RsResultSummary;

/**
 * Host widget for single-tool (atomic) workflow UI.
 *
 * Layout (top → bottom): title, help summary, schema form (inline
 * validation), estimate line, progress, hint/error line, structured result
 * summary, action bar (Help | load-to-map | Run/Stop | Close).
 */
class TaskPanelHost : public QWidget
{
    Q_OBJECT
  public:
    explicit TaskPanelHost( QWidget *parent = nullptr );

    void showTool( const QString &title, const QString &helpSummary, const Json::Value &schema,
                   const QString &operatorHelpContext = QString() );
    void setRasterLayerChoices( const QStringList &ids, const QStringList &names );
    void setVectorLayerChoices( const QStringList &ids, const QStringList &names );
    void setAssetChoices( const QStringList &ids, const QStringList &names );
    void setModelChoices( const QStringList &names );
    void setHints( const QStringList &hints );
    void setRunning( bool running );
    void setSuccess( const QString &message );
    void setFailed( const QString &message );

    /** Preflight/resource estimate line shown above the progress bar. */
    void setEstimate( const QString &estimate );

    /** Render the completed operator result in the shared summary view. */
    void showResult( const Json::Value &result,
                     const QString &operatorId = QString(),
                     qint64 elapsedMs = -1,
                     bool fromCache = false );

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
    /// User double-clicked an output artifact in the result summary.
    void resultOpenRequested( const QString &path );

  private:
    void applyHintStyle( bool isError );
    void onActionButtonClicked();
    void updateRunButtonState();

    bool m_running = false;
    bool m_validationBlocked = false;

    QLabel *m_title = nullptr;
    QLabel *m_help = nullptr;
    SchemaFormBuilder *m_form = nullptr;
    QLabel *m_estimate = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_hint = nullptr;
    RsResultSummary *m_resultSummary = nullptr;
    QCheckBox *m_loadToMap = nullptr;
    QPushButton *m_helpBtn = nullptr;
    QPushButton *m_runBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
