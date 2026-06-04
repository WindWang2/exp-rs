// rs_classifier_load_dialog.h — Phase 10A.1.3.
//
// Modal dialog that lets the user pick a previously saved classifier model
// (NormalBayes or SVM YAML) from disk. KMeans is intentionally excluded as
// cv::kmeans is not a cv::Algorithm and cannot be serialised.
//
// On accept(), selectedKind() + modelPath() expose the user's choices; the
// caller is responsible for instantiating the matching RsClassifierBackend
// subclass and invoking ->load(path).
#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QRadioButton;

class RsClassifierLoadDialog : public QDialog
{
    Q_OBJECT

  public:
    enum class BackendKind
    {
      NormalBayes,
      SvmRbf,
    };

    explicit RsClassifierLoadDialog( QWidget *parent = nullptr );

    BackendKind selectedKind() const { return mKind; }
    QString modelPath() const;

  private slots:
    void browseForFile();
    void onAccept();

  private:
    QRadioButton *mRbBayes = nullptr;
    QRadioButton *mRbSvm = nullptr;
    QLineEdit *mPathEdit = nullptr;
    BackendKind mKind = BackendKind::NormalBayes;
};
