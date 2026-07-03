// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "processing/algorithms/band_math.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

void BandMathDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    // Expression input
    auto *exprLayout = new QHBoxLayout();
    exprLayout->addWidget(new QLabel(tr("Expression:"), this));
    m_expressionEdit = new QLineEdit(this);
    m_expressionEdit->setPlaceholderText(tr("e.g., (b1 - b2) / (b1 + b2)"));
    exprLayout->addWidget(m_expressionEdit);
    mainLayout->addLayout(exprLayout);

    // Output file (from base class)
    setupOutputRow(mainLayout);

    // Buttons (from base class)
    setupButtonBar(mainLayout);
}

BandMathDialog::BandMathDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setMinimumWidth(450);
    setupUi();
}

void BandMathDialog::onRun()
{
    const QString expression = m_expressionEdit->text().trimmed();
    if (expression.isEmpty()) {
        QMessageBox::warning(this, tr("Band Math"), tr("Please enter an expression."));
        return;
    }

    const QString sourcePath = m_rasterLayer->source();
    const QString outPath = outputPath();

    runGdalTask([sourcePath, outPath, expression]() -> QString {
        QString error;
        if (!BandMath::processFile(sourcePath, outPath, expression, &error))
            return QString();
        return outPath;
    });
}