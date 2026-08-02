// rs_georeferencing_session.h — Shared Georeferencing Session (#32)
//
// Owns GCP pairing, transform fit readiness, per-point residual summary, and
// immutable warp snapshots. Warp execution is submitted through an injected
// executor seam (ADR 0020 decision 3); production delegates to Task Center.
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPointF>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcptransformer.h"
#include "qgsimagewarper.h"
#include "qgspointxy.h"

#include "processing/framework/task_center.h"
#include "workflow/builtin_definitions.h"
#include "workflow/workflow_runtime.h"

class QWidget;
class QgsGeorefTransform;
class RsWarpTask;

#include <functional>

struct CustomWarpExecutor
{
  std::function<long( const sicnu::jobs::JobRequest &request,
                      const sicnu::TaskCenter::JobExecutor &executor,
                      const sicnu::TaskCenter::CancelHook &onCancel )> submit;
  std::function<bool( long taskId )> cancel;
};

/**
 * One Ground Control Point pairing in the session.
 * Source is in source-image coordinates (same as QgsGcpPoint source);
 * destination is map / reference coordinates.
 */
struct RsGeorefGcpPair
{
  QgsPointXY source;
  QgsPointXY destination;
  bool enabled = true;
  /// User-defined point type label (SICNU `.points` v2 / table 类型 column).
  QString pointType;
};

/**
 * Sentinel stored in RsGeorefFitResult::residuals for entries with no valid
 * residual (disabled GCPs, failed back-transform, or unfit points).
 */
inline QPointF rsGeorefInvalidResidual()
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  return QPointF( nan, nan );
}

inline bool rsGeorefResidualIsValid( const QPointF &r )
{
  return !std::isnan( r.x() ) && !std::isnan( r.y() );
}

/**
 * Outcome of the last fit (parameter estimation from enabled GCPs).
 *
 * Residual semantics (ADR 0020 decision 2): residuals live in source-image
 * PIXELS. Per enabled GCP, the destination point is back-transformed into
 * source pixel space (QgsGeorefTransform::transformWorldToRaster) and the
 * residual is the Euclidean delta against the observed source pixel
 * (QgsGeorefTransform::toSourcePixel). rms is the root-mean-square of the
 * per-point residual magnitudes over enabled GCPs.
 */
struct RsGeorefFitResult
{
  bool ready = false;
  double rms = -1.0; ///< source-pixel RMS over enabled GCPs
  int enabledGcpCount = 0;
  int minimumGcpCount = 0;
  QString errorMessage;
  /// Per-point residual (dx, dy) in source pixels, aligned with gcps()
  /// ordering. Always sized to gcps().size(); disabled GCPs and points whose
  /// back-transform failed carry rsGeorefInvalidResidual().
  QVector<QPointF> residuals;
  /// RPC refinement diagnostic: source-pixel RMS of the unrefined RPC fit
  /// (before GCP-bias refinement). -1 when not applicable (non-RPC method,
  /// fewer than 3 enabled GCPs, or unrefined fit failed).
  double refinementRmsBefore = -1.0;
};

/**
 * Immutable warp request frozen at enqueue time. Mutating the live session
 * after createWarpSnapshot() must not change fields of an existing snapshot.
 */
struct RsGeorefWarpSnapshot
{
  QString sourcePath;
  QString outputPath;
  QVector<RsGeorefGcpPair> gcps; // includes disabled; warp uses enabled only
  QgsGcpTransformerInterface::TransformMethod method =
    QgsGcpTransformerInterface::TransformMethod::Linear;
  QgsImageWarper::ResamplingMethod resampling =
    QgsImageWarper::ResamplingMethod::NearestNeighbour;
  QgsCoordinateReferenceSystem destCrs;
  double pixelSize = 0.0;
  double rmsAtCapture = -1.0;
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

    void setGcps( const QVector<RsGeorefGcpPair> &gcps );
    const QVector<RsGeorefGcpPair> &gcps() const { return mGcps; }
    void clearGcps();

    /**
     * Granular GCP mutations (ADR 0020 S2): the session is the sole owner of
     * the GCP list. Each mutation emits gcpsChanged() and re-runs refit()
     * (which emits fitChanged). setGcps() stays wholesale and does NOT refit
     * (callers refit explicitly), preserving the stale-fit snapshot gating.
     */
    void addGcp( const RsGeorefGcpPair &gcp );
    void appendGcps( const QVector<RsGeorefGcpPair> &gcps );
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
    /// Build a transform for the current method, loading the source raster and
    /// (for RpcPhysical) injecting source path + DEM/Z-offset/refinement into
    /// QgsRpcGcpTransformer — mirrors the shell's recomputeFit configuration.
    std::unique_ptr<QgsGeorefTransform> makeConfiguredTransform( bool rpcRefinement ) const;
    void syncWorkflowGcps();

    bool mDirty = false;
    QString mLastPointsPath;
    QString mSourcePath;
    QgsGcpTransformerInterface::TransformMethod mMethod =
      QgsGcpTransformerInterface::TransformMethod::Linear;
    QString mDemPath;
    double mDemZOffset = 0.0;
    QVector<RsGeorefGcpPair> mGcps;
    RsGeorefFitResult mLastFit;

    CustomWarpExecutor mCustomExecutor;

    long mPendingWarpTaskId = -1;
    RsWarpTask *mPendingWarpTask = nullptr; // deleteLater on terminal
    RsGeorefWarpSnapshot mPendingSnap;

    // WorkflowRuntime mirror (ADR 0028)
    sicnu::workflow::WorkflowRuntime mWorkflowRuntime;
    std::string mWorkflowSessionId;
    bool mWorkflowBuiltinsRegistered = false;
};
