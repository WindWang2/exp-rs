// src/app/dialogs/atmospheric_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;

/**
 * Dialog for Atmospheric Correction operations.
 * Supports DN-to-Radiance conversion, DOS1, and DOS2 methods
 * using the AtmosphericCorrection algorithm library.
 */
class AtmosphericDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit AtmosphericDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("atmospheric_correction"); }
    QString dialogTitle() const override { return tr("Atmospheric Correction"); }
    void onRun() override;

private slots:
    void onMethodChanged(int index);
    void onCompleted(const QString &outputPath);
    void onFailed(const QString &errorMessage);

private:
    void setupUi();
    void populateBandCombo();

    QComboBox *m_methodCombo = nullptr;
    QComboBox *m_bandCombo = nullptr;
    QDoubleSpinBox *m_gainSpin = nullptr;
    QDoubleSpinBox *m_biasSpin = nullptr;
    QDoubleSpinBox *m_airmassSpin = nullptr;

    QLabel *m_airmassLabel = nullptr;
};
