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

    QGroupBox *paramGroup = setupParamGroup(
      mainLayout, tr( "PCA Transform Parameters" ) );
    paramGroup->setToolTip(
      tr( "The number of components cannot exceed the total band count of the input image; the first components usually capture most of the variance." ) );
    auto *form = SicnuUi::makeFormLayout();
    qobject_cast<QVBoxLayout *>( paramGroup->layout() )->addLayout( form );

    m_componentsSpin = new QSpinBox( paramGroup );
    m_componentsSpin->setObjectName( QStringLiteral( "pcaComponentsSpin" ) );
    m_componentsSpin->setRange( 0, 100 );
    // Schema default is the single source of truth (rs:pca: 0 = all bands);
    // the dialog must not invent its own default.
    m_componentsSpin->setValue( 0 );
    SicnuDialogHelp::tip( m_componentsSpin, tr(
      "Number of output components, 0 = all bands; must be ≤ the input band count."
      "The first PCs usually hold most of the variance; used for band decorrelation and dimensionality-reduction compression.")  );
    form->addRow( tr( "Number of Components" ), m_componentsSpin );

    setupOutputRow( mainLayout );
    setupButtonBar( mainLayout );
    mainLayout->addStretch( 1 );
}

void PcaDialog::onRun()
{
    if (!m_rasterLayer || !m_rasterLayer->isValid()) {
        QMessageBox::warning(this, dialogTitle(), tr("Select a valid raster layer first."));
        return;
    }

    const int numComponents = m_componentsSpin->value();
    // 0 = all bands (operator schema default); only a positive count must fit.
    if ( numComponents > 0 && numComponents > m_rasterLayer->bandCount() ) {
        QMessageBox::warning(this, dialogTitle(),
                             tr("The number of components (%1) exceeds the total band count of the input raster (%2).")
                                 .arg(numComponents).arg(m_rasterLayer->bandCount()));
        return;
    }

    Json::Value params(Json::objectValue);
    params["input"] = m_rasterLayer->source().toStdString();
    params["output"] = outputPath().toStdString();
    params["numComponents"] = numComponents;

    runOperatorTask(QStringLiteral("rs:pca"), params);
}
