// src/app/dialogs/crs_preset_dialog.cpp
#include "crs_preset_dialog.h"
#include "dialog_help_catalog.h"
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

CrsPresetDialog::CrsPresetDialog( QWidget *parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Select CRS Preset" ) );
    
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
    auto *mainLayout = new QVBoxLayout( this );

    // Search bar at top
    auto *searchLayout = new QHBoxLayout();
    searchLayout->addWidget( new QLabel( tr( "Search:" ) ) );
    m_searchEdit = new QLineEdit( this );
    m_searchEdit->setPlaceholderText( tr( "Filter by name or EPSG code..." ) );
    SicnuDialogHelp::tip( m_searchEdit, tr( "按名称或 EPSG 代码过滤预设列表。" ) );
    searchLayout->addWidget( m_searchEdit );
    mainLayout->addLayout( searchLayout );

    // Splitter: tree on left, details on right
    auto *splitter = new QSplitter( Qt::Horizontal, this );

    // Left panel: tree view
    m_treeWidget = new QTreeWidget( splitter );
    m_treeWidget->setHeaderLabels( { tr( "Name" ), tr( "EPSG" ) } );
    m_treeWidget->setColumnCount( 2 );
    m_treeWidget->setRootIsDecorated( true );
    m_treeWidget->setAlternatingRowColors( true );
    m_treeWidget->setSelectionMode( QAbstractItemView::SingleSelection );
    SicnuDialogHelp::tip( m_treeWidget, tr( "常用坐标系分组列表。选中后右侧显示详情，双击可确认。" ) );

    // Right panel: details group box
    auto *detailsWidget = new QWidget( splitter );
    auto *detailsLayout = new QFormLayout( detailsWidget );
    detailsLayout->setContentsMargins( 8, 8, 8, 8 );

    m_nameLabel = new QLabel( this );
    m_nameLabel->setWordWrap( true );
    detailsLayout->addRow( tr( "Name:" ), m_nameLabel );

    m_epsgLabel = new QLabel( this );
    detailsLayout->addRow( tr( "EPSG Code:" ), m_epsgLabel );

    m_categoryLabel = new QLabel( this );
    detailsLayout->addRow( tr( "Category:" ), m_categoryLabel );

    m_descriptionLabel = new QLabel( this );
    m_descriptionLabel->setWordWrap( true );
    detailsLayout->addRow( tr( "Description:" ), m_descriptionLabel );

    m_wktLabel = new QLabel( this );
    m_wktLabel->setWordWrap( true );
    m_wktLabel->setTextInteractionFlags( Qt::TextSelectableByMouse );
    detailsLayout->addRow( tr( "WKT:" ), m_wktLabel );

    splitter->addWidget( m_treeWidget );
    splitter->addWidget( detailsWidget );
    splitter->setStretchFactor( 0, 2 );
    splitter->setStretchFactor( 1, 1 );

    mainLayout->addWidget( splitter, 1 );

    // Bottom buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_okButton = new QPushButton( tr( "OK" ), this );
    m_okButton->setDefault( true );
    m_okButton->setEnabled( false );
    m_cancelButton = new QPushButton( tr( "Cancel" ), this );
    btnLayout->addWidget( m_okButton );
    btnLayout->addWidget( m_cancelButton );
    mainLayout->addLayout( btnLayout );

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

    auto *helpRow = new QHBoxLayout();
    helpRow->addStretch();
    auto *helpBtn = new QPushButton( tr( "帮助" ), this );
    SicnuDialogHelp::tip( helpBtn, tr( "查看本对话框说明。" ) );
    connect( helpBtn, &QPushButton::clicked, this, [this]() {
        SicnuDialogHelp::showToolHelp( this, QStringLiteral( "crs_preset" ), windowTitle() );
    } );
    helpRow->addWidget( helpBtn );
    mainLayout->addLayout( helpRow );
}

void CrsPresetDialog::populateTree()
{
    m_treeWidget->clear();

    // "Recently Used" category first (if any)
    const QList<CrsPreset> recentPresets = CrsPresets::recentPresets();
    if ( !recentPresets.isEmpty() )
    {
        auto *categoryItem = new QTreeWidgetItem( m_treeWidget );
        categoryItem->setText( 0, tr( "Recently Used" ) );
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
