/***************************************************************************
 * layer_sections.cpp — general/metadata section implementation
 ***************************************************************************/
#include "layer_sections.h"

#include <QLabel>
#include <QVBoxLayout>

#include <qgsmaplayer.h>
#include <qgscoordinatereferencesystem.h>
#include <qgsrectangle.h>
#include <qgsrasterlayer.h>
#include <qgsvectorlayer.h>
#include <qgswkbtypes.h>

namespace sicnu::app
{
namespace
{

QString escapeCell( const QString &text )
{
    return text.toHtmlEscaped();
}

} // namespace

// ── General ───────────────────────────────────────────────────────────────

LayerGeneralSection::LayerGeneralSection( QWidget *parent )
    : InspectorSection( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_summary = new QLabel( this );
    m_summary->setObjectName( QStringLiteral( "rsInspectorGeneral" ) );
    m_summary->setTextFormat( Qt::RichText );
    m_summary->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_summary->setWordWrap( true );
    layout->addWidget( m_summary );
    layout->addStretch( 1 );
}

bool LayerGeneralSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    return snapshot.activeLayer != nullptr;
}

void LayerGeneralSection::populate( const SelectionContextSnapshot &snapshot )
{
    QgsMapLayer *layer = snapshot.activeLayer;
    if ( !layer )
    {
        m_summary->setText( tr( "未选中图层。" ) );
        return;
    }

    const QString type = [&layer] {
        switch ( layer->type() )
        {
            case Qgis::LayerType::Raster:
                return tr( "栅格" );
            case Qgis::LayerType::Vector:
                return tr( "矢量" );
            case Qgis::LayerType::VectorTile:
                return tr( "矢量瓦片" );
            case Qgis::LayerType::Mesh:
                return tr( "网格" );
            case Qgis::LayerType::PointCloud:
                return tr( "点云" );
            default:
                return tr( "图层" );
        }
    }();
    const QString validity = layer->isValid() ? tr( "有效" ) : tr( "无效 / 数据源缺失" );
    const QString crs = layer->crs().isValid() ? layer->crs().authid() : tr( "未定义" );
    const QgsRectangle extent = layer->extent();
    const QString extentText = extent.isEmpty()
                                   ? tr( "—" )
                                   : QStringLiteral( "%1, %2 — %3, %4" )
                                         .arg( extent.xMinimum(), 0, 'f', 2 )
                                         .arg( extent.yMinimum(), 0, 'f', 2 )
                                         .arg( extent.xMaximum(), 0, 'f', 2 )
                                         .arg( extent.yMaximum(), 0, 'f', 2 );

    QString extra;
    if ( auto *raster = qobject_cast<QgsRasterLayer *>( layer ) )
    {
        extra = tr( "<tr><td>波段数</td><td>%1</td></tr>"
                    "<tr><td>像元尺寸</td><td>%2 × %3</td></tr>" )
                    .arg( raster->bandCount() )
                    .arg( raster->width() )
                    .arg( raster->height() );
    }
    else if ( auto *vector = qobject_cast<QgsVectorLayer *>( layer ) )
    {
        extra = tr( "<tr><td>要素数</td><td>%1</td></tr>"
                    "<tr><td>几何类型</td><td>%2</td></tr>" )
                    .arg( vector->featureCount() )
                    .arg( escapeCell( QgsWkbTypes::displayString(
                        vector->wkbType() ) ) );
    }

    m_summary->setText(
        tr( "<b>%1</b><br>"
            "<table cellspacing='2'>"
            "<tr><td>类型</td><td>%2</td></tr>"
            "<tr><td>状态</td><td>%3</td></tr>"
            "<tr><td>CRS</td><td>%4</td></tr>"
            "<tr><td>范围</td><td>%5</td></tr>"
            "%6"
            "</table>" )
            .arg( escapeCell( layer->name() ), type, validity, escapeCell( crs ),
                  extentText, extra ) );
}

// ── Metadata (lazy) ──────────────────────────────────────────────────────

LayerMetadataSection::LayerMetadataSection( QWidget *parent )
    : InspectorSection( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_body = new QLabel( this );
    m_body->setObjectName( QStringLiteral( "rsInspectorMetadata" ) );
    m_body->setTextFormat( Qt::RichText );
    m_body->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );
    layout->addStretch( 1 );
}

bool LayerMetadataSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    return snapshot.activeLayer != nullptr;
}

void LayerMetadataSection::populate( const SelectionContextSnapshot &snapshot )
{
    QgsMapLayer *layer = snapshot.activeLayer;
    if ( !layer )
    {
        m_body->setText( tr( "未选中图层。" ) );
        return;
    }

    // Authoritative layer metadata (same source as the properties dialog).
    // Rendering through the shared rich-text label keeps this one bounded
    // page; a paged view arrives with the 100k work (Milestone O).
    m_body->setText( layer->htmlMetadata() );
}

void LayerMetadataSection::cancelPending()
{
    // Section is synchronous today; hook kept for the async metadata contract.
}

} // namespace sicnu::app
