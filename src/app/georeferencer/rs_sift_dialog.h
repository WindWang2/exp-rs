#ifndef RS_SIFT_DIALOG_H
#define RS_SIFT_DIALOG_H

#include <QDialog>

#include "rs_sift_matcher.h"

class QDoubleSpinBox;
class QSpinBox;

/**
 * Parameter dialog for the SIFT auto-match flow (Task 11.5.7).
 *
 * Exposes 5 numeric controls bound to RsSiftMatcher::Params:
 *  - contrastThreshold (SIFT sensitivity)
 *  - maxMatches (cap before RANSAC)
 *  - minInlierRatio (informational quality floor)
 *  - ransacThreshold (px reprojection tolerance)
 *  - maxImageSide (downsample cap for both SRC and REF)
 */
class RsSiftDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit RsSiftDialog( QWidget *parent = nullptr );

    //! Returns the current spinbox values as a Params struct.
    RsSiftMatcher::Params params() const;

  private:
    QDoubleSpinBox *mContrast     = nullptr;
    QSpinBox       *mMaxMatches   = nullptr;
    QDoubleSpinBox *mMinInlier    = nullptr;
    QDoubleSpinBox *mRansacThresh = nullptr;
    QSpinBox       *mMaxImageSide = nullptr;
};

#endif // RS_SIFT_DIALOG_H
