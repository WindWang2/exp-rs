// rs_accuracy_panel.h — classification workflow Step 5 accuracy embed.
//
// Embeddable QWidget showing overall accuracy / Kappa, confusion matrix,
// per-class Producer / User / F1, and CSV export. Shared by Step 5 of the
// workflow host and optionally by RsAccuracyDialog as a thin wrapper.
#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "rs_accuracy_assessment.h"

class QLabel;
class QPushButton;
class QTableWidget;

class RsAccuracyPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit RsAccuracyPanel( QWidget *parent = nullptr );

    void setResult( const RsAccuracyAssessment::Result &r,
                    const QHash<int, QString> &classNames );
    void clear();
    bool hasResult() const;

    RsAccuracyAssessment::Result result() const { return mResult; }
    QHash<int, QString> classNames() const { return mNames; }

  private slots:
    void exportCsv();

  private:
    QString classLabel( int id ) const;
    void rebuildTables();

    RsAccuracyAssessment::Result mResult;
    QHash<int, QString> mNames;
    bool mHasResult = false;

    QLabel *mHeaderLabel = nullptr;
    QTableWidget *mConfusion = nullptr;
    QTableWidget *mPerClass = nullptr;
    QPushButton *mExportBtn = nullptr;
    QLabel *mEmptyHint = nullptr;
};
