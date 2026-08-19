#ifndef RS_WARP_TASK_H
#define RS_WARP_TASK_H

#include <memory>

#include <QString>

#include "qgscoordinatereferencesystem.h"
#include "qgsfeedback.h"
#include "qgsimagewarper.h"
#include "qgstaskmanager.h"

class QgsGeorefTransform;

/**
 * QgsTask subclass that runs QgsImageWarper on a worker thread.
 *
 * Owns an internal QgsFeedback that bridges GDAL progress to QgsTask::setProgress
 * and lets cancel() interrupt mid-warp. The warp result is exposed via result()
 * after the task completes (taskCompleted or taskTerminated signal).
 */
class RsWarpTask : public QgsTask
{
    Q_OBJECT
  public:
    RsWarpTask( const QString &in,
                const QString &out,
                const QgsGeorefTransform *transform,
                QgsImageWarper::ResamplingMethod r,
                const QgsCoordinateReferenceSystem &destCrs,
                double pixelSize,
                int backgroundValue = 0 );
    ~RsWarpTask() override;

    int backgroundValue() const { return mBackground; }

    bool run() override;
    void cancel() override;

    const QgsImageWarper::WarpResult &result() const { return mResult; }

  private:
    QString mIn;
    QString mOut;
    std::unique_ptr<QgsGeorefTransform> mTransform;
    QgsImageWarper::ResamplingMethod mResamp;
    QgsCoordinateReferenceSystem mDestCrs;
    double mPixelSize = 0.0;
    int mBackground = 0;
    QgsFeedback mFb;
    QgsImageWarper::WarpResult mResult;
};

#endif // RS_WARP_TASK_H
