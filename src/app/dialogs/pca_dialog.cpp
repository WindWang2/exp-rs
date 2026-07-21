// src/app/dialogs/pca_dialog.cpp
#include "pca_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"

#include <raster/qgsrasterlayer.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSpinBox>
#include <QMessageBox>

PcaDialog::PcaDialog(QWidget *parent)
    : RasterProcessingDialogBase(parent)
{
    setWindowTitle(dialogTitle());
    setupUi();
}

void PcaDialog::setupUi()
{
    auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

    setupHelpBanner( mainLayout );

    QFrame *sec = SicnuUi::makeSection(
      this, tr( "参数" ),
      tr( "主成分个数不超过输入波段数；前几个 PC 通常含大部分方差。" ) );
    auto *form = new QFormLayout();
    form->setContentsMargins( 0, 0, 0, 0 );
    m_componentsSpin = new QSpinBox( sec );
    m_componentsSpin->setRange( 1, 10 );
    m_componentsSpin->setValue( 3 );
    SicnuDialogHelp::tip( m_componentsSpin, tr(
      "输出主成分个数，必须 ≤ 输入波段数。"
      "前几个 PC 通常含大部分方差，用于去相关与降维。" ) );
    form->addRow( tr( "主成分数" ), m_componentsSpin );
    sec->layout()->addItem( form );
    mainLayout->addWidget( sec );

    setupOutputRow( mainLayout );
    setupButtonBar( mainLayout );
    mainLayout->addStretch( 1 );
}

void PcaDialog::onRun()
{
    if (!m_rasterLayer) {
        QMessageBox::warning(this, tr("PCA"), tr("No raster layer selected."));
        return;
    }

    const int numComponents = m_componentsSpin->value();
    if (numComponents > m_rasterLayer->bandCount()) {
        QMessageBox::warning(this, tr("PCA"),
                             tr("Number of components (%1) exceeds band count (%2).")
                                 .arg(numComponents).arg(m_rasterLayer->bandCount()));
        return;
    }

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["numComponents"] = numComponents;

    runOperatorTask(QStringLiteral("rs:pca"), params);
}
