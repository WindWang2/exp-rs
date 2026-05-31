// src/app/dialogs/band_math_dialog.h
#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;
class QPushButton;
class QgsRasterLayer;

/**
 * Dialog for Band Math operations.
 * Allows users to enter mathematical expressions and apply them
 * to multi-band raster layers using the BandMath algorithm.
 */
class BandMathDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BandMathDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();

private:
    void setupUi();

    QLineEdit *m_expressionEdit = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
