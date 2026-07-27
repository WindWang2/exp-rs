// band_composition_rail.cpp
#include "band_composition_rail.h"

#include <qgsrasterlayer.h>
#include <qgsrasterdataprovider.h>
#include <qgsrasterrenderer.h>
#include <qgssinglebandgrayrenderer.h>
#include <qgsmultibandcolorrenderer.h>
#include <qgis.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
#include <QSizePolicy>
#include <cmath>

#include <qgsmaplayer.h>

BandCompositionRail::BandCompositionRail( QWidget *parent )
    : QWidget( parent )
{
    setObjectName( QStringLiteral( "rsBandCompositionRail" ) );
    setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );
    setFixedHeight( 28 );

    auto *root = new QHBoxLayout( this );
    root->setContentsMargins( 10, 2, 10, 2 );
    root->setSpacing( 6 );

    auto *title = new QLabel( tr( "波段" ), this );
    title->setObjectName( QStringLiteral( "rsBandMetaLabel" ) );
    root->addWidget( title );

    m_chipHost = new QWidget( this );
    m_chipLayout = new QHBoxLayout( m_chipHost );
    m_chipLayout->setContentsMargins( 0, 0, 0, 0 );
    m_chipLayout->setSpacing( 4 );
    root->addWidget( m_chipHost );

    m_rangeLabel = new QLabel( this );
    m_rangeLabel->setObjectName( QStringLiteral( "rsBandRangeLabel" ) );
    m_rangeLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    root->addWidget( m_rangeLabel );

    root->addStretch( 1 );

    refresh();
}

QLabel *BandCompositionRail::makeChip( const QString &role, const QString &text )
{
    auto *chip = new QLabel( text, m_chipHost );
    chip->setObjectName( QStringLiteral( "rsBandChip" ) );
    chip->setProperty( "role", role );
    chip->setAlignment( Qt::AlignCenter );
    chip->setToolTip( tr( "通道 %1 → 波段 %2" ).arg( role, text ) );
    if ( QStyle *st = chip->style() )
    {
        st->unpolish( chip );
        st->polish( chip );
    }
    return chip;
}

void BandCompositionRail::setRasterLayer( QgsRasterLayer *layer )
{
    if ( m_layer.data() == layer )
    {
        refresh();
        return;
    }

    if ( m_layer )
        disconnect( m_layer, nullptr, this, nullptr );

    m_layer = layer;
    if ( m_layer )
    {
        connect( m_layer, &QgsMapLayer::rendererChanged, this, &BandCompositionRail::refresh );
        connect( m_layer, &QObject::destroyed, this, [this]() {
            m_layer.clear();
            refresh();
        } );
    }
    refresh();
}

void BandCompositionRail::clearChips()
{
    if ( !m_chipHost || !m_chipLayout )
        return;

    const QList<QWidget *> kids = m_chipHost->findChildren<QWidget *>( QString(), Qt::FindDirectChildrenOnly );
    for ( QWidget *w : kids )
    {
        m_chipLayout->removeWidget( w );
        delete w;
    }
    while ( QLayoutItem *item = m_chipLayout->takeAt( 0 ) )
        delete item;
}

void BandCompositionRail::rebuildChips()
{
    if ( !m_chipLayout || !m_chipHost )
        return;

    clearChips();

    if ( !m_layer || !m_layer->isValid() )
    {
        auto *empty = new QLabel( tr( "—" ), m_chipHost );
        empty->setObjectName( QStringLiteral( "rsBandMetaLabel" ) );
        m_chipLayout->addWidget( empty );
        return;
    }

    QgsRasterRenderer *renderer = m_layer->renderer();
    if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( renderer ) )
    {
        m_chipLayout->addWidget( makeChip( QStringLiteral( "Gray" ),
                                           QString::number( gray->inputBand() ) ) );
    }
    else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( renderer ) )
    {
        m_chipLayout->addWidget( makeChip( QStringLiteral( "R" ), QString::number( rgb->redBand() ) ) );
        m_chipLayout->addWidget( makeChip( QStringLiteral( "G" ), QString::number( rgb->greenBand() ) ) );
        m_chipLayout->addWidget( makeChip( QStringLiteral( "B" ), QString::number( rgb->blueBand() ) ) );
    }
    else
    {
        const int n = m_layer->bandCount();
        if ( n >= 3 )
        {
            m_chipLayout->addWidget( makeChip( QStringLiteral( "R" ), QStringLiteral( "1" ) ) );
            m_chipLayout->addWidget( makeChip( QStringLiteral( "G" ), QStringLiteral( "2" ) ) );
            m_chipLayout->addWidget( makeChip( QStringLiteral( "B" ), QStringLiteral( "3" ) ) );
        }
        else
        {
            m_chipLayout->addWidget( makeChip( QStringLiteral( "Gray" ), QStringLiteral( "1" ) ) );
        }
    }
}

void BandCompositionRail::updateRangeLabel()
{
    if ( !m_rangeLabel )
        return;

    QString rangeText = tr( "量程 —" );
    if ( m_layer && m_layer->isValid() )
    {
        int band = 1;
        if ( auto *gray = dynamic_cast<QgsSingleBandGrayRenderer *>( m_layer->renderer() ) )
            band = gray->inputBand();
        else if ( auto *rgb = dynamic_cast<QgsMultiBandColorRenderer *>( m_layer->renderer() ) )
            band = rgb->redBand();

        if ( QgsRasterDataProvider *provider = m_layer->dataProvider() )
        {
            const QgsRasterBandStats stats = provider->bandStatistics(
                band,
                Qgis::RasterBandStatistic::Min | Qgis::RasterBandStatistic::Max,
                QgsRectangle(),
                0 );
            if ( std::isfinite( stats.minimumValue ) && std::isfinite( stats.maximumValue ) )
            {
                rangeText = tr( "量程 [%1 ~ %2]" )
                              .arg( stats.minimumValue, 0, 'g', 5 )
                              .arg( stats.maximumValue, 0, 'g', 5 );
            }
        }
    }
    m_rangeLabel->setText( rangeText );
}

void BandCompositionRail::refresh()
{
    if ( m_refreshing )
        return;
    m_refreshing = true;
    rebuildChips();
    updateRangeLabel();
    m_refreshing = false;
}
