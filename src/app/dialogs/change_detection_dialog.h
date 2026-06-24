// change_detection_dialog.h — Change Detection Dialog
#pragma once

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QgsRasterLayer;
class AsyncGdalRunner;

class ChangeDetectionDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChangeDetectionDialog(QWidget *parent = nullptr);

    void populateLayers();
    QString outputPath() const;

private slots:
    void runDetection();
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &errorMessage);
    void browseOutput();
    void updateBandSelectors();
    void onMethodChanged(int index);

private:
    void setupUi();

    QComboBox *m_beforeLayerCombo = nullptr;
    QComboBox *m_afterLayerCombo = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_beforeBandCombo = nullptr;
    QComboBox *m_afterBandCombo = nullptr;
    QDoubleSpinBox *m_thresholdSpin = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_runButton = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
};
