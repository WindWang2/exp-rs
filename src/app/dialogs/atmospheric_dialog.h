// src/app/dialogs/atmospheric_dialog.h
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QPushButton;
class QgsRasterLayer;

/**
 * Dialog for Atmospheric Correction operations.
 * Supports DN-to-Radiance conversion, DOS1, and DOS2 methods
 * using the AtmosphericCorrection algorithm library.
 */
class AtmosphericDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AtmosphericDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();
    void onMethodChanged(int index);

private:
    void setupUi();

    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QDoubleSpinBox *m_gainSpin = nullptr;
    QDoubleSpinBox *m_biasSpin = nullptr;
    QDoubleSpinBox *m_airmassSpin = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
