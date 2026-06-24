// src/app/dialogs/contrast_stretch_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class AsyncGdalRunner;

/**
 * Dialog for Contrast Stretch operations.
 * Supports Linear, Percentage Clip, Std Dev, and Histogram Equalization
 * using the ImageEnhancement algorithm library.
 */
class ContrastStretchDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit ContrastStretchDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("contrast_stretch"); }
    QString dialogTitle() const override { return tr("Contrast Stretch"); }
    void onRun() override;

private slots:
    void onMethodChanged(int index);

private:
    void setupUi();

    QComboBox *m_methodCombo = nullptr;
    QDoubleSpinBox *m_clipSpin = nullptr;
    QDoubleSpinBox *m_stddevSpin = nullptr;
    QLabel *m_clipLabel = nullptr;
    QLabel *m_stddevLabel = nullptr;
    AsyncGdalRunner *m_runner = nullptr;
};
