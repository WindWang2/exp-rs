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
 * Each Apply/Run enqueues a row (Running → Success / Failed / Cancelled)
 * with method, RMS, duration, and output path.
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
      QString title;       ///< e.g. transform method name
      QString detail;      ///< error or note
      QString sourcePath;
      QString outputPath;
      QString methodLabel;
      double rmsPx = -1.0; ///< <0 = n/a
      int gcpCount = 0;
      int durationMs = 0;
      qint64 outputBytes = 0;
      QDateTime startedAt;
      QDateTime finishedAt;
    };

    explicit RsGeorefTaskList( QWidget *parent = nullptr );

    /// Start a new running entry; returns id for later finish*.
    int beginTask( Kind kind,
                   const QString &title,
                   const QString &methodLabel,
                   const QString &sourcePath,
                   const QString &outputPath,
                   int gcpCount,
                   double rmsPx );

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

  signals:
    void openOutputRequested( const QString &path );

  private slots:
    void onDoubleClick( int row, int column );
    void onClearClicked();

  private:
    void rebuildTable();
    void updateSummary();
    static QString kindLabel( Kind k );
    static QString statusLabel( Status s );
    static QColor statusColor( Status s );

    QTableWidget *mTable = nullptr;
    QLabel *mSummary = nullptr;
    QPushButton *mClearBtn = nullptr;
    QVector<Entry> mEntries;
    int mNextId = 1;
};
