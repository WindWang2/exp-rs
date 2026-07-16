#ifndef RS_CLASSIFY_SESSION_STATE_H
#define RS_CLASSIFY_SESSION_STATE_H

#include <QString>

class QWidget;

/**
 * \brief Dirty flag + Classification/* QSettings for workflow continuity.
 * Does not own ROIs, canvases, or classifier backends.
 */
class RsClassifySessionState
{
  public:
    struct Snapshot
    {
      QString lastSourcePath;
      QString lastOutputPath;
      QString lastRoisPath;
      QString lastModelPath;
      int classifierKind = 0;
      double trainRatio = 0.7;
      double wandTolerance = 20.0;
    };

    bool isDirty() const { return mDirty; }
    void markDirty() { mDirty = true; }
    void clearDirty() { mDirty = false; }

    QString lastRoisPath() const { return mLastRoisPath; }
    void setLastRoisPath( const QString &path );

    void saveWindow( QWidget *w );
    void restoreWindow( QWidget *w );

    void saveSnapshot( const Snapshot &s );
    /// Reads Classification/* keys and hydrates \a mLastRoisPath so
    /// \ref lastRoisPath() is correct after process restart.
    Snapshot restoreSnapshot();

  private:
    bool mDirty = false;
    QString mLastRoisPath;
};

#endif
