/***************************************************************************
 * rs_job_panel.h  —  bottom dock listing JobEngine jobs + selected log
 ***************************************************************************/
#pragma once

#include <qgsdockwidget.h>

#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QSplitter;

/**
 * Dock widget for unified JobEngine observability.
 *
 * Left: job list (title, state, progress). Right: selected job log.
 * Actions: Cancel selected, Clear finished. Optional state filter.
 */
class RsJobPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit RsJobPanel( QWidget *parent = nullptr );

  public slots:
    void onJobUpdated( const QString &jobId );
    void onJobFinished( const QString &jobId );

  private slots:
    void onSelectionChanged();
    void onCancelClicked();
    void onClearFinishedClicked();
    void onFilterChanged();

  private:
    void setupUi();
    void refreshAll();
    void upsertJobRow( const QString &jobId );
    void fillLogForJob( const QString &jobId );
    QString selectedJobId() const;
    bool passesFilter( const QString &stateText ) const;
    static QString stateToString( int state ); // JobState as int for storage
    static QString formatProgress( double progress );

    QTreeWidget *m_jobTree = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_clearFinishedBtn = nullptr;
    QComboBox *m_filterCombo = nullptr;
    QString m_selectedId;
};
