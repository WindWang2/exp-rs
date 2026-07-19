// rs_classify_stepper_bar.h — Classification workflow top stepper + expert mode.
#pragma once

#include "rs_classify_workflow_controller.h"

#include <QVector>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QToolButton;

/**
 * \brief Horizontal 7-step indicator for the classification workflow.
 *
 * Checkable exclusive QToolButtons labeled 1–7 (体系…输出) plus an
 * expert-mode checkbox. Soft navigation only — completion is visual.
 */
class RsClassifyStepperBar : public QWidget
{
    Q_OBJECT

  public:
    explicit RsClassifyStepperBar( QWidget *parent = nullptr );

    void setCurrentStep( RsClassifyStep s );
    void setStepComplete( RsClassifyStep s, bool complete );
    void setMode( RsClassifyUiMode m );

  signals:
    void stepClicked( RsClassifyStep s );
    void modeToggled( RsClassifyUiMode m );

  private:
    void rebuildStyle( int index );

    QButtonGroup *mGroup = nullptr;
    QVector<QToolButton *> mButtons; // size = Count
    QCheckBox *mExpertCheck = nullptr;
    QVector<bool> mComplete; // size = Count
};
