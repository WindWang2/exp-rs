// src/app/dialogs/crs_preset_dialog.cpp
#include "crs_preset_dialog.h"
#include "dialog_help_catalog.h"
#include "dialog_utils.h"
#include "crs_presets.h"

#include <qgscoordinatereferencesystem.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QDialogButtonBox>

CrsPresetDialog::CrsPresetDialog( QWidget *parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "选择坐标系预设" ) );
    
    SicnuDialogHelp::applyDialogChrome( this, QStringLiteral( "crs_preset" ) );
    resize( 650, 480 );
    setupUi();
    populateTree();
}

int CrsPresetDialog::selectedEpsg() const
{
    return m_selectedEpsg;
}

void CrsPresetDialog::setupUi()
{
    SicnuUi::polishDialog( this, 650 );
    resize( 720, 500 );

    auto *mainLayout = SicnuUi::makeDialogRootLayout( this );

    // Search bar at top
    auto *searchGroup = SicnuUi::makeGroup( this, tr( "坐标系预设检索" ) );
    auto *searchLayout = new QHBoxLayout( searchGroup );
    searchLayout->setContentsMargins( 10, 8, 10, 8 );
    searchLayout->setSpacing( 8 );

    auto *searchLabel = new QLabel( tr( "快速检索" ), searchGroup );
    m_searchEdit = new QLineEdit( searchGroup );
    m_searchEdit->setObjectName( QStringLiteral( "crsSearchEdit" ) );
    m_searchEdit->setPlaceholderText( tr( "按名称或 EPSG 代码过滤（如 WGS 84、3857、CGCS2000 等）…" ) );
    SicnuDialogHelp::tip( m_searchEdit, tr( "按坐标系名称或 EPSG 代码快速过滤预设列表。" ) );
    searchLayout->addWidget( searchLabel );
    searchLayout->addWidget( m_searchEdit, 1 );
    mainLayout->addWidget( searchGroup );

    // Splitter: tree on left, details on right
    auto *splitter = new QSplitter( Qt::Horizontal, this );

    // Left panel: tree view in group
    auto *treeGroup = SicnuUi::makeGroup( this, tr( "常用坐标系预设" ) );
    auto *treeGroupLayout = new QVBoxLayout( treeGroup );
    treeGroupLayout->setContentsMargins( 10, 8, 10, 8 );
    treeGroupLayout->setSpacing( 8 );

    m_treeWidget = new QTreeWidget( treeGroup );
    m_treeWidget->setObjectName( QStringLiteral( "crsTreeWidget" ) );
    m_treeWidget->setHeaderLabels( { tr( "坐标系名称" ), tr( "EPSG" ) } );
    m_treeWidget->setColumnCount( 2 );
    m_treeWidget->setRootIsDecorated( true );
    m_treeWidget->setAlternatingRowColors( true );
    m_treeWidget->setSelectionMode( QAbstractItemView::SingleSelection );
    SicnuDialogHelp::tip( m_treeWidget, tr( "常用坐标系分组列表。选中后右侧显示详情，双击可直接应用。" ) );
    treeGroupLayout->addWidget( m_treeWidget );

    // Right panel: details group box
    auto *detailsGroup = SicnuUi::makeGroup( this, tr( "坐标系详细信息" ) );
    auto *detailsLayout = SicnuUi::makeFormLayout( detailsGroup );

    m_nameLabel = new QLabel( detailsGroup );
    m_nameLabel->setWordWrap( true );
    detailsLayout->addRow( tr( "坐标系名称" ), m_nameLabel );

    m_epsgLabel = new QLabel( detailsGroup );
    detailsLayout->addRow( tr( "EPSG 代码" ), m_epsgLabel );

    m_categoryLabel = new QLabel( detailsGroup );
    detailsLayout->addRow( tr( "所属分类" ), m_categoryLabel );

    m_descriptionLabel = new QLabel( detailsGroup );
    m_descriptionLabel->setWordWrap( true );
    detailsLayout->addRow( tr( "详细描述" ), m_descriptionLabel );

    m_wktLabel = new QLabel( detailsGroup );
    m_wktLabel->setWordWrap( true );
    m_wktLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    detailsLayout->addRow( tr( "WKT 定义" ), m_wktLabel );

    splitter->addWidget( treeGroup );
    splitter->addWidget( detailsGroup );
    splitter->setStretchFactor( 0, 3 );
    splitter->setStretchFactor( 1, 2 );

    mainLayout->addWidget( splitter, 1 );

    // Single unified standard button box
    auto *buttonBox = new QDialogButtonBox( this );
    buttonBox->setObjectName( QStringLiteral( "crsPresetButtonBox" ) );

    auto *helpBtn = buttonBox->addButton( tr( "帮助" ), QDialogButtonBox::HelpRole );
    SicnuUi::markSecondary( helpBtn );
    SicnuDialogHelp::tip( helpBtn, tr( "查看坐标系预设说明与帮助。" ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
        SicnuDialogHelp::showToolHelp( this, QStringLiteral( "crs_preset" ), windowTitle() );
    } );

    m_cancelButton = buttonBox->addButton( tr( "取消" ), QDialogButtonBox::RejectRole );
    SicnuUi::markSecondary( m_cancelButton );

    m_okButton = buttonBox->addButton( tr( "确定" ), QDialogButtonBox::AcceptRole );
    SicnuUi::markPrimary( m_okButton );
    m_okButton->setDefault( true );
    m_okButton->setEnabled( false );

    mainLayout->addWidget( buttonBox );

    // Connections
    connect( m_searchEdit, &QLineEdit::textChanged,
             this, &CrsPresetDialog::onSearchTextChanged );
    connect( m_treeWidget, &QTreeWidget::currentItemChanged,
             this, &CrsPresetDialog::onTreeItemChanged );
    connect( m_treeWidget, &QTreeWidget::itemDoubleClicked,
             this, &CrsPresetDialog::onTreeItemDoubleClicked );
    connect( m_okButton, &QPushButton::clicked, this, [this]() {
        if ( m_selectedEpsg > 0 )
            CrsPresets::addRecentCrs( m_selectedEpsg );
        accept();
    } );
    connect( m_cancelButton, &QPushButton::clicked, this, &QDialog::reject );
}

void CrsPresetDialog::populateTree()
{
    m_treeWidget->clear();

    // "Recently Used" category first (if any)
    const QList<CrsPreset> recentPresets = CrsPresets::recentPresets();
    if ( !recentPresets.isEmpty() )
    {
        auto *categoryItem = new QTreeWidgetItem( m_treeWidget );
        categoryItem->setText( 0, tr( "最近使用" ) );
        categoryItem->setFlags( Qt::ItemIsEnabled );

        for ( const CrsPreset &preset : recentPresets )
        {
            auto *presetItem = new QTreeWidgetItem( categoryItem );
            presetItem->setText( 0, preset.name );
            presetItem->setText( 1, QString::number( preset.epsgCode ) );
            presetItem->setData( 0, Qt::UserRole, preset.epsgCode );
        }

        categoryItem->setExpanded( true );
    }

    // Remaining categories
    const QStringList categories = CrsPresets::categories();
    for ( const QString &category : categories )
    {
        auto *categoryItem = new QTreeWidgetItem( m_treeWidget );
        categoryItem->setText( 0, category );
        categoryItem->setFlags( Qt::ItemIsEnabled );

        const QList<CrsPreset> presets = CrsPresets::presetsByCategory( category );
        for ( const CrsPreset &preset : presets )
        {
            auto *presetItem = new QTreeWidgetItem( categoryItem );
            presetItem->setText( 0, preset.name );
            presetItem->setText( 1, QString::number( preset.epsgCode ) );
            presetItem->setData( 0, Qt::UserRole, preset.epsgCode );
        }

        categoryItem->setExpanded( true );
    }

    m_treeWidget->resizeColumnToContents( 0 );
    m_treeWidget->resizeColumnToContents( 1 );
}

void CrsPresetDialog::refreshTree()
{
    populateTree();
}

void CrsPresetDialog::onSearchTextChanged( const QString &text )
{
    filterTree( text.trimmed() );
}

void CrsPresetDialog::onTreeItemChanged( QTreeWidgetItem *current, QTreeWidgetItem * )
{
    if ( !current )
    {
        m_selectedEpsg = -1;
        m_okButton->setEnabled( false );
        return;
    }

    int epsg = current->data( 0, Qt::UserRole ).toInt();
    if ( epsg > 0 )
    {
        m_selectedEpsg = epsg;
        m_okButton->setEnabled( true );
        updateDetails( epsg );
    }
    else
    {
        // Category node selected
        m_selectedEpsg = -1;
        m_okButton->setEnabled( false );
    }
}

void CrsPresetDialog::onTreeItemDoubleClicked( QTreeWidgetItem *item, int )
{
    if ( !item )
        return;

    int epsg = item->data( 0, Qt::UserRole ).toInt();
    if ( epsg > 0 )
    {
        m_selectedEpsg = epsg;
        CrsPresets::addRecentCrs( epsg );
        accept();
    }
}

void CrsPresetDialog::updateDetails( int epsgCode )
{
    auto preset = CrsPresets::presetForEpsg( epsgCode );
    if ( preset )
    {
        m_nameLabel->setText( preset->name );
        m_epsgLabel->setText( QString::number( preset->epsgCode ) );
        m_categoryLabel->setText( preset->category );
        m_descriptionLabel->setText( preset->description );
    }
    else
    {
        m_nameLabel->setText( tr( "Unknown" ) );
        m_epsgLabel->setText( QString::number( epsgCode ) );
        m_categoryLabel->clear();
        m_descriptionLabel->clear();
    }

    // Show WKT from QGIS CRS if available
    QgsCoordinateReferenceSystem crs = QgsCoordinateReferenceSystem::fromEpsgId( epsgCode );
    if ( crs.isValid() )
    {
        QString wkt = crs.toWkt( Qgis::CrsWktVariant::Preferred );
        if ( wkt.length() > 300 )
            wkt = wkt.left( 300 ) + QStringLiteral( "..." );
        m_wktLabel->setText( wkt );
    }
    else
    {
        m_wktLabel->setText( tr( "N/A" ) );
    }
}

void CrsPresetDialog::filterTree( const QString &text )
{
    const bool showAll = text.isEmpty();

    for ( int i = 0; i < m_treeWidget->topLevelItemCount(); ++i )
    {
        QTreeWidgetItem *categoryItem = m_treeWidget->topLevelItem( i );
        int visibleChildCount = 0;

        for ( int j = 0; j < categoryItem->childCount(); ++j )
        {
            QTreeWidgetItem *childItem = categoryItem->child( j );
            if ( showAll )
            {
                childItem->setHidden( false );
                ++visibleChildCount;
            }
            else
            {
                bool match = childItem->text( 0 ).contains( text, Qt::CaseInsensitive )
                             || childItem->text( 1 ).contains( text, Qt::CaseInsensitive );
                childItem->setHidden( !match );
                if ( match )
                    ++visibleChildCount;
            }
        }

        categoryItem->setHidden( visibleChildCount == 0 );
        if ( visibleChildCount > 0 )
            categoryItem->setExpanded( true );
    }

    QTreeWidgetItem *current = m_treeWidget->currentItem();
    if ( current && ( current->isHidden() || ( current->parent() && current->parent()->isHidden() ) ) )
    {
        m_treeWidget->setCurrentItem( nullptr );
        m_selectedEpsg = -1;
        m_okButton->setEnabled( false );
        m_nameLabel->setText( tr( "N/A" ) );
        m_epsgLabel->setText( tr( "N/A" ) );
        m_categoryLabel->clear();
        m_descriptionLabel->clear();
        m_wktLabel->setText( tr( "N/A" ) );
    }
}
