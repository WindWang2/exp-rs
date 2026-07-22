// rs_post_process_dialog.h — 单算法后处理对话框（一算法一对话框）
#pragma once

#include "rs_post_process_task.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QLabel;

/**
 * One dialog per post-process algorithm (Sieve / Majority / Clump / Recode /
 * Polygonize). Default: load result into the classification window layer tree.
 */
class RsPostProcessDialog : public QDialog
{
    Q_OBJECT
  public:
    enum class Algorithm
    {
      Sieve,
      Majority,
      Clump,
      Recode,
      Polygonize
    };

    explicit RsPostProcessDialog( Algorithm algo, QWidget *parent = nullptr );

    void setDefaultInputPath( const QString &path );
    void setDefaultOutputPath( const QString &path );

    /// Whether result should be added to the classification layer tree (default true).
    bool loadToLayerTree() const;

    /// Build a single-operator RsPostProcessConfig for this algorithm.
    bool buildConfig( RsPostProcessConfig &cfg, QString *errorMessage = nullptr ) const;

    static QString algorithmTitle( Algorithm a );
    static QString algorithmId( Algorithm a );

  private:
    void setupUi();
    QMap<int, int> collectRecodeMap() const;
    QString defaultOutputSuffix() const;

    Algorithm m_algo = Algorithm::Sieve;
    QLineEdit *m_inputEdit = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QSpinBox *m_sieveSpin = nullptr;
    QSpinBox *m_majoritySpin = nullptr;
    QSpinBox *m_connectSpin = nullptr; // 4 or 8 for sieve/clump
    QTableWidget *m_recodeTable = nullptr;
    QCheckBox *m_loadToLayersCb = nullptr;
    QLabel *m_hintLabel = nullptr;
};
