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
#include <qgsfields.h>
#include <qgsfield.h>
#include <qgswkbtypes.h>

#include <algorithm>

#include <QStringList>

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

// ── Vector structure (Inspector 2.0, Milestone G) ─────────────────────────

VectorStructureSection::VectorStructureSection( QWidget *parent )
    : InspectorSection( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_body = new QLabel( this );
    m_body->setObjectName( QStringLiteral( "rsInspectorVector" ) );
    m_body->setTextFormat( Qt::RichText );
    m_body->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );
    layout->addStretch( 1 );
}

bool VectorStructureSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    return snapshot.firstVectorLayer() != nullptr;
}

void VectorStructureSection::populate( const SelectionContextSnapshot &snapshot )
{
    QgsVectorLayer *vector = snapshot.firstVectorLayer();
    if ( !vector )
    {
        m_body->setText( tr( "未选中矢量图层。" ) );
        return;
    }

    // Bounded field listing: first N fields, then a count of the remainder —
    // never an unbounded dump on the UI thread.
    constexpr int kMaxFields = 32;
    const QgsFields fields = vector->fields();
    QStringList fieldRows;
    const int listed = std::min( fields.count(), kMaxFields );
    for ( int i = 0; i < listed; ++i )
    {
        fieldRows += tr( "<tr><td>%1</td><td>%2</td></tr>" )
                         .arg( escapeCell( fields.at( i ).name() ),
                               escapeCell( fields.at( i ).typeName() ) );
    }
    const QString overflow =
        fields.count() > listed
            ? tr( "<tr><td colspan='2'>…及其余 %1 个字段</td></tr>" ).arg( fields.count() - listed )
            : QString();

    const QString editState = vector->isEditable()
                                  ? ( vector->isModified() ? tr( "编辑中（有未保存修改）" )
                                                           : tr( "编辑中" ) )
                                  : ( vector->readOnly() ? tr( "只读" ) : tr( "可编辑（未开始）" ) );

    m_body->setText(
        tr( "<b>%1</b><br>"
            "<table cellspacing='2'>"
            "<tr><td>要素数</td><td>%2</td></tr>"
            "<tr><td>选中要素</td><td>%3</td></tr>"
            "<tr><td>几何类型</td><td>%4</td></tr>"
            "<tr><td>CRS</td><td>%5</td></tr>"
            "<tr><td>编辑状态</td><td>%6</td></tr>"
            "<tr><td>字段数</td><td>%7</td></tr>"
            "%8%9"
            "</table>" )
            .arg( escapeCell( vector->name() ) )
            .arg( vector->featureCount() )
            .arg( vector->selectedFeatureCount() )
            .arg( escapeCell( QgsWkbTypes::displayString( vector->wkbType() ) ) )
            .arg( escapeCell( vector->crs().isValid() ? vector->crs().authid() : tr( "未定义" ) ) )
            .arg( editState )
            .arg( fields.count() )
            .arg( fieldRows.join( QString() ), overflow ) );
}

// ── SAR context (Inspector 2.0, Milestone G) ──────────────────────────────

SarInfoSection::SarInfoSection( QWidget *parent )
    : InspectorSection( parent )
{
    auto *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 8, 8, 8, 8 );
    m_body = new QLabel( this );
    m_body->setObjectName( QStringLiteral( "rsInspectorSar" ) );
    m_body->setTextFormat( Qt::RichText );
    m_body->setAlignment( Qt::AlignTop | Qt::AlignLeft );
    m_body->setWordWrap( true );
    layout->addWidget( m_body );
    layout->addStretch( 1 );
}

bool SarInfoSection::supports( const SelectionContextSnapshot &snapshot ) const
{
    // Conservative: the SAR tab exists only when the projection flagged SAR
    // (predicate override or the documented product-token heuristic).
    return snapshot.hasSar && snapshot.firstRasterLayer() != nullptr;
}

void SarInfoSection::populate( const SelectionContextSnapshot &snapshot )
{
    QgsRasterLayer *raster = snapshot.firstRasterLayer();
    if ( !raster )
    {
        m_body->setText( tr( "未选中 SAR 栅格。" ) );
        return;
    }

    // Standard SAR facts commonly present in product metadata. Extracted from
    // the authoritative provider metadata (in memory, one bounded pass).
    static const QStringList sarKeys = {
        QStringLiteral( "POLARISATION" ), QStringLiteral( "polarization" ),
        QStringLiteral( "ORBIT" ),        QStringLiteral( "orbit" ),
        QStringLiteral( "INCIDENCE" ),    QStringLiteral( "incidence" ),
        QStringLiteral( "LOOK" ),         QStringLiteral( "look" ),
        QStringLiteral( "CALIBRATION" ),  QStringLiteral( "calibration" ),
        QStringLiteral( "PASS" ),         QStringLiteral( "SENSOR" ),
        QStringLiteral( "BEAM" ),
    };

    // The provider's HTML metadata already renders key/value pairs; pick the
    // lines mentioning SAR-relevant keys instead of dumping everything.
    const QStringList lines = raster->htmlMetadata().split(QLatin1Char('\n'));
    QStringList facts;
    for ( const QString &line : lines )
    {
        for ( const QString &key : sarKeys )
        {
            if ( line.contains( key ) )
            {
                facts += tr( "<tr><td>%1</td></tr>" ).arg( escapeCell( line.trimmed() ) );
                break;
            }
        }
        if ( facts.size() >= 24 ) // bounded page
            break;
    }

    m_body->setText(
        tr( "<b>%1</b><br>"
            "识别为 SAR 产品（基于数据源/名称启发式，可被显式判定覆盖）。"
            "<table cellspacing='2'>"
            "<tr><td>波段数</td><td>%2</td></tr>"
            "<tr><td>CRS</td><td>%3</td></tr>"
            "%4"
            "</table>"
            "<br>%5" )
            .arg( escapeCell( raster->name() ) )
            .arg( raster->bandCount() )
            .arg( escapeCell( raster->crs().isValid() ? raster->crs().authid() : tr( "未定义" ) ) )
            .arg( facts.join( QString() ) )
            .arg( facts.isEmpty()
                      ? tr( "提供方元数据中未发现极化/轨道/入射角等标准 SAR 字段。" )
                      : QString() ) );
}

} // namespace sicnu::app
