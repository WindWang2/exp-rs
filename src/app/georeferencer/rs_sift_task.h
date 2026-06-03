#ifndef RS_SIFT_TASK_H
#define RS_SIFT_TASK_H

#include <QString>

#include "qgscoordinatereferencesystem.h"
#include "qgsfeedback.h"
#include "qgstaskmanager.h"

#include "rs_sift_matcher.h"

/**
 * QgsTask wrapper around RsSiftMatcher (Task 11.5.7).
 *
 * Runs SIFT detection + matching on a worker thread; result() is valid after
 * the task emits taskCompleted or taskTerminated. cancel() cooperatively
 * interrupts the matcher via the internal QgsFeedback.
 */
class RsSiftTask : public QgsTask
{
    Q_OBJECT
  public:
    RsSiftTask( QString srcRaster,
                QString refRaster,
                QgsCoordinateReferenceSystem refCrs,
                RsSiftMatcher::Params params );

    bool run() override;
    void cancel() override;

    const RsSiftMatcher::Result &result() const { return mResult; }

  private:
    QString mSrc;
    QString mRef;
    QgsCoordinateReferenceSystem mRefCrs;
    RsSiftMatcher::Params mParams;
    QgsFeedback mFb;
    RsSiftMatcher::Result mResult;
};

#endif // RS_SIFT_TASK_H
