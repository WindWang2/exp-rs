// src/app/dialogs/spatial_filter_dialog.h
#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;
class QgsRasterLayer;

/**
 * Dialog for Spatial Filtering operations.
 * Supports Mean, Gaussian, Median, Sobel, and Laplacian filters
 * using the ImageEnhancement algorithm library.
 */
class SpatialFilterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SpatialFilterDialog(QWidget *parent = nullptr);

    void setRasterLayer(QgsRasterLayer *layer);

private slots:
    void onBrowseOutput();
    void onRun();

private:
    void setupUi();

    QComboBox *m_filterTypeCombo = nullptr;
    QComboBox *m_kernelSizeCombo = nullptr;
    QLineEdit *m_outputEdit = nullptr;
    QPushButton *m_runButton = nullptr;
    QgsRasterLayer *m_rasterLayer = nullptr;
};
