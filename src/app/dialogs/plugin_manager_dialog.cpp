// src/app/dialogs/plugin_manager_dialog.cpp — Plugin Manager (Phase W)
#include "plugin_manager_dialog.h"

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_registry.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTextEdit>
#include <QVBoxLayout>

namespace {
QString stateLabel( const char *state )
{
    return QString::fromLatin1( state );
}
} // namespace

PluginManagerDialog::PluginManagerDialog( QWidget *parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "插件管理器" ) );
    resize( 760, 480 );

    auto *layout = new QVBoxLayout( this );
    mSummary = new QLabel( this );
    layout->addWidget( mSummary );

    auto *splitter = new QSplitter( Qt::Vertical, this );
    mTable = new QTableWidget( 0, 5, this );
    mTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "名称" ), tr( "版本" ), tr( "状态" ),
                                         tr( "来源" ) } );
    mTable->horizontalHeader()->setStretchLastSection( true );
    mTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    splitter->addWidget( mTable );

    mDiagnostics = new QTextEdit( this );
    mDiagnostics->setReadOnly( true );
    mDiagnostics->setPlaceholderText( tr( "选中插件的诊断信息" ) );
    splitter->addWidget( mDiagnostics );
    layout->addWidget( splitter, 1 );

    auto *buttons = new QHBoxLayout;
    mEnableButton = new QPushButton( tr( "启用" ), this );
    mDisableButton = new QPushButton( tr( "禁用" ), this );
    auto *refreshButton = new QPushButton( tr( "刷新" ), this );
    buttons->addWidget( mEnableButton );
    buttons->addWidget( mDisableButton );
    buttons->addWidget( refreshButton );
    buttons->addStretch( 1 );
    auto *closeBox = new QDialogButtonBox( QDialogButtonBox::Close, this );
    buttons->addWidget( closeBox );
    layout->addLayout( buttons );

    connect( mEnableButton, &QPushButton::clicked, this, &PluginManagerDialog::toggleEnabled );
    connect( mDisableButton, &QPushButton::clicked, this, &PluginManagerDialog::toggleEnabled );
    connect( refreshButton, &QPushButton::clicked, this, &PluginManagerDialog::refresh );
    connect( closeBox, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject );
    connect( mTable, &QTableWidget::itemSelectionChanged, this,
             &PluginManagerDialog::showDiagnostics );

    refresh();
}

void PluginManagerDialog::refresh()
{
    exprs::PluginRegistry::instance().refresh();
    populate();
}

void PluginManagerDialog::populate()
{
    const auto &records = exprs::PluginRegistry::instance().records();
    mTable->setRowCount( static_cast<int>( records.size() ) );
    int validated = 0;
    int problem = 0;
    for ( int row = 0; row < static_cast<int>( records.size() ); ++row )
    {
        const exprs::PluginRecord &record = records[row];
        auto *idItem = new QTableWidgetItem( QString::fromStdString( record.manifest.id ) );
        idItem->setData( Qt::UserRole, QString::fromStdString( record.manifest.id ) );
        mTable->setItem( row, 0, idItem );
        mTable->setItem( row, 1, new QTableWidgetItem( QString::fromStdString( record.manifest.name ) ) );
        mTable->setItem( row, 2, new QTableWidgetItem( QString::fromStdString( record.manifest.version ) ) );
        mTable->setItem( row, 3, new QTableWidgetItem( stateLabel( exprs::pluginStateName( record.state ) ) ) );
        mTable->setItem( row, 4, new QTableWidgetItem( QString::fromLatin1( exprs::pluginOriginName( record.origin ) ) ) );
        if ( record.state == exprs::PluginState::Validated || record.state == exprs::PluginState::Loaded )
            ++validated;
        else if ( record.state == exprs::PluginState::Broken || record.state == exprs::PluginState::Incompatible
                  || record.state == exprs::PluginState::Failed )
            ++problem;
    }
    mSummary->setText( tr( "已发现 %1 个插件（可用 %2，异常 %3）。扫描仅读取 plugin.json，"
                           "不会加载插件二进制。" )
                           .arg( records.size() )
                           .arg( validated )
                           .arg( problem ) );
}

void PluginManagerDialog::toggleEnabled()
{
    const auto selection = mTable->selectedItems();
    if ( selection.isEmpty() )
        return;
    const QString pluginId = mTable->item( selection.first()->row(), 0 )->data( Qt::UserRole ).toString();
    const auto *record = exprs::PluginRegistry::instance().record( pluginId.toStdString() );
    if ( !record )
        return;
    const bool enable = record->state != exprs::PluginState::Validated
                        && record->state != exprs::PluginState::Loaded;
    exprs::PluginRegistry::instance().setEnabled( pluginId.toStdString(), enable );
    populate();
}

void PluginManagerDialog::showDiagnostics()
{
    const auto selection = mTable->selectedItems();
    if ( selection.isEmpty() )
        return;
    const QString pluginId = mTable->item( selection.first()->row(), 0 )->data( Qt::UserRole ).toString();
    const auto diagnostics = exprs::PluginRegistry::instance().diagnostics().forPlugin(
        pluginId.toStdString() );
    if ( diagnostics.empty() )
    {
        mDiagnostics->setPlainText( tr( "无诊断信息。" ) );
        return;
    }
    QString text;
    for ( const auto &item : diagnostics )
    {
        text += QString::fromStdString( exprs::PluginDiagnostic::codeString( item.code ) ) + "  "
                + QString::fromStdString( item.message ) + "\n";
    }
    mDiagnostics->setPlainText( text );
}
