// rs_georeferencing_session.h — Shared Georeferencing Session (#32)
//
// Owns GCP pairing, transform fit readiness, residual summary, and immutable
// warp snapshots. Warp execution is submitted through Task Center.
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPointF>

#include <memory>
#include <optional>

#include "qgscoordinatereferencesystem.h"
#include "qgsgcptransformer.h"
#include "qgsimagewarper.h"
#include "qgspointxy.h"

#include "processing/framework/task_center.h"

class QgsGeorefTransform;
class RsWarpTask;

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
};

/**
 * Outcome of the last fit (parameter estimation from enabled GCPs).
 */
struct RsGeorefFitResult
{
  bool ready = false;
  double rms = -1.0;
  int enabledGcpCount = 0;
  int minimumGcpCount = 0;
  QString errorMessage;
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
 * Deep Georeferencing Session: GCP + fit + snapshot + Task Center warp.
 * Independent per window instance (one session object per shell).
 */
class RsGeoreferencingSession : public QObject
{
  Q_OBJECT
  public:
    explicit RsGeoreferencingSession( QObject *parent = nullptr );
    ~RsGeoreferencingSession() override;

    void setSourceRasterPath( const QString &path );
    QString sourceRasterPath() const { return mSourcePath; }

    void setTransformMethod( QgsGcpTransformerInterface::TransformMethod method );
    QgsGcpTransformerInterface::TransformMethod transformMethod() const { return mMethod; }

    void setGcps( const QVector<RsGeorefGcpPair> &gcps );
    const QVector<RsGeorefGcpPair> &gcps() const { return mGcps; }
    void clearGcps();

    /// Re-estimate transform parameters from enabled GCPs.
    RsGeorefFitResult refit();
    const RsGeorefFitResult &lastFit() const { return mLastFit; }
    bool isFitReady() const { return mLastFit.ready; }

    /// Frozen snapshot for a warp job. Does not start execution.
    /// Returns nullopt if source/output empty or fit is not ready.
    std::optional<RsGeorefWarpSnapshot> createWarpSnapshot(
      const QString &outputPath,
      QgsImageWarper::ResamplingMethod resampling,
      const QgsCoordinateReferenceSystem &destCrs,
      double pixelSize ) const;

    /// Build a transform from a snapshot (enabled GCPs only). Caller owns it.
    static std::unique_ptr<QgsGeorefTransform> transformFromSnapshot(
      const RsGeorefWarpSnapshot &snap );

    /// Submit warp of \a snap through Task Center. Returns Task Center id or -1.
    long startWarpTask( const RsGeorefWarpSnapshot &snap );

    bool cancelWarpTask( long taskCenterId );
    long pendingWarpTaskId() const { return mPendingWarpTaskId; }

  signals:
    void fitChanged( const RsGeorefFitResult &fit );
    /// Terminal warp outcome for the pending Task Center task.
    void warpFinished( long taskCenterId, bool success, const QString &errorMessage,
                       const QString &outputPath );

  private slots:
    void onTaskUpdated( const sicnu::AlgorithmTaskInfo &info );

  private:
    QString mSourcePath;
    QgsGcpTransformerInterface::TransformMethod mMethod =
      QgsGcpTransformerInterface::TransformMethod::Linear;
    QVector<RsGeorefGcpPair> mGcps;
    RsGeorefFitResult mLastFit;

    long mPendingWarpTaskId = -1;
    RsWarpTask *mPendingWarpTask = nullptr; // deleteLater on terminal
    RsGeorefWarpSnapshot mPendingSnap;
};
