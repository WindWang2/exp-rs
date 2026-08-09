// resolution_widget.h — shared raster resolution & grid selection widget
#pragma once

#include <QWidget>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QStackedWidget;
class RasterLayerCombo;
class QgsRasterLayer;

/**
 * Shared resolution selection widget supporting 3 modes:
 *  1. Fixed Resolution (specified pixel size in map units, >0)
 *  2. Scale Factor (scale multiplier relative to input raster, >0)
 *  3. Reference Layer (derives resolution and grid alignment from another loaded raster)
 *
 * Includes preflight parameter validation prohibiting <= 0.0 physical resolutions and 0.0 scale factors.
 */
class ResolutionWidget : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        FixedResolution = 0,
        ScaleFactor = 1,
        ReferenceLayer = 2
    };
    Q_ENUM(Mode)

    explicit ResolutionWidget(QWidget *parent = nullptr);
    ~ResolutionWidget() override = default;

    /// Current resolution selection mode
    Mode mode() const;
    void setMode(Mode mode);

    /// Target physical pixel resolution in X (map units)
    double targetResolutionX() const;
    void setTargetResolutionX(double resX);

    /// Target physical pixel resolution in Y (map units)
    double targetResolutionY() const;
    void setTargetResolutionY(double resY);

    /// Set symmetric target physical pixel resolution (resX = resY)
    void setTargetResolution(double res);

    /// Target scale factor multiplier (> 0.0)
    double scaleFactor() const;
    void setScaleFactor(double scale);

    /// Selected reference raster layer (nullptr if none selected or not in ReferenceLayer mode)
    QgsRasterLayer *referenceLayer() const;

    /// Populate reference layer dropdown from current QgsProject
    void populateReferenceLayers();

    /// Validate current resolution settings. Returns true if valid.
    /// If invalid, populates errorMsg with explanation.
    bool isValid(QString *errorMsg = nullptr) const;

signals:
    void resolutionChanged();

private slots:
    void onModeChanged(int index);

private:
    void setupUi();

    QComboBox *mModeCombo = nullptr;
    QStackedWidget *mStack = nullptr;
    
    // Fixed resolution inputs
    QDoubleSpinBox *mResXSpin = nullptr;
    QDoubleSpinBox *mResYSpin = nullptr;

    // Scale factor input
    QDoubleSpinBox *mScaleSpin = nullptr;

    // Reference layer combo
    RasterLayerCombo *mRefLayerCombo = nullptr;
};
