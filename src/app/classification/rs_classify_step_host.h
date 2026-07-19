// rs_classify_step_host.h — Right-side stacked panels for classification steps.
#pragma once

#include "rs_classify_workflow_controller.h"

#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QStackedWidget;

/**
 * \brief Host for the seven classification step panels.
 *
 * Each panel is a placeholder skeleton (title, completion tip, gate label,
 * prev/next). Real controls are filled in a later task.
 */
class RsClassifyStepHost : public QWidget
{
    Q_OBJECT

  public:
    explicit RsClassifyStepHost( QWidget *parent = nullptr );

    void setCurrentStep( RsClassifyStep s );
    QWidget *panel( RsClassifyStep s ) const;

    /// Soft-gate status label for the given step (objectName classifyStepGate).
    QLabel *gateLabel( RsClassifyStep s ) const;

    QPushButton *prevButton( RsClassifyStep s ) const;
    QPushButton *nextButton( RsClassifyStep s ) const;

  signals:
    void prevClicked();
    void nextClicked();

  private:
    QWidget *buildPanel( RsClassifyStep s );

    QStackedWidget *mStack = nullptr;
    QVector<QWidget *> mPanels; // size Count
};
