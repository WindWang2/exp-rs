// rs_georef_task_list.h — georef warp job history / queue UI.
#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

/**
 * \brief Task list for Image Registration warp runs.
 *
 * Each Run enqueues a row (Running → Success / Failed / Cancelled)
 * with method, progress, RMS, duration, and output path.
 */
class RsGeorefTaskList : public QWidget
{
    Q_OBJECT
  public:
    enum class Kind
    {
      WarpI2I, ///< Image 2 Image warp
      WarpI2M, ///< Image 2 Map warp
    };

    enum class Status
    {
      Running,
      Success,
      Failed,
      Cancelled,
    };

    struct Entry
    {
      int id = 0;
      Kind kind = Kind::WarpI2I;
      Status status = Status::Running;
      QString title;
      QString detail;
      QString sourcePath;
      QString outputPath;
      QString methodLabel;
      double rmsPx = -1.0;
      int gcpCount = 0;
      int durationMs = 0;
      qint64 outputBytes = 0;
      double progress = 0.0; ///< 0–100 while running
      QDateTime startedAt;
      QDateTime finishedAt;
    };

    explicit RsGeorefTaskList( QWidget *parent = nullptr );

    int beginTask( Kind kind,
                   const QString &title,
                   const QString &methodLabel,
                   const QString &sourcePath,
                   const QString &outputPath,
                   int gcpCount,
                   double rmsPx );

    void setProgress( int id, double percent );
    void finishSuccess( int id, int durationMs, qint64 outputBytes = 0,
                        const QString &detail = QString() );
    void finishFailed( int id, const QString &error, int durationMs = 0 );
    void finishCancelled( int id, int durationMs = 0 );

    void clearFinished();
    void clearAll();

    int entryCount() const { return mEntries.size(); }
    int runningCount() const;
    bool hasRunning() const { return runningCount() > 0; }
    Entry entryAt( int row ) const;
    Entry entryById( int id ) const;

  signals:
    void openOutputRequested( const QString &path );
    void loadOutputRequested( const QString &path );
    void cancelTaskRequested( int taskId );

  private slots:
    void onDoubleClick( int row, int column );
    void onClearClicked();
    void onCancelClicked();
    void onContextMenu( const QPoint &pos );

  private:
    void rebuildTable();
    void updateSummary();
    void refreshRow( int id );
    int rowForId( int id ) const;
    static QString kindLabel( Kind k );
    static QString statusLabel( Status s );
    static QColor statusColor( Status s );

    QTableWidget *mTable = nullptr;
    QLabel *mSummary = nullptr;
    QPushButton *mClearBtn = nullptr;
    QPushButton *mCancelBtn = nullptr;
    QVector<Entry> mEntries;
    int mNextId = 1;
};
