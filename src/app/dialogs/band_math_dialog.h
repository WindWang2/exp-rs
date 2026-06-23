// src/app/dialogs/band_math_dialog.h
#pragma once

#include "raster_processing_dialog_base.h"

class QLineEdit;

/**
 * Dialog for Band Math operations.
 * Allows users to enter mathematical expressions and apply them
 * to multi-band raster layers using the BandMath algorithm.
 */
class BandMathDialog : public RasterProcessingDialogBase
{
    Q_OBJECT

public:
    explicit BandMathDialog(QWidget *parent = nullptr);

protected:
    QString toolName() const override { return QStringLiteral("band_math"); }
    QString dialogTitle() const override { return tr("Band Math"); }
    void onRun() override;

private:
    void setupUi();

    QLineEdit *m_expressionEdit = nullptr;
};
