// rs_georeferencing_session.h — Shared Georeferencing Session (#32)
//
// Owns GCP pairing, transform fit readiness, per-point residual summary, and
// immutable warp snapshots. Warp execution is submitted through an injected
// executor seam (ADR 0020 decision 3); production delegates to Task Center.
#pragma once

#include <QSemaphore>
#include <QObject>

#include <atomic>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcppoint.h"
#include "qgsgcptransformer.h"
#include "qgsgeoreftransform.h"
#include "qgsimagewarper.h"
#include "qgspointxy.h"

#include "processing/framework/task_center.h"
#include "workflow/builtin_definitions.h"
#include "workflow/workflow_runtime.h"

class QWidget;
class RsWarpTask;

struct CustomWarpExecutor
{
  std::function<long( const sicnu::jobs::JobRequest &request,
                      const sicnu::TaskCenter::JobExecutor &executor,
                      const sicnu::TaskCenter::CancelHook &onCancel )> submit;
  std::function<bool( long taskId )> cancel;
};

/**
 * Immutable warp request frozen at enqueue time. Mutating the live session
 * after createWarpSnapshot() must not change fields of an existing snapshot.
 */
struct RsGeorefWarpSnapshot
{
  QString sourcePath;
  QString outputPath;
  QVector<QgsGcpPoint> gcps; // includes disabled; warp uses enabled only
  QgsGcpTransformerInterface::TransformMethod method =
    QgsGcpTransformerInterface::TransformMethod::Linear;
  QgsImageWarper::ResamplingMethod resampling =
    QgsImageWarper::ResamplingMethod::NearestNeighbour;
  QgsCoordinateReferenceSystem destCrs;
  QString demPath;
  double demZOffset = 0.0;
  double pixelSize = 0.0;
  double rmsAtCapture = -1.0;
  int backgroundValue = 0;
};

/**
 * Deep Georeferencing Session (ADR 0027, ADR 0028).
 *
 * Single deep module owning GCP pairing, transform fitting, residual calculations,
 * dirty state tracking, QSettings snapshot persistence, WorkflowRuntime session mirror
 * (lab.georef.image_to_map), and Task Center warp task dispatch.
 */
class RsGeoreferencingSession : public QObject
{
  Q_OBJECT
  public:
    explicit RsGeoreferencingSession( QObject *parent = nullptr );
    explicit RsGeoreferencingSession( CustomWarpExecutor customExecutor,
                                      QObject *parent = nullptr );
    void setCustomWarpExecutor( CustomWarpExecutor executor ) { mCustomExecutor = std::move( executor ); }
    ~RsGeoreferencingSession() override;
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
      QString lastDestCrsAuthId;
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
    WorkflowSnapshot restoreWorkflow();

    /// WorkflowRuntime mirror for lab.georef.image_to_map (ADR 0028).
    bool enableWorkflowMirror( const std::string &definitionId = "lab.georef.image_to_map" );
    bool isWorkflowMirrorActive() const { return !mWorkflowSessionId.empty(); }
    const std::string &workflowSessionId() const { return mWorkflowSessionId; }
    const sicnu::workflow::WorkflowRuntime &workflowRuntime() const { return mWorkflowRuntime; }
    void setWorkflowStep( const std::string &stepId );
    void markWorkflowStepComplete( const std::string &stepId );

    void setSourceRasterPath( const QString &path );
    QString sourceRasterPath() const { return mSourcePath; }

    void setTransformMethod( QgsGcpTransformerInterface::TransformMethod method );
    QgsGcpTransformerInterface::TransformMethod transformMethod() const { return mMethod; }

    /// RPC transform options (mirror of the params panel DEM row). Only used
    /// when the method is RpcPhysical; injected into QgsRpcGcpTransformer
    /// during refit().
    void setDemPath( const QString &path );
    QString demPath() const { return mDemPath; }
    void setDemZOffset( double z );
    double demZOffset() const { return mDemZOffset; }

    void setDestinationCrs( const QgsCoordinateReferenceSystem &crs );
    QgsCoordinateReferenceSystem destinationCrs() const { return mDestCrs; }

    void setGcps( const QVector<QgsGcpPoint> &gcps );
    const QVector<QgsGcpPoint> &gcps() const { return mGcps; }
    void clearGcps();

    /**
     * Granular GCP mutations (ADR 0020 S2): the session is the sole owner of
     * the GCP list. Each mutation emits gcpsChanged() and re-runs refit()
     * (which emits fitChanged). setGcps() stays wholesale and does NOT refit
     * (callers refit explicitly), preserving the stale-fit snapshot gating.
     */
    void addGcp( const QgsGcpPoint &gcp );
    void appendGcps( const QVector<QgsGcpPoint> &gcps );
    void removeGcpAt( int row );
    void setGcpEnabled( int row, bool enabled );
    void setGcpSource( int row, const QgsPointXY &source );
    void setGcpDestination( int row, const QgsPointXY &destination );
    void setGcpPointType( int row, const QString &pointType );

    /// Re-estimate transform parameters from enabled GCPs.
    RsGeorefFitResult refit();
    const RsGeorefFitResult &lastFit() const { return mLastFit; }
    bool isFitReady() const { return mLastFit.ready; }

    /// Frozen snapshot for a warp job. Does not start execution.
    /// Returns nullopt unless the session alone can gate warp submission
    /// (mirrors QgsGeorefShellWindow::applyTransform validation): source and
    /// output paths non-empty, enabled GCP count >= the method minimum, and a
    /// successful fit.
    std::optional<RsGeorefWarpSnapshot> createWarpSnapshot(
      const QString &outputPath,
      QgsImageWarper::ResamplingMethod resampling,
      const QgsCoordinateReferenceSystem &destCrs,
      double pixelSize ) const;

    /// Build a transform from a snapshot (enabled GCPs only). Caller owns it.
    static std::unique_ptr<QgsGeorefTransform> transformFromSnapshot(
      const RsGeorefWarpSnapshot &snap );

    /// Submit warp of \a snap through the warp executor. Returns task id or -1.
    long startWarpTask( const RsGeorefWarpSnapshot &snap );

    bool cancelWarpTask( long taskCenterId );
    long pendingWarpTaskId() const { return mPendingWarpTaskId; }

  signals:
    /// GCP list structure or contents changed (any mutation, incl. setGcps).
    /// Emitted BEFORE the fitChanged() that follows from the mutation's refit.
    void gcpsChanged();
    void fitChanged( const RsGeorefFitResult &fit );
    /// Terminal warp outcome for the pending Task Center task.
    void warpFinished( long taskCenterId, bool success, const QString &errorMessage,
                       const QString &outputPath );

  public slots:
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    void syncWorkflowGcps();

    bool mDirty = false;
    QString mLastPointsPath;
    QString mSourcePath;
    QgsGcpTransformerInterface::TransformMethod mMethod =
      QgsGcpTransformerInterface::TransformMethod::Linear;
    QString mDemPath;
    double mDemZOffset = 0.0;
    QgsCoordinateReferenceSystem mDestCrs;
    QVector<QgsGcpPoint> mGcps;
    RsGeorefFitResult mLastFit;

    CustomWarpExecutor mCustomExecutor;

    long mPendingWarpTaskId = -1;
    RsWarpTask *mPendingWarpTask = nullptr; // deleteLater on terminal
    /// True while the JobEngine worker is inside mPendingWarpTask->run();
    /// the destructor bounded-waits on it instead of hard-deleting (#626).
    std::atomic<bool> mWarpExecutorActive{ false };
    /// Released when the executor leaves the warp job (#650): the destructor
    /// blocks on this instead of a 500x10 ms GUI-thread spin.
    QSemaphore mWarpExecutorDone{ 0 };
    RsGeorefWarpSnapshot mPendingSnap;

    // WorkflowRuntime mirror (ADR 0028)
    sicnu::workflow::WorkflowRuntime mWorkflowRuntime;
    std::string mWorkflowSessionId;
    bool mWorkflowBuiltinsRegistered = false;
};
