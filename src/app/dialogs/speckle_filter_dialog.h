// src/app/dialogs/speckle_filter_dialog.h
#pragma once

#include <QDialog>
#include <QLineEdit>

class QComboBox;
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QgsRasterLayer;

/**
 * Dialog for SAR Speckle Filtering operations.
 * Supports Lee, Frost, Kuan, and Gamma-MAP filters
 * for reducing speckle noise in synthetic aperture radar imagery.
 */
class SpeckleFilterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpeckleFilterDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

    QString outputPath() const { return m_outputEdit ? m_outputEdit->text().trimmed() : QString(); }

private slots:
    void onBrowseOutput();
    void onRun();
    void onFilterTypeChanged(int index);

private:
    void setupUi();

    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
    QDoubleSpinBox *m_noiseVarSpin = nullptr;
    QDoubleSpinBox *m_dampingSpin = nullptr;
    QLabel *m_dampingLabel = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
