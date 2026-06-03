// rs_accuracy_dialog.h — Phase 10A Task 10.9.
//
// Modal-style result dialog for the accuracy assessment computed after
// RsClassificationTask trains its backend. Displays:
//
//   - Header (Overall Accuracy %, Kappa) in 18pt bold
//   - Confusion matrix QTableWidget (diagonal green, off-diagonal red ≥ 10)
//   - Per-class Producer / User / F1 metrics table
//   - [Export CSV] button alongside [Close]
//
// The dialog is shown non-modally by the main window so the QgsTask's
// taskCompleted lambda can return promptly; the dialog deletes itself
// on close.
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>

#include "rs_accuracy_assessment.h"

class RsAccuracyDialog : public QDialog
{
    Q_OBJECT
  public:
    RsAccuracyDialog( const RsAccuracyAssessment::Result &result,
                      const QHash<int, QString> &classNames,
                      QWidget *parent = nullptr );

  private slots:
    void exportCsv();

  private:
    QString classLabel( int id ) const;

    RsAccuracyAssessment::Result mResult;
    QHash<int, QString> mNames;
};
