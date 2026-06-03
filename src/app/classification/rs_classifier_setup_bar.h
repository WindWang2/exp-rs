// rs_classifier_setup_bar.h — Phase 10A Task 10.8.
//
// Bottom-toolbar widget exposing the supervised/unsupervised classifier
// choice + band picker + train ratio + Apply / Quick-preview / Cross-validate
// buttons. Mirrors the ClassifierBar component in design.html.
//
// v1: bands picked via a comma-separated QLineEdit (1-based GDAL indices).
//     RF / Mahalanobis / UNet algorithm buttons are present but disabled to
//     telegraph that they ship in a later phase.
#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QToolButton;

enum class RsClassifierKind
{
  NormalBayes = 0,
  SvmRbf,
  KMeans,
};

class RsClassifierSetupBar : public QWidget
{
    Q_OBJECT
  public:
    explicit RsClassifierSetupBar( QWidget *parent = nullptr );

    RsClassifierKind currentKind() const { return mKind; }
    QVector<int> selectedBands() const;
    double trainRatio() const;
    QString outputPath() const;

    void setSourceBands( int count );
    int sourceBands() const { return mSourceBands; }

    void setOutputPath( const QString &path );

  signals:
    void applyRequested();
    void previewRequested();
    void crossValidateRequested();

  private:
    void buildLayout();

    RsClassifierKind mKind = RsClassifierKind::NormalBayes;
    int mSourceBands = 0;

    QToolButton *mBtnNormalBayes = nullptr;
    QToolButton *mBtnSvm = nullptr;
    QToolButton *mBtnKMeans = nullptr;
    QToolButton *mBtnRfDisabled = nullptr;
    QToolButton *mBtnMahaDisabled = nullptr;
    QToolButton *mBtnUnetDisabled = nullptr;

    QLineEdit *mBandsEdit = nullptr;
    QDoubleSpinBox *mTrainRatioSpin = nullptr;
    QLineEdit *mOutputEdit = nullptr;
    QPushButton *mBtnApply = nullptr;
    QPushButton *mBtnPreview = nullptr;
    QPushButton *mBtnCv = nullptr;
};
