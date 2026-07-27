// band_composition_rail.h — band chips + real data range only (session meta lives on status bar)
#pragma once

#include <QPointer>
#include <QWidget>

class QgsRasterLayer;
class QLabel;
class QHBoxLayout;

/**
 * Thin rail under the ribbon: RGB/gray band chips and Real Data Range.
 * Opacity / scale / CRS / layer name / tasks belong on the status bar.
 */
class BandCompositionRail : public QWidget
{
    Q_OBJECT

public:
    explicit BandCompositionRail( QWidget *parent = nullptr );

public Q_SLOTS:
    void setRasterLayer( QgsRasterLayer *layer );
    void refresh();

private:
    void rebuildChips();
    void clearChips();
    void updateRangeLabel();
    QLabel *makeChip( const QString &role, const QString &text );

    QPointer<QgsRasterLayer> m_layer;
    QHBoxLayout *m_chipLayout = nullptr;
    QWidget *m_chipHost = nullptr;
    QLabel *m_rangeLabel = nullptr;
    bool m_refreshing = false;
};
