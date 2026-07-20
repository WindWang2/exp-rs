#ifndef RS_GEOREF_SESSION_STATE_H
#define RS_GEOREF_SESSION_STATE_H

#include <QString>

class QWidget;

/**
 * \brief Dirty flag + Georeferencer/* QSettings for workflow continuity.
 * Does not own GCPs or canvases.
 */
class RsGeorefSessionState
{
  public:
    struct WorkflowSnapshot
    {
      int mode = 0;
      int transformMethod = 0;
      int resamplingMethod = 0;
      QString lastSourcePath;
      QString lastRefPath;
      QString lastOutputPath;
      QString lastDemPath;
      QString lastPointsPath;
      /// Destination CRS auth id (e.g. "EPSG:32650"); empty if unset.
      QString lastDestCrsAuthId;
      /// DEM height offset (metres) for RPC; default 0.
      double demZOffset = 0.0;
      bool syncZoom = true;
    };

    bool isDirty() const { return mDirty; }
    void markDirty() { mDirty = true; }
    void clearDirty() { mDirty = false; }

    QString lastPointsPath() const { return mLastPointsPath; }
    void setLastPointsPath( const QString &path );

    void saveWindow( QWidget *w );
    void restoreWindow( QWidget *w );

    void saveWorkflow( const WorkflowSnapshot &s );
    /// Reads workflow keys from QSettings and hydrates \a mLastPointsPath
    /// so \ref lastPointsPath() is correct after process restart (Task 2 close-save).
    WorkflowSnapshot restoreWorkflow();

  private:
    bool mDirty = false;
    QString mLastPointsPath;
};

#endif
