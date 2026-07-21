// src/app/dialogs/band_math_dialog.cpp
#include "band_math_dialog.h"
#include "dialog_help_catalog.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>

void BandMathDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);

    setupHelpBanner(mainLayout);
// Expression input
    auto *exprLayout = new QHBoxLayout();
    exprLayout->addWidget(new QLabel(tr("Expression:"), this));
    m_expressionEdit = new QLineEdit(this);
    m_expressionEdit->setPlaceholderText(tr("e.g., (b1 - b2) / (b1 + b2)"));
    SicnuDialogHelp::tip( m_expressionEdit, tr(
      "波段运算表达式。波段写作 b1,b2…（从 1 起）。\n"
      "示例：(b1-b2)/(b1+b2)；b1*0.0001\n"
      "支持 + − * / 与括号。" ) );
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

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["expression"] = expression.toStdString();

    runOperatorTask(QStringLiteral("rs:band_math"), params);
}
