// resolution_widget.cpp — shared raster resolution & grid selection widget
#include "resolution_widget.h"
#include "raster_layer_combo.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>

ResolutionWidget::ResolutionWidget(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void ResolutionWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto *modeLayout = new QHBoxLayout();
    auto *modeLabel = new QLabel(tr("Resolution Mode:"), this);
    mModeCombo = new QComboBox(this);
    mModeCombo->addItem(tr("Fixed Target Resolution"), static_cast<int>(Mode::FixedResolution));
    mModeCombo->addItem(tr("Scale Factor Multiplier"), static_cast<int>(Mode::ScaleFactor));
    mModeCombo->addItem(tr("Match Reference Layer Grid"), static_cast<int>(Mode::ReferenceLayer));
    modeLayout->addWidget(modeLabel);
    modeLayout->addWidget(mModeCombo, 1);

    mainLayout->addLayout(modeLayout);

    mStack = new QStackedWidget(this);

    // Page 0: Fixed Target Resolution
    auto *fixedWidget = new QWidget(this);
    auto *fixedLayout = new QFormLayout(fixedWidget);
    fixedLayout->setContentsMargins(0, 4, 0, 0);

    mResXSpin = new QDoubleSpinBox(fixedWidget);
    mResXSpin->setRange(0.000001, 100000.0);
    mResXSpin->setDecimals(6);
    mResXSpin->setValue(10.0);

    mResYSpin = new QDoubleSpinBox(fixedWidget);
    mResYSpin->setRange(0.000001, 100000.0);
    mResYSpin->setDecimals(6);
    mResYSpin->setValue(10.0);

    auto *resSpinLayout = new QHBoxLayout();
    resSpinLayout->addWidget(new QLabel(tr("X:"), fixedWidget));
    resSpinLayout->addWidget(mResXSpin);
    resSpinLayout->addWidget(new QLabel(tr("Y:"), fixedWidget));
    resSpinLayout->addWidget(mResYSpin);

    fixedLayout->addRow(tr("Pixel Size (Map Units):"), resSpinLayout);
    mStack->addWidget(fixedWidget);

    // Page 1: Scale Factor Multiplier
    auto *scaleWidget = new QWidget(this);
    auto *scaleLayout = new QFormLayout(scaleWidget);
    scaleLayout->setContentsMargins(0, 4, 0, 0);

    mScaleSpin = new QDoubleSpinBox(scaleWidget);
    mScaleSpin->setRange(0.0001, 1000.0);
    mScaleSpin->setDecimals(4);
    mScaleSpin->setSingleStep(0.1);
    mScaleSpin->setValue(1.0);

    scaleLayout->addRow(tr("Scale Multiplier (e.g. 0.5x, 2.0x):"), mScaleSpin);
    mStack->addWidget(scaleWidget);

    // Page 2: Match Reference Layer
    auto *refWidget = new QWidget(this);
    auto *refLayout = new QFormLayout(refWidget);
    refLayout->setContentsMargins(0, 4, 0, 0);

    mRefLayerCombo = new RasterLayerCombo(refWidget);
    refLayout->addRow(tr("Reference Layer:"), mRefLayerCombo);
    mStack->addWidget(refWidget);

    mainLayout->addWidget(mStack);

    connect(mModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResolutionWidget::onModeChanged);

    connect(mResXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ResolutionWidget::resolutionChanged);
    connect(mResYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ResolutionWidget::resolutionChanged);
    connect(mScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ResolutionWidget::resolutionChanged);
    connect(mRefLayerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ResolutionWidget::resolutionChanged);
}

ResolutionWidget::Mode ResolutionWidget::mode() const
{
    return static_cast<Mode>(mModeCombo->currentData().toInt());
}

void ResolutionWidget::setMode(Mode mode)
{
    int index = mModeCombo->findData(static_cast<int>(mode));
    if (index >= 0) {
        mModeCombo->setCurrentIndex(index);
    }
}

void ResolutionWidget::onModeChanged(int index)
{
    if (index >= 0 && index < mStack->count()) {
        mStack->setCurrentIndex(index);
        emit resolutionChanged();
    }
}

double ResolutionWidget::targetResolutionX() const
{
    return mResXSpin->value();
}

void ResolutionWidget::setTargetResolutionX(double resX)
{
    if (resX > 0.0) {
        mResXSpin->setValue(resX);
    }
}

double ResolutionWidget::targetResolutionY() const
{
    return mResYSpin->value();
}

void ResolutionWidget::setTargetResolutionY(double resY)
{
    if (resY > 0.0) {
        mResYSpin->setValue(resY);
    }
}

void ResolutionWidget::setTargetResolution(double res)
{
    setTargetResolutionX(res);
    setTargetResolutionY(res);
}

double ResolutionWidget::scaleFactor() const
{
    return mScaleSpin->value();
}

void ResolutionWidget::setScaleFactor(double scale)
{
    if (scale > 0.0) {
        mScaleSpin->setValue(scale);
    }
}

QgsRasterLayer *ResolutionWidget::referenceLayer() const
{
    if (mode() == Mode::ReferenceLayer) {
        return mRefLayerCombo->currentRasterLayer();
    }
    return nullptr;
}

void ResolutionWidget::populateReferenceLayers()
{
    mRefLayerCombo->populate();
}

bool ResolutionWidget::isValid(QString *errorMsg) const
{
    switch (mode()) {
    case Mode::FixedResolution:
        if (targetResolutionX() <= 0.0 || targetResolutionY() <= 0.0) {
            if (errorMsg) {
                *errorMsg = tr("Physical target resolution must be strictly greater than zero.");
            }
            return false;
        }
        break;
    case Mode::ScaleFactor:
        if (scaleFactor() <= 0.0) {
            if (errorMsg) {
                *errorMsg = tr("Scale factor multiplier must be strictly greater than zero.");
            }
            return false;
        }
        break;
    case Mode::ReferenceLayer:
        if (!referenceLayer()) {
            if (errorMsg) {
                *errorMsg = tr("A valid reference raster layer must be selected.");
            }
            return false;
        }
        break;
    }

    return true;
}
