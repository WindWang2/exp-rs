// src/app/dialogs/plugin_manager_dialog.cpp — Plugin Manager (Phase W)
#include "plugin_manager_dialog.h"

#include "exprs/plugin_diagnostics.h"
#include "exprs/plugin_registry.h"
#include "exprs/plugin_ui.h"
#include "plugins/framework/plugin_ui_host.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
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
    setWindowTitle( tr( "Plugin Manager" ) );
    resize( 760, 480 );

    auto *layout = new QVBoxLayout( this );
    mSummary = new QLabel( this );
    layout->addWidget( mSummary );

    auto *splitter = new QSplitter( Qt::Vertical, this );
    mTable = new QTableWidget( 0, 5, this );
    mTable->setHorizontalHeaderLabels( { tr( "ID" ), tr( "Name" ), tr( "Version" ), tr( "Status" ),
                                         tr( "Source" ) } );
    mTable->horizontalHeader()->setStretchLastSection( true );
    mTable->setSelectionBehavior( QAbstractItemView::SelectRows );
    mTable->setSelectionMode( QAbstractItemView::SingleSelection );
    mTable->setEditTriggers( QAbstractItemView::NoEditTriggers );
    splitter->addWidget( mTable );

    mDiagnostics = new QTextEdit( this );
    mDiagnostics->setReadOnly( true );
    mDiagnostics->setPlaceholderText( tr( "Diagnostics of the selected plugin" ) );
    splitter->addWidget( mDiagnostics );
    layout->addWidget( splitter, 1 );

    auto *buttons = new QHBoxLayout;
    mEnableButton = new QPushButton( tr( "Enable" ), this );
    mDisableButton = new QPushButton( tr( "Disable" ), this );
    auto *refreshButton = new QPushButton( tr( "Refresh" ), this );
    buttons->addWidget( mEnableButton );
    buttons->addWidget( mDisableButton );
    buttons->addWidget( refreshButton );
    buttons->addStretch( 1 );
    auto *closeBox = new QDialogButtonBox( QDialogButtonBox::Close, this );
    buttons->addWidget( closeBox );
    layout->addLayout( buttons );

    connect( mEnableButton, &QPushButton::clicked, this, [this]() { applyEnabled( true ); } );
    connect( mDisableButton, &QPushButton::clicked, this, [this]() { applyEnabled( false ); } );
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
    // Snapshot ids then copy each record under the registry lock: a live
    // vector& / pointer into mRecords dangles across refresh() (#943).
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    const std::vector<std::string> ids = registry.pluginIds();
    mTable->setRowCount( static_cast<int>( ids.size() ) );
    int validated = 0;
    int problem = 0;
    int row = 0;
    for ( const std::string &pluginId : ids )
    {
        exprs::PluginRecord record;
        if ( !registry.copyRecord( pluginId, record ) )
            continue;
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
        ++row;
    }
    mTable->setRowCount( row );
    mSummary->setText( tr( "Found %1 plugins (%2 usable, %3 broken). The scan only reads plugin.json,"
                           "Plugin binaries are not loaded." )
                           .arg( row )
                           .arg( validated )
                           .arg( problem ) );
}

void PluginManagerDialog::applyEnabled( bool enable )
{
    const auto selection = mTable->selectedItems();
    if ( selection.isEmpty() )
        return;
    const QString pluginId =
        mTable->item( selection.first()->row(), 0 )->data( Qt::UserRole ).toString();
    const std::string id = pluginId.toStdString();
    exprs::PluginRegistry &registry = exprs::PluginRegistry::instance();
    if ( !enable )
    {
        // Disabling a loaded plugin also unloads it, so the state the user
        // sees matches the persisted index. A refused unload (plugin still
        // executing) must NOT flip the persisted flag either — the plugin
        // stays loaded AND enabled, with the diagnostic surfaced.
        if ( !registry.unload( id ) && registry.isLoaded( id ) )
        {
            QString inUse;
            for ( const auto &item : registry.diagnostics().forPlugin( id ) )
            {
                if ( item.code == exprs::PluginDiagnosticCode::PluginInUse )
                    inUse = QString::fromStdString( item.message );
            }
            QMessageBox::warning( this, tr( "Plugin In Use" ),
                                  inUse.isEmpty() ? tr( "The plugin is executing and cannot be disabled." ) : inUse );
            populate();
            return;
        }
        registry.setEnabled( id, false );
    }
    else
    {
        registry.setEnabled( id, true );
        // Full round-trip (#755): re-run the same load + attach path the
        // startup shell used, instead of only flipping the persisted state.
        if ( registry.load( id ) )
        {
            auto *uiHost = sicnu::plugins::PluginUiHost::instance();
            const exprs::LoadedPlugin *loaded = registry.loaded( id );
            if ( loaded && loaded->uiContribution )
                uiHost->attachCollectedUi(
                    pluginId,
                    static_cast<exprs::UiContributionV1 *>( loaded->uiContribution ) );
        }
        else
        {
            exprs::PluginRecord snapshot;
            if ( registry.copyRecord( id, snapshot )
                 && snapshot.state == exprs::PluginState::Failed )
            {
                QMessageBox::warning( this, tr( "Plugin Failed to Load" ),
                                      tr( "Enablement saved, but the plugin failed to load — see the diagnostics." ) );
            }
        }
    }
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
        mDiagnostics->setPlainText( tr( "No diagnostics." ) );
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
