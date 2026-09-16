// src/app/widgets/band_composite_palette.h — D13 RGB composite selector
#pragma once

#include <QWidget>
#include <QPointer>

class QgsRasterLayer;
class QComboBox;
class QDoubleSpinBox;

namespace exp_gui
{

    /// RGB band-composite palette: pick the R/G/B band mapping (1-based,
    /// 0 = unset) and a stretch method for the bound raster layer.
    /// The observed layer is held through a QPointer — destroying the layer
    /// never leaves the palette holding a dangling reference.
    class BandCompositePalette : public QWidget
    {
        Q_OBJECT

      public:
        explicit BandCompositePalette( QWidget *parent = nullptr );
        ~BandCompositePalette() override;

        /// Binds the palette to a raster layer and populates the band combos.
        /// Re-binding resets the current mapping.
        void bindRasterLayer( QgsRasterLayer *layer );

        /// Sets the RGB band mapping (1-based, 0 = unset) and emits
        /// bandMappingChanged. Values are clamped into [0, bandCount] —
        /// illegal input degrades instead of crashing.
        void setRgbMapping( int redBand1Based, int greenBand1Based, int blueBand1Based );

        /// Sets the stretch mode (0 = none, 1 = linear min/max, 2 = min/max
        /// with @p clipPercent cut) and emits stretchMethodChanged.
        void setStretchMethod( int stretchMode, double clipPercent );

        int redBand() const { return m_redBand; }
        int greenBand() const { return m_greenBand; }
        int blueBand() const { return m_blueBand; }
        bool hasLayer() const { return !m_layer.isNull(); }
        int bandCount() const { return m_bandCount; }

      signals:
        void bandMappingChanged( int r, int g, int b );
        void stretchMethodChanged( int stretchMode, double clipPercent );

      private:
        int clampBand( int band1Based ) const;
        void applyComboMapping();

        QPointer<QgsRasterLayer> m_layer;
        int m_bandCount = 0;
        int m_redBand = 0;
        int m_greenBand = 0;
        int m_blueBand = 0;
        QComboBox *m_redCombo = nullptr;
        QComboBox *m_greenCombo = nullptr;
        QComboBox *m_blueCombo = nullptr;
        QComboBox *m_stretchCombo = nullptr;
        QDoubleSpinBox *m_clipPercent = nullptr;
    };

} // namespace exp_gui
