// src/app/dialogs/pca_dialog.h
#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QgsRasterLayer;

/**
 * Dialog for Principal Component Analysis.
 * Reduces multi-band raster data to a specified number of components
 * using the ImageEnhancement::pca algorithm.
 */
class PcaDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PcaDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();

private:
    void setupUi();

    QSpinBox *m_componentsSpin = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
