// src/app/dialogs/pca_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QSpinBox;

/**
 * Dialog for Principal Component Analysis.
 * Reduces multi-band raster data to a specified number of components
 * using the ImageEnhancement::pca algorithm.
 */
class PcaDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit PcaDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("pca"); }
    QString dialogTitle() const override { return tr("主成分分析 (PCA)"); }
    void onRun() override;

private slots:

private:
    void setupUi();

    QSpinBox *m_componentsSpin = nullptr;
};
