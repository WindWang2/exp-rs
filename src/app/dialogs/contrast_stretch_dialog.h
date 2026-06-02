// src/app/dialogs/contrast_stretch_dialog.h
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QDoubleSpinBox;
class QgsRasterLayer;

/**
 * Dialog for Contrast Stretch operations.
 * Supports Linear, Percentage Clip, Std Dev, and Histogram Equalization
 * using the ImageEnhancement algorithm library.
 */
class ContrastStretchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ContrastStretchDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();
    void onMethodChanged(int index);

private:
    void setupUi();

    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_clipSpin = nullptr;
    QDoubleSpinBox *m_stddevSpin = nullptr;
    QLabel *m_clipLabel = nullptr;
    QLabel *m_stddevLabel = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
